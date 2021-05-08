
// Copyright (c) 2009-2018 The Bitcoin Core developers
// Copyright (c) 2018-2020 The Veil developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <wallet/wallet.h>
#include <veil/ringct/anonwallet.h>
#include <veil/budget.h>

#include <checkpoints.h>
#include <chain.h>
#include <wallet/coincontrol.h>
#include <consensus/consensus.h>
#include <consensus/validation.h>
#include <fs.h>
#include <key.h>
#include <key_io.h>
#include <keystore.h>
#include <validation.h>
#include <net.h>
#include <policy/fees.h>
#include <policy/policy.h>
#include <policy/rbf.h>
#include <primitives/block.h>
#include <primitives/transaction.h>
#include <script/script.h>
#include <shutdown.h>
#include <timedata.h>
#include <txmempool.h>
#include <utilmoneystr.h>
#include <wallet/fees.h>
#include <wallet/walletutil.h>
#include <veil/zerocoin/accumulators.h>
#include <wallet/deterministicmint.h>
#include <veil/zerocoin/denomination_functions.h>
#include <veil/mnemonic/mnemonic.h>
#include <veil/zerocoin/mintmeta.h>
#include <veil/zerocoin/zchain.h>
#include <veil/zerocoin/zwallet.h>
#include <libzerocoin/Params.h>
#include <veil/proofofstake/stakeinput.h>
#include <veil/proofofstake/kernel.h>

#include <algorithm>
#include <assert.h>
#include <future>

#include <boost/algorithm/string/join.hpp>
#include <boost/algorithm/string/replace.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>
#include <util.h>

#include <boost/thread.hpp>

extern std::map<uint256, int64_t>mapComputeTimeTransactions;

static const size_t OUTPUT_GROUP_MAX_ENTRIES = 10;
int64_t nAutoMintStartupTime = GetTime(); //!< Client startup time for use with automint

//! Sum of all zero coin denominations - 10000 + 1000 + 100 + 10
static const int ZQ_11110 = 11110;

static CCriticalSection cs_wallets;
static std::vector<std::shared_ptr<CWallet>> vpwallets GUARDED_BY(cs_wallets);

bool fGlobalUnlockSpendCache = false;

bool AddWallet(const std::shared_ptr<CWallet>& wallet)
{
    LOCK(cs_wallets);
    assert(wallet);
    std::vector<std::shared_ptr<CWallet>>::const_iterator i = std::find(vpwallets.begin(), vpwallets.end(), wallet);
    if (i != vpwallets.end()) return false;
    vpwallets.push_back(wallet);
    return true;
}

bool RemoveWallet(const std::shared_ptr<CWallet>& wallet)
{
    LOCK(cs_wallets);
    assert(wallet);
    std::vector<std::shared_ptr<CWallet>>::iterator i = std::find(vpwallets.begin(), vpwallets.end(), wallet);
    if (i == vpwallets.end()) return false;
    vpwallets.erase(i);
    return true;
}

bool HasWallets()
{
    LOCK(cs_wallets);
    return !vpwallets.empty();
}

std::vector<std::shared_ptr<CWallet>> GetWallets()
{
    LOCK(cs_wallets);
    return vpwallets;
}

std::shared_ptr<CWallet> GetWallet(const std::string& name)
{
    LOCK(cs_wallets);
    for (const std::shared_ptr<CWallet>& wallet : vpwallets) {
        if (wallet->GetName() == name) return wallet;
    }
    return nullptr;
}

std::shared_ptr<CWallet> GetMainWallet()
{
    LOCK(cs_wallets);
    if (!vpwallets.empty())
        return vpwallets.at(0);

    return nullptr;
}

// Custom deleter for shared_ptr<CWallet>.
static void ReleaseWallet(CWallet* wallet)
{
    wallet->WalletLogPrintf("Releasing wallet\n");
    wallet->BlockUntilSyncedToCurrentChain();
    wallet->Flush();
    delete wallet;
}

const uint32_t BIP32_HARDENED_KEY_LIMIT = 0x80000000;

const uint256 ABANDON_HASH(uint256S("0000000000000000000000000000000000000000000000000000000000000001"));

/** @defgroup mapWallet
 *
 * @{
 */

std::string COutput::ToString() const
{
    return strprintf("COutput(%s, %d, %d) [%s]", tx->GetHash().ToString(), i, nDepth, FormatMoney(tx->tx->vpout[i]->GetValue()));
}

/** A class to identify which pubkeys a script and a keystore have in common. */
class CAffectedKeysVisitor : public boost::static_visitor<void> {
private:
    const CKeyStore &keystore;
    std::vector<CKeyID> &vKeys;

public:
    /**
     * @param[in] keystoreIn The CKeyStore that is queried for the presence of a pubkey.
     * @param[out] vKeysIn A vector to which a script's pubkey identifiers are appended if they are in the keystore.
     */
    CAffectedKeysVisitor(const CKeyStore &keystoreIn, std::vector<CKeyID> &vKeysIn) : keystore(keystoreIn), vKeys(vKeysIn) {}

    /**
     * Apply the visitor to each destination in a script, recursively to the redeemscript
     * in the case of p2sh destinations.
     * @param[in] script The CScript from which destinations are extracted.
     * @post Any CKeyIDs that script and keystore have in common are appended to the visitor's vKeys.
     */
    void Process(const CScript &script) {
        txnouttype type;
        std::vector<CTxDestination> vDest;
        int nRequired;
        if (ExtractDestinations(script, type, vDest, nRequired)) {
            for (const CTxDestination &dest : vDest)
                boost::apply_visitor(*this, dest);
        }
    }

    void operator()(const CKeyID &keyId) {
        if (keystore.HaveKey(keyId))
            vKeys.push_back(keyId);
    }

    void operator()(const CScriptID &scriptId) {
        CScript script;
        if (keystore.GetCScript(scriptId, script))
            Process(script);
    }

    void operator()(const CKeyID256 &keyId) {
        //if (keystore.HaveKey(keyId))
        //    vKeys.push_back(keyId);
    }

    void operator()(const CScriptID256 &scriptId) {
        //CScript script;
        //if (keystore.GetCScript(scriptId, script))
        //    Process(script);
    }

    void operator()(const CNoDestination &none) {}

    void operator()(const CExtKey &none) {}
    void operator()(const CStealthAddress &sxAddr) {}

    void operator()(const WitnessV0ScriptHash& scriptID)
    {
        CScriptID id;
        CRIPEMD160().Write(scriptID.begin(), 32).Finalize(id.begin());
        CScript script;
        if (keystore.GetCScript(id, script)) {
            Process(script);
        }
    }

    void operator()(const WitnessV0KeyHash& keyid)
    {
        CKeyID id(keyid);
        if (keystore.HaveKey(id)) {
            vKeys.push_back(id);
        }
    }

    template<typename X>
    void operator()(const X &none) {}
};

const CWalletTx* CWallet::GetWalletTx(const uint256& hash) const
{
    LOCK(cs_wallet);
    std::map<uint256, CWalletTx>::const_iterator it = mapWallet.find(hash);
    if (it == mapWallet.end())
        return nullptr;
    return &(it->second);
}

CPubKey CWallet::GenerateNewKey(WalletBatch &batch, bool internal)
{
    assert(!IsWalletFlagSet(WALLET_FLAG_DISABLE_PRIVATE_KEYS));
    AssertLockHeld(cs_wallet); // mapKeyMetadata
    bool fCompressed = CanSupportFeature(FEATURE_COMPRPUBKEY); // default to compressed public keys if we want 0.6.0 wallets

    CKey secret;

    // Create new metadata
    int64_t nCreationTime = GetTime();
    CKeyMetadata metadata(nCreationTime);

    // use HD key derivation if HD was enabled during wallet creation
    if (CWallet::IsHDEnabled()) {
        DeriveNewChildKey(batch, metadata, secret, (CanSupportFeature(FEATURE_HD_SPLIT) ? internal : false));
    } else {
        secret.MakeNewKey(fCompressed);
    }

    // Compressed public keys were introduced in version 0.6.0
    if (fCompressed) {
        SetMinVersion(FEATURE_COMPRPUBKEY);
    }

    CPubKey pubkey = secret.GetPubKey();
    assert(secret.VerifyPubKey(pubkey));

    mapKeyMetadata[pubkey.GetID()] = metadata;
    UpdateTimeFirstKey(nCreationTime);

    if (!AddKeyPubKeyWithDB(batch, secret, pubkey)) {
        throw std::runtime_error(std::string(__func__) + ": AddKey failed");
    }
    return pubkey;
}

#define BIP44_PURPOSE 0x8000002C

CExtKey DeriveKeyFromPath(const CExtKey& keyAccount, const BIP32Path& vPath)
{
    CExtKey keyDerive = keyAccount;
    for (auto p : vPath) {
        uint32_t nAccount = p.first;
        bool fHardened = p.second;
        if (fHardened)
            nAccount |= BIP32_HARDENED_KEY_LIMIT;
        keyDerive.Derive(keyDerive, nAccount);
    }
    return keyDerive;
}

std::string BIP32PathToString(const BIP32Path& vPath)
{
    std::string str;
    for (auto p : vPath) {
        str += std::to_string(p.first);
        if (p.second)
            str += "'";
        str += "/";
    }
    return str;
}

// Veil:
// Pass in {[0, true], [0, false], [0, false]} to match default bip44 matching
// the same derivation used here https://iancoleman.io/bip39/
CExtKey CWallet::DeriveBIP32Path(const BIP32Path& vPath)
{
    CKey seed1;                     //seed (512bit) stored between two keys
    CKey seed2;
    CExtKey keyMaster;             //hd master key
    CExtKey keyPurpose;            //key at m/44'
    CExtKey keyCoin;               //key at m/44'/coinid'

    // try to get the seed
    if (!GetKey(hdChain.seed_id, seed1))
        throw std::runtime_error(std::string(__func__) + ": seed not found");

    // veil default is 512 bit seed
    if (hdChain.Is512BitSeed()) {
        if (!GetKey(hdChain.seed_id_r, seed2))
            throw std::runtime_error(std::string(__func__) + ": seed2 not found");
        keyMaster.SetSeedFromKeys(seed1, seed2);
    } else {
        keyMaster.SetSeed(seed1.begin(), seed1.size());
    }
    seed1.Clear();
    seed2.Clear();

    // Automatically derive m/44'/slip44_coinid then build the rest of the request from there
    keyMaster.Derive(keyPurpose, BIP44_PURPOSE);
    keyPurpose.Derive(keyCoin, Params().BIP44ID());

    return DeriveKeyFromPath(keyCoin, vPath);
}

void CWallet::DeriveNewChildKey(WalletBatch &batch, CKeyMetadata& metadata, CKey& secret, bool internal)
{
    // Derive keys according to BIP44/32. Derive each stage hardened.
    // Internal: m/44'/slip44id'/0'/0'/d'
    // External: m/44'/slip44id'/0'/1'/d'
    BIP32Path vPath = {{0, true}, {static_cast<uint32_t>(internal), true}};
    CExtKey accountKey = DeriveBIP32Path(vPath);

    // derive child key at next index, skip keys already known to the wallet
    CExtKey childKey;
    do {
        if (internal) {
            accountKey.Derive(childKey, hdChain.nInternalChainCounter | BIP32_HARDENED_KEY_LIMIT);
            metadata.hdKeypath = strprintf("m/44'/%d'/0'/1'/%d'", (Params().BIP44ID() - BIP32_HARDENED_KEY_LIMIT), hdChain.nInternalChainCounter);
            hdChain.nInternalChainCounter++;
        } else {
            accountKey.Derive(childKey, hdChain.nExternalChainCounter | BIP32_HARDENED_KEY_LIMIT);
            metadata.hdKeypath = strprintf("m/44'/%d'/0'/0'/%d'", (Params().BIP44ID() - BIP32_HARDENED_KEY_LIMIT), hdChain.nExternalChainCounter);
            hdChain.nExternalChainCounter++;
        }
    } while (HaveKey(childKey.key.GetPubKey().GetID()));

    secret = childKey.key;
    //LogPrintf("Final Key %s=%s\n", metadata.hdKeypath, HexStr(secret.GetPubKey()));
    metadata.hd_seed_id = hdChain.seed_id;
    metadata.hd_seed_id_r = hdChain.seed_id_r;
    // update the chain model in the database
    if (!batch.WriteHDChain(hdChain))
        throw std::runtime_error(std::string(__func__) + ": Writing HD chain model failed");
}

int CWallet::GetAccountKeyCount() const
{
    return hdChain.nExternalChainCounter;
}

bool CWallet::AddKeyPubKeyWithDB(WalletBatch &batch, const CKey& secret, const CPubKey &pubkey)
{
    AssertLockHeld(cs_wallet); // mapKeyMetadata

    // CCryptoKeyStore has no concept of wallet databases, but calls AddCryptedKey
    // which is overridden below.  To avoid flushes, the database handle is
    // tunneled through to it.
    bool needsDB = !encrypted_batch;
    if (needsDB) {
        encrypted_batch = &batch;
    }
    if (!CCryptoKeyStore::AddKeyPubKey(secret, pubkey)) {
        if (needsDB) encrypted_batch = nullptr;
        return false;
    }
    if (needsDB) encrypted_batch = nullptr;

    // check if we need to remove from watch-only
    CScript script;
    script = GetScriptForDestination(pubkey.GetID());
    if (HaveWatchOnly(script)) {
        RemoveWatchOnly(script);
    }
    script = GetScriptForRawPubKey(pubkey);
    if (HaveWatchOnly(script)) {
        RemoveWatchOnly(script);
    }

    if (!IsCrypted()) {
        return batch.WriteKey(pubkey, secret.GetPrivKey(), mapKeyMetadata[pubkey.GetID()]);
    }
    return true;
}

bool CWallet::AddKeyPubKey(const CKey& secret, const CPubKey &pubkey)
{
    WalletBatch batch(*database);
    return CWallet::AddKeyPubKeyWithDB(batch, secret, pubkey);
}

bool CWallet::AddCryptedKey(const CPubKey &vchPubKey,
                            const std::vector<unsigned char> &vchCryptedSecret)
{
    if (!CCryptoKeyStore::AddCryptedKey(vchPubKey, vchCryptedSecret))
        return false;
    {
        LOCK(cs_wallet);
        if (encrypted_batch)
            return encrypted_batch->WriteCryptedKey(vchPubKey,
                                                        vchCryptedSecret,
                                                        mapKeyMetadata[vchPubKey.GetID()]);
        else
            return WalletBatch(*database).WriteCryptedKey(vchPubKey,
                                                            vchCryptedSecret,
                                                            mapKeyMetadata[vchPubKey.GetID()]);
    }
}

void CWallet::LoadKeyMetadata(const CKeyID& keyID, const CKeyMetadata &meta)
{
    AssertLockHeld(cs_wallet); // mapKeyMetadata
    UpdateTimeFirstKey(meta.nCreateTime);
    mapKeyMetadata[keyID] = meta;
}

void CWallet::LoadScriptMetadata(const CScriptID& script_id, const CKeyMetadata &meta)
{
    AssertLockHeld(cs_wallet); // m_script_metadata
    UpdateTimeFirstKey(meta.nCreateTime);
    m_script_metadata[script_id] = meta;
}

bool CWallet::LoadCryptedKey(const CPubKey &vchPubKey, const std::vector<unsigned char> &vchCryptedSecret)
{
    return CCryptoKeyStore::AddCryptedKey(vchPubKey, vchCryptedSecret);
}

/**
 * Update wallet first key creation time. This should be called whenever keys
 * are added to the wallet, with the oldest key creation time.
 */
void CWallet::UpdateTimeFirstKey(int64_t nCreateTime)
{
    AssertLockHeld(cs_wallet);
    if (nCreateTime <= 1) {
        // Cannot determine birthday information, so set the wallet birthday to
        // the beginning of time.
        nTimeFirstKey = 1;
    } else if (!nTimeFirstKey || nCreateTime < nTimeFirstKey) {
        nTimeFirstKey = nCreateTime;
    }
}

bool CWallet::AddCScript(const CScript& redeemScript)
{
    if (!CCryptoKeyStore::AddCScript(redeemScript))
        return false;
    return WalletBatch(*database).WriteCScript(Hash160(redeemScript), redeemScript);
}

bool CWallet::LoadCScript(const CScript& redeemScript)
{
    /* A sanity check was added in pull #3843 to avoid adding redeemScripts
     * that never can be redeemed. However, old wallets may still contain
     * these. Do not add them to the wallet and warn. */
    if (redeemScript.size() > MAX_SCRIPT_ELEMENT_SIZE)
    {
        std::string strAddr = EncodeDestination(CScriptID(redeemScript));
        WalletLogPrintf("%s: Warning: This wallet contains a redeemScript of size %i which exceeds maximum size %i thus can never be redeemed. Do not use address %s.\n", __func__, redeemScript.size(), MAX_SCRIPT_ELEMENT_SIZE, strAddr);
        return true;
    }

    return CCryptoKeyStore::AddCScript(redeemScript);
}

bool CWallet::AddWatchOnly(const CScript& dest)
{
    if (!CCryptoKeyStore::AddWatchOnly(dest))
        return false;
    const CKeyMetadata& meta = m_script_metadata[CScriptID(dest)];
    UpdateTimeFirstKey(meta.nCreateTime);
    NotifyWatchonlyChanged(true);
    return WalletBatch(*database).WriteWatchOnly(dest, meta);
}

bool CWallet::AddWatchOnly(const CScript& dest, int64_t nCreateTime)
{
    m_script_metadata[CScriptID(dest)].nCreateTime = nCreateTime;
    return AddWatchOnly(dest);
}

bool CWallet::RemoveWatchOnly(const CScript &dest)
{
    AssertLockHeld(cs_wallet);
    if (!CCryptoKeyStore::RemoveWatchOnly(dest))
        return false;
    if (!HaveWatchOnly())
        NotifyWatchonlyChanged(false);
    if (!WalletBatch(*database).EraseWatchOnly(dest))
        return false;

    return true;
}

bool CWallet::LoadWatchOnly(const CScript &dest)
{
    return CCryptoKeyStore::AddWatchOnly(dest);
}

bool CWallet::RestoreBaseCoinAddresses(uint32_t nCount)
{
    WalletBatch wdb(GetDBHandle());
    for (unsigned int i = 0; i < nCount; i++) {
        GenerateNewKey(wdb, false);
    }
    return true;
}

bool CWallet::LockWallet()
{
    zwalletMain->Lock();
    pAnonWalletMain->Lock();
    fUnlockForStakingOnly = false;
    return CCryptoKeyStore::Lock();
}

bool CWallet::UnlockZerocoinWallet()
{
    CKey keyZerocoinMaster;
    if (!GetZerocoinSeed(keyZerocoinMaster))
        return error("%s: failed to derive zerocoin master key", __func__);
    auto seedID = keyZerocoinMaster.GetPubKey().GetID();
    if (seedID != zwalletMain->GetMasterSeedID())
        return error("%s: derived zerocoin key %s does not match expected key %s", __func__, seedID.GetHex(), zwalletMain->GetMasterSeedID().GetHex());
    zwalletMain->SetMasterSeed(keyZerocoinMaster);
    LogPrintf("%s: cwallet set zerocoin masterseed to %s\n", __func__, keyZerocoinMaster.GetPubKey().GetID().GetHex());
    return true;
}

bool CWallet::UnlockAnonWallet()
{
    CExtKey keyAnonMaster;
    if (!GetAnonWalletSeed(keyAnonMaster))
        return error("%s: failed to derive anon wallet master seed", __func__);

    auto idSeedDB = pAnonWalletMain->GetMasterID();
    auto idDerived = keyAnonMaster.key.GetPubKey().GetID();
    if (idDerived != idSeedDB)
        return error("%s: derived anon wallet key %s does not match expected key %s", __func__, idDerived.GetHex(), idSeedDB.GetHex());

    pAnonWalletMain->UnlockWallet(keyAnonMaster);
    return true;
}

bool CWallet::Unlock(const SecureString& strWalletPassphrase, bool fUnlockForStakingOnly)
{
    CCrypter crypter;
    CKeyingMaterial _vMasterKey;

    bool fSuccess = false;
    {
        {
            LOCK(cs_wallet);
            this->fUnlockForStakingOnly = fUnlockForStakingOnly;
            for (const MasterKeyMap::value_type& pMasterKey : mapMasterKeys) {
                if (!crypter.SetKeyFromPassphrase(strWalletPassphrase, pMasterKey.second.vchSalt, pMasterKey.second.nDeriveIterations, pMasterKey.second.nDerivationMethod))
                    return false;
                if (!crypter.Decrypt(pMasterKey.second.vchCryptedKey, _vMasterKey))
                    continue; // try another master key
                if (CCryptoKeyStore::Unlock(_vMasterKey)) {
                    fSuccess = true;
                    break;
                }
            }
            if (!fSuccess)
                return false;
        }

        WalletBatch walletdb(*database);
        if (!walletdb.LoadHDChain(hdChain))
            return error("%s: failed to load hd chain from database", __func__);
        if (!UnlockZerocoinWallet())
            return error("%s: failed to unlock zerocoin wallet", __func__);
        if (!UnlockAnonWallet())
            return error("%s: failed to unlock anon wallet", __func__);
    }

    return fSuccess;
}

bool CWallet::ChangeWalletPassphrase(const SecureString& strOldWalletPassphrase, const SecureString& strNewWalletPassphrase)
{
    bool fWasLocked = IsLocked();

    {
        LOCK(cs_wallet);
        LockWallet();

        CCrypter crypter;
        CKeyingMaterial _vMasterKey;
        for (MasterKeyMap::value_type& pMasterKey : mapMasterKeys)
        {
            if(!crypter.SetKeyFromPassphrase(strOldWalletPassphrase, pMasterKey.second.vchSalt, pMasterKey.second.nDeriveIterations, pMasterKey.second.nDerivationMethod))
                return false;
            if (!crypter.Decrypt(pMasterKey.second.vchCryptedKey, _vMasterKey))
                return false;
            if (CCryptoKeyStore::Unlock(_vMasterKey))
            {
                int64_t nStartTime = GetTimeMillis();
                crypter.SetKeyFromPassphrase(strNewWalletPassphrase, pMasterKey.second.vchSalt, pMasterKey.second.nDeriveIterations, pMasterKey.second.nDerivationMethod);
                pMasterKey.second.nDeriveIterations = static_cast<unsigned int>(pMasterKey.second.nDeriveIterations * (100 / ((double)(GetTimeMillis() - nStartTime))));

                nStartTime = GetTimeMillis();
                crypter.SetKeyFromPassphrase(strNewWalletPassphrase, pMasterKey.second.vchSalt, pMasterKey.second.nDeriveIterations, pMasterKey.second.nDerivationMethod);
                pMasterKey.second.nDeriveIterations = (pMasterKey.second.nDeriveIterations + static_cast<unsigned int>(pMasterKey.second.nDeriveIterations * 100 / ((double)(GetTimeMillis() - nStartTime)))) / 2;

                if (pMasterKey.second.nDeriveIterations < 25000)
                    pMasterKey.second.nDeriveIterations = 25000;

                WalletLogPrintf("Wallet passphrase changed to an nDeriveIterations of %i\n", pMasterKey.second.nDeriveIterations);

                if (!crypter.SetKeyFromPassphrase(strNewWalletPassphrase, pMasterKey.second.vchSalt, pMasterKey.second.nDeriveIterations, pMasterKey.second.nDerivationMethod))
                    return false;
                if (!crypter.Encrypt(_vMasterKey, pMasterKey.second.vchCryptedKey))
                    return false;
                WalletBatch(*database).WriteMasterKey(pMasterKey.first, pMasterKey.second);
                if (fWasLocked)
                    LockWallet();
                return true;
            }
        }
    }

    return false;
}

void CWallet::ChainStateFlushed(const CBlockLocator& loc)
{
    WalletBatch batch(*database);
    batch.WriteBestBlock(loc);
}

void CWallet::SetMinVersion(enum WalletFeature nVersion, WalletBatch* batch_in, bool fExplicit)
{
    LOCK(cs_wallet); // nWalletVersion
    if (nWalletVersion >= nVersion)
        return;

    // when doing an explicit upgrade, if we pass the max version permitted, upgrade all the way
    if (fExplicit && nVersion > nWalletMaxVersion)
            nVersion = FEATURE_LATEST;

    nWalletVersion = nVersion;

    if (nVersion > nWalletMaxVersion)
        nWalletMaxVersion = nVersion;

    {
        WalletBatch* batch = batch_in ? batch_in : new WalletBatch(*database);
        if (nWalletVersion > 40000)
            batch->WriteMinVersion(nWalletVersion);
        if (!batch_in)
            delete batch;
    }
}

bool CWallet::SetMaxVersion(int nVersion)
{
    LOCK(cs_wallet); // nWalletVersion, nWalletMaxVersion
    // cannot downgrade below current version
    if (nWalletVersion > nVersion)
        return false;

    nWalletMaxVersion = nVersion;

    return true;
}

std::set<uint256> CWallet::GetConflicts(const uint256& txid) const
{
    std::set<uint256> result;
    AssertLockHeld(cs_wallet);

    std::map<uint256, CWalletTx>::const_iterator it = mapWallet.find(txid);
    if (it == mapWallet.end())
        return result;
    const CWalletTx& wtx = it->second;

    std::pair<TxSpends::const_iterator, TxSpends::const_iterator> range;

    for (const CTxIn& txin : wtx.tx->vin)
    {
        if (mapTxSpends.count(txin.prevout) <= 1 || txin.IsZerocoinSpend())
            continue;  // No conflict if zero or one spends
        range = mapTxSpends.equal_range(txin.prevout);
        for (TxSpends::const_iterator _it = range.first; _it != range.second; ++_it)
            result.insert(_it->second);
    }
    return result;
}

bool CWallet::HasWalletSpend(const uint256& txid) const
{
    AssertLockHeld(cs_wallet);
    auto iter = mapTxSpends.lower_bound(COutPoint(txid, 0));
    return (iter != mapTxSpends.end() && iter->first.hash == txid);
}

void CWallet::Flush(bool shutdown)
{
    database->Flush(shutdown);
}

void CWallet::SyncMetaData(std::pair<TxSpends::iterator, TxSpends::iterator> range)
{
    // We want all the wallet transactions in range to have the same metadata as
    // the oldest (smallest nOrderPos).
    // So: find smallest nOrderPos:

    int nMinOrderPos = std::numeric_limits<int>::max();
    const CWalletTx* copyFrom = nullptr;
    for (TxSpends::iterator it = range.first; it != range.second; ++it) {
        const CWalletTx* wtx = &mapWallet.at(it->second);
        if (wtx->nOrderPos < nMinOrderPos) {
            nMinOrderPos = wtx->nOrderPos;
            copyFrom = wtx;
        }
    }

    if (!copyFrom) {
        return;
    }

    // Now copy data from copyFrom to rest:
    for (TxSpends::iterator it = range.first; it != range.second; ++it)
    {
        const uint256& hash = it->second;
        CWalletTx* copyTo = &mapWallet.at(hash);
        if (copyFrom == copyTo) continue;
        assert(copyFrom && "Oldest wallet transaction in range assumed to have been found.");
        if (!copyFrom->IsEquivalentTo(*copyTo)) continue;
        copyTo->mapValue = copyFrom->mapValue;
        copyTo->vOrderForm = copyFrom->vOrderForm;
        // fTimeReceivedIsTxTime not copied on purpose
        // nTimeReceived not copied on purpose
        copyTo->nTimeSmart = copyFrom->nTimeSmart;
        copyTo->fFromMe = copyFrom->fFromMe;
        // nOrderPos not copied on purpose
        // cached members not copied on purpose
    }
}

/**
 * Outpoint is spent if any non-conflicted transaction
 * spends it:
 */
bool CWallet::IsSpent(const uint256& hash, unsigned int n) const
{
    const COutPoint outpoint(hash, n);
    std::pair<TxSpends::const_iterator, TxSpends::const_iterator> range;
    range = mapTxSpends.equal_range(outpoint);

    for (TxSpends::const_iterator it = range.first; it != range.second; ++it)
    {
        const uint256& wtxid = it->second;
        std::map<uint256, CWalletTx>::const_iterator mit = mapWallet.find(wtxid);
        if (mit != mapWallet.end()) {
            int depth = mit->second.GetDepthInMainChain();
            if (depth > 0  || (depth == 0 && !mit->second.isAbandoned()))
                return true; // Spent
        }
    }

    if (setAnonTx.count(hash))
        return pAnonWalletMain->IsSpent(hash, n);

    return false;
}

void CWallet::AddToSpends(const COutPoint& outpoint, const uint256& wtxid)
{
    mapTxSpends.insert(std::make_pair(outpoint, wtxid));

    setLockedCoins.erase(outpoint);

    std::pair<TxSpends::iterator, TxSpends::iterator> range;
    range = mapTxSpends.equal_range(outpoint);
    SyncMetaData(range);
}


void CWallet::AddToSpends(const uint256& wtxid)
{
    auto it = mapWallet.find(wtxid);
    assert(it != mapWallet.end());
    CWalletTx& thisTx = it->second;
    if (thisTx.IsCoinBase() || thisTx.IsZerocoinSpend()) // Coinbases/zcspends don't spend anything in CWallet!
        return;

    for (const CTxIn& txin : thisTx.tx->vin) {
        if (txin.IsAnonInput()) // Anon inputs do not spend anything
            return;
        AddToSpends(txin.prevout, wtxid);
    }
}

bool CWallet::EncryptWallet(const SecureString& strWalletPassphrase)
{
    if (IsCrypted())
        return false;

    CKeyingMaterial _vMasterKey;

    _vMasterKey.resize(WALLET_CRYPTO_KEY_SIZE);
    GetStrongRandBytes(&_vMasterKey[0], WALLET_CRYPTO_KEY_SIZE);

    CMasterKey kMasterKey;

    kMasterKey.vchSalt.resize(WALLET_CRYPTO_SALT_SIZE);
    GetStrongRandBytes(&kMasterKey.vchSalt[0], WALLET_CRYPTO_SALT_SIZE);

    CCrypter crypter;
    int64_t nStartTime = GetTimeMillis();
    crypter.SetKeyFromPassphrase(strWalletPassphrase, kMasterKey.vchSalt, 25000, kMasterKey.nDerivationMethod);
    kMasterKey.nDeriveIterations = static_cast<unsigned int>(2500000 / ((double)(GetTimeMillis() - nStartTime)));

    nStartTime = GetTimeMillis();
    crypter.SetKeyFromPassphrase(strWalletPassphrase, kMasterKey.vchSalt, kMasterKey.nDeriveIterations, kMasterKey.nDerivationMethod);
    kMasterKey.nDeriveIterations = (kMasterKey.nDeriveIterations + static_cast<unsigned int>(kMasterKey.nDeriveIterations * 100 / ((double)(GetTimeMillis() - nStartTime)))) / 2;

    if (kMasterKey.nDeriveIterations < 25000)
        kMasterKey.nDeriveIterations = 25000;

    WalletLogPrintf("Encrypting Wallet with an nDeriveIterations of %i\n", kMasterKey.nDeriveIterations);

    if (!crypter.SetKeyFromPassphrase(strWalletPassphrase, kMasterKey.vchSalt, kMasterKey.nDeriveIterations, kMasterKey.nDerivationMethod))
        return false;
    if (!crypter.Encrypt(_vMasterKey, kMasterKey.vchCryptedKey))
        return false;

    {
        LOCK(cs_wallet);
        mapMasterKeys[++nMasterKeyMaxID] = kMasterKey;
        assert(!encrypted_batch);
        encrypted_batch = new WalletBatch(*database);
        if (!encrypted_batch->TxnBegin()) {
            delete encrypted_batch;
            encrypted_batch = nullptr;
            return false;
        }
        encrypted_batch->WriteMasterKey(nMasterKeyMaxID, kMasterKey);

        if (!EncryptKeys(_vMasterKey))
        {
            encrypted_batch->TxnAbort();
            delete encrypted_batch;
            // We now probably have half of our keys encrypted in memory, and half not...
            // die and let the user reload the unencrypted wallet.
            assert(false);
        }

        // Encryption was introduced in version 0.4.0
        SetMinVersion(FEATURE_WALLETCRYPT, encrypted_batch, true);

        if (!encrypted_batch->TxnCommit()) {
            delete encrypted_batch;
            // We now have keys encrypted in memory, but not on disk...
            // die to avoid confusion and let the user reload the unencrypted wallet.
            assert(false);
        }

        delete encrypted_batch;
        encrypted_batch = nullptr;

        CCryptoKeyStore::Lock();
        CWallet::Unlock(strWalletPassphrase, false);

        // if we are using HD, replace the HD seed with a new one
//        if (CWallet::IsHDEnabled()) {
//            SetHDSeed(GenerateNewSeed());
//        }

        NewKeyPool();
        CCryptoKeyStore::Lock();
        fUnlockForStakingOnly = false;

        // Need to completely rewrite the wallet file; if we don't, bdb might keep
        // bits of the unencrypted private key in slack space in the database file.
        database->Rewrite();

    }
    NotifyStatusChanged(this);

    return true;
}

DBErrors CWallet::ReorderTransactions()
{
    LOCK(cs_wallet);
    WalletBatch batch(*database);

    // Old wallets didn't have any defined order for transactions
    // Probably a bad idea to change the output of this

    // First: get all CWalletTx into a sorted-by-time multimap.
    typedef std::multimap<int64_t, CWalletTx*> TxItems;
    TxItems txByTime;

    for (auto& entry : mapWallet)
    {
        CWalletTx* wtx = &entry.second;
        txByTime.insert(std::make_pair(wtx->nTimeReceived, wtx));
    }

    nOrderPosNext = 0;
    std::vector<int64_t> nOrderPosOffsets;
    for (TxItems::iterator it = txByTime.begin(); it != txByTime.end(); ++it)
    {
        CWalletTx *const pwtx = (*it).second;
        int64_t& nOrderPos = pwtx->nOrderPos;

        if (nOrderPos == -1)
        {
            nOrderPos = nOrderPosNext++;
            nOrderPosOffsets.push_back(nOrderPos);

            if (!batch.WriteTx(*pwtx))
                return DBErrors::LOAD_FAIL;
        }
        else
        {
            int64_t nOrderPosOff = 0;
            for (const int64_t& nOffsetStart : nOrderPosOffsets)
            {
                if (nOrderPos >= nOffsetStart)
                    ++nOrderPosOff;
            }
            nOrderPos += nOrderPosOff;
            nOrderPosNext = std::max(nOrderPosNext, nOrderPos + 1);

            if (!nOrderPosOff)
                continue;

            // Since we're changing the order, write it back
            if (!batch.WriteTx(*pwtx))
                return DBErrors::LOAD_FAIL;
        }
    }
    batch.WriteOrderPosNext(nOrderPosNext);

    return DBErrors::LOAD_OK;
}

int64_t CWallet::IncOrderPosNext(WalletBatch *batch)
{
    AssertLockHeld(cs_wallet); // nOrderPosNext
    int64_t nRet = nOrderPosNext++;
    if (batch) {
        batch->WriteOrderPosNext(nOrderPosNext);
    } else {
        WalletBatch(*database).WriteOrderPosNext(nOrderPosNext);
    }
    return nRet;
}

void CWallet::MarkDirty()
{
    {
        LOCK(cs_wallet);
        for (std::pair<const uint256, CWalletTx>& item : mapWallet)
            item.second.MarkDirty();
    }
}

bool CWallet::MarkReplaced(const uint256& originalHash, const uint256& newHash)
{
    LOCK(cs_wallet);

    auto mi = mapWallet.find(originalHash);

    // There is a bug if MarkReplaced is not called on an existing wallet transaction.
    assert(mi != mapWallet.end());

    CWalletTx& wtx = (*mi).second;

    // Ensure for now that we're not overwriting data
    assert(wtx.mapValue.count("replaced_by_txid") == 0);

    wtx.mapValue["replaced_by_txid"] = newHash.ToString();

    WalletBatch batch(*database, "r+");

    bool success = true;
    if (!batch.WriteTx(wtx)) {
        WalletLogPrintf("%s: Updating batch tx %s failed\n", __func__, wtx.GetHash().ToString());
        success = false;
    }

    NotifyTransactionChanged(this, originalHash, CT_UPDATED);

    return success;
}

bool CWallet::AddToWallet(const CWalletTx& wtxIn, bool fFlushOnClose)
{
    LOCK(cs_wallet);

    WalletBatch batch(*database, "r+", fFlushOnClose);

    uint256 hash = wtxIn.GetHash();
    bool fHaveAnonRecord = setAnonTx.count(hash);

    // Inserts only if not already there, returns tx inserted or tx found
    std::pair<std::map<uint256, CWalletTx>::iterator, bool> ret = mapWallet.insert(std::make_pair(hash, wtxIn));
    CWalletTx& wtx = (*ret.first).second;
    wtx.BindWallet(this);
    bool fInsertedNew = ret.second;
    if (fInsertedNew) {
        if (mapComputeTimeTransactions.count(hash)) {
            wtx.nComputeTime = mapComputeTimeTransactions.at(hash);
        }
        wtx.nTimeReceived = GetAdjustedTime();
        wtx.nOrderPos = IncOrderPosNext(&batch);
        wtx.m_it_wtxOrdered = wtxOrdered.insert(std::make_pair(wtx.nOrderPos, &wtx));
        wtx.nTimeSmart = ComputeTimeSmart(wtx);
        if (fHaveAnonRecord) {
            wtx.mapValue.emplace("anon", "");
            CTransactionRecord rtx;
            if (!pAnonWalletMain->AddToRecord(rtx, *mapWallet.at(hash).tx, nullptr, 0, false))
                return error("%s: FIXME! Failed to add anon tx record", __func__);
        }
        AddToSpends(hash);
    }

    bool fUpdated = false;
    if (!fInsertedNew)
    {
        // Merge
        if (!wtxIn.hashUnset() && wtxIn.hashBlock != wtx.hashBlock)
        {
            wtx.hashBlock = wtxIn.hashBlock;
            fUpdated = true;
        }
        // If no longer abandoned, update
        if (wtxIn.hashBlock.IsNull() && wtx.isAbandoned())
        {
            wtx.hashBlock = wtxIn.hashBlock;
            fUpdated = true;
        }
        if (wtxIn.nIndex != -1 && (wtxIn.nIndex != wtx.nIndex))
        {
            wtx.nIndex = wtxIn.nIndex;
            fUpdated = true;
        }
        if (wtxIn.fFromMe && wtxIn.fFromMe != wtx.fFromMe)
        {
            wtx.fFromMe = wtxIn.fFromMe;
            fUpdated = true;
        }
        // If we have a witness-stripped version of this transaction, and we
        // see a new version with a witness, then we must be upgrading a pre-segwit
        // wallet.  Store the new version of the transaction with the witness,
        // as the stripped-version must be invalid.
        // TODO: Store all versions of the transaction, instead of just one.
        if (wtxIn.tx->HasWitness() && !wtx.tx->HasWitness()) {
            wtx.SetTx(wtxIn.tx);
            fUpdated = true;
        }
        // Make sure wtx has knowledge that there is an anon txrecord associated with it
        if (wtxIn.mapValue.count("anon") != wtx.mapValue.count("anon")) {
            wtx.mapValue["anon"] = "";
            fUpdated = true;
        }
    }

    //// debug print
    WalletLogPrintf("AddToWallet %s  %s%s\n", wtxIn.GetHash().ToString(), (fInsertedNew ? "new" : ""), (fUpdated ? "update" : ""));

    // Write to disk
    if (fInsertedNew || fUpdated)
        if (!batch.WriteTx(wtx))
            return false;

    // Break debit/credit balance caches:
    wtx.MarkDirty();

    // Notify UI of new or updated transaction
    NotifyTransactionChanged(this, hash, fInsertedNew ? CT_NEW : CT_UPDATED);

    // notify an external script when a wallet transaction comes in or is updated
    std::string strCmd = gArgs.GetArg("-walletnotify", "");

    if (!strCmd.empty())
    {
        boost::replace_all(strCmd, "%s", wtxIn.GetHash().GetHex());
        std::thread t(runCommand, strCmd);
        t.detach(); // thread runs free
    }

    return true;
}

void CWallet::LoadToWallet(const CWalletTx& wtxIn)
{
    uint256 hash = wtxIn.GetHash();
    const auto& ins = mapWallet.emplace(hash, wtxIn);
    CWalletTx& wtx = ins.first->second;
    wtx.BindWallet(this);
    if (/* insertion took place */ ins.second) {
        wtx.m_it_wtxOrdered = wtxOrdered.insert(std::make_pair(wtx.nOrderPos, &wtx));
    }
    AddToSpends(hash);
    for (const CTxIn& txin : wtx.tx->vin) {
        auto it = mapWallet.find(txin.prevout.hash);
        if (it != mapWallet.end() && !txin.IsAnonInput() && !txin.IsZerocoinSpend()) {
            CWalletTx& prevtx = it->second;
            if (prevtx.nIndex == -1 && !prevtx.hashUnset()) {
                MarkConflicted(prevtx.hashBlock, wtx.GetHash());
            }
        }
    }
    //Link to tx record within anon wallet
    if (wtx.mapValue.count("anon"))
        setAnonTx.emplace(hash);
}

bool CWallet::AddToWalletIfInvolvingMe(const CTransactionRef& ptx, const CBlockIndex* pIndex, int posInBlock, bool fUpdate)
{
    const CTransaction& tx = *ptx;
    uint256 txid = tx.GetHash();
    bool fMyZerocoinSpend = false;
    {
        AssertLockHeld(cs_wallet);

        //Check if Anon Wallet has this
        bool fAnonRecord = false;
        if (tx.HasBlindedValues()) {
            pAnonWalletMain->AddToWalletIfInvolvingMe(ptx, pIndex, posInBlock, fUpdate);

            //If a tx record was added to anon wallet, give knowledge of that to the main wallet
            if (pAnonWalletMain->mapRecords.count(tx.GetHash())) {
                setAnonTx.emplace(tx.GetHash());
                fAnonRecord = true;
            }
        }

        // Check for spent zerocoins
        if (tx.IsZerocoinSpend()) {
            std::set<uint256> setSerials;
            TxToSerialHashSet(&tx, setSerials);

            for (const uint256& hashSerial : setSerials) {
                // Send signal to wallet if this is ours
                if (IsMyZerocoinSpend(hashSerial)) {
                    LogPrintf("%s: detected spent zerocoin in transaction %s \n", __func__, txid.GetHex());
                    SetSerialSpent(hashSerial, txid);
                    fMyZerocoinSpend = true;
                }
            }
        }

        if (pIndex != nullptr) {
            for (const CTxIn& txin : tx.vin) {
                if (txin.IsZerocoinSpend() || tx.IsCoinBase())
                    continue;

                //Anon wallet tx already added above
                if (txin.IsAnonInput())
                    continue;

                std::pair<TxSpends::const_iterator, TxSpends::const_iterator> range = mapTxSpends.equal_range(txin.prevout);
                while (range.first != range.second) {
                    if (range.first->second != tx.GetHash()) {
                        WalletLogPrintf("CWallet: Transaction %s (in block %s) conflicts with wallet transaction %s (both spend %s:%i)\n",
                                tx.GetHash().ToString(), pIndex->GetBlockHash().ToString(), range.first->second.ToString(),
                                range.first->first.hash.ToString(), range.first->first.n);
                        MarkConflicted(pIndex->GetBlockHash(), range.first->second);
                    }
                    range.first++;
                }
            }
        }

        bool fExisted = mapWallet.count(tx.GetHash()) != 0;

        if (fExisted && !fUpdate) return false;
        if (fExisted || IsMine(tx) || IsFromMe(tx) || fAnonRecord || fMyZerocoinSpend)
        {
            /* Check if any keys in the wallet keypool that were supposed to be unused
             * have appeared in a new transaction. If so, remove those keys from the keypool.
             * This can happen when restoring an old wallet backup that does not contain
             * the mostly recently created transactions from newer versions of the wallet.
             */

            // loop though all outputs
            for (const CTxOutBaseRef& pOut : tx.vpout) {
                //Anon tx already added above
                if (!pOut->IsStandardOutput())
                    continue;

                auto txout = *pOut->GetStandardOutput();
                // extract addresses and check if they match with an unused keypool key
                std::vector<CKeyID> vAffected;
                CAffectedKeysVisitor(*this, vAffected).Process(txout.scriptPubKey);
                for (const CKeyID &keyid : vAffected) {
                    std::map<CKeyID, int64_t>::const_iterator mi = m_pool_key_to_index.find(keyid);
                    if (mi != m_pool_key_to_index.end()) {
                        WalletLogPrintf("%s: Detected a used keypool key, mark all keypool key up to this key as used\n", __func__);
                        MarkReserveKeysAsUsed(mi->second);

                        if (!TopUpKeyPool()) {
                            WalletLogPrintf("%s: Topping up keypool failed (locked wallet)\n", __func__);
                        }
                    }
                }
            }

            CWalletTx wtx(this, ptx);

            // Get merkle branch if transaction was found in a block
            if (pIndex != nullptr)
                wtx.SetMerkleBranch(pIndex, posInBlock);

            return AddToWallet(wtx, false);
        }
    }
    return false;
}

bool CWallet::TransactionCanBeAbandoned(const uint256& hashTx) const
{
    LOCK2(cs_main, cs_wallet);
    const CWalletTx* wtx = GetWalletTx(hashTx);
    return wtx && !wtx->isAbandoned() && wtx->GetDepthInMainChain() == 0 && !wtx->InMempool();
}

void CWallet::MarkInputsDirty(const CTransactionRef& tx)
{
    for (const CTxIn& txin : tx->vin) {
        auto it = mapWallet.find(txin.prevout.hash);
        if (it != mapWallet.end()) {
            it->second.MarkDirty();
        }
    }
}

bool CWallet::AbandonTransaction(const uint256& hashTx)
{
    LOCK2(cs_main, cs_wallet);

    WalletBatch batch(*database, "r+");

    std::set<uint256> todo;
    std::set<uint256> done;

    // Can't mark abandoned if confirmed or in mempool
    auto it = mapWallet.find(hashTx);
    assert(it != mapWallet.end());
    CWalletTx& origtx = it->second;
    if (origtx.GetDepthInMainChain() != 0 || origtx.InMempool()) {
        return false;
    }

    todo.insert(hashTx);

    while (!todo.empty()) {
        uint256 now = *todo.begin();
        todo.erase(now);
        done.insert(now);
        auto it = mapWallet.find(now);
        assert(it != mapWallet.end());
        CWalletTx& wtx = it->second;
        int currentconfirm = wtx.GetDepthInMainChain();
        // If the orig tx was not in block, none of its spends can be
        assert(currentconfirm <= 0);
        // if (currentconfirm < 0) {Tx and spends are already conflicted, no need to abandon}
        if (currentconfirm == 0 && !wtx.isAbandoned()) {
            // If the orig tx was not in block/mempool, none of its spends can be in mempool
            assert(!wtx.InMempool());
            wtx.nIndex = -1;
            wtx.setAbandoned();
            wtx.MarkDirty();
            batch.WriteTx(wtx);
            NotifyTransactionChanged(this, wtx.GetHash(), CT_UPDATED);
            // Iterate over all its outputs, and mark transactions in the wallet that spend them abandoned too
            TxSpends::const_iterator iter = mapTxSpends.lower_bound(COutPoint(now, 0));
            while (iter != mapTxSpends.end() && iter->first.hash == now) {
                if (!done.count(iter->second)) {
                    todo.insert(iter->second);
                }
                iter++;
            }
            // If a transaction changes 'conflicted' state, that changes the balance
            // available of the outputs it spends. So force those to be recomputed
            MarkInputsDirty(wtx.tx);
        }
    }
    LogPrintf("%s: Abandoned transaction %s\n", __func__, hashTx.GetHex());
    return true;
}

void CWallet::MarkConflicted(const uint256& hashBlock, const uint256& hashTx)
{
    LOCK2(cs_main, cs_wallet);

    int conflictconfirms = 0;
    CBlockIndex* pindex = LookupBlockIndex(hashBlock);
    if (pindex && chainActive.Contains(pindex)) {
        conflictconfirms = -(chainActive.Height() - pindex->nHeight + 1);
    }
    // If number of conflict confirms cannot be determined, this means
    // that the block is still unknown or not yet part of the main chain,
    // for example when loading the wallet during a reindex. Do nothing in that
    // case.
    if (conflictconfirms >= 0)
        return;

    // Do not flush the wallet here for performance reasons
    WalletBatch batch(*database, "r+", false);

    std::set<uint256> todo;
    std::set<uint256> done;

    todo.insert(hashTx);

    while (!todo.empty()) {
        uint256 now = *todo.begin();
        todo.erase(now);
        done.insert(now);
        auto it = mapWallet.find(now);
        assert(it != mapWallet.end());
        CWalletTx& wtx = it->second;
        int currentconfirm = wtx.GetDepthInMainChain();
        if (conflictconfirms < currentconfirm) {
            // Block is 'more conflicted' than current confirm; update.
            // Mark transaction as conflicted with this block.
            wtx.nIndex = -1;
            wtx.hashBlock = hashBlock;
            wtx.MarkDirty();
            batch.WriteTx(wtx);
            // Iterate over all its outputs, and mark transactions in the wallet that spend them conflicted too
            TxSpends::const_iterator iter = mapTxSpends.lower_bound(COutPoint(now, 0));
            while (iter != mapTxSpends.end() && iter->first.hash == now) {
                 if (!done.count(iter->second)) {
                     todo.insert(iter->second);
                 }
                 iter++;
            }
            // If a transaction changes 'conflicted' state, that changes the balance
            // available of the outputs it spends. So force those to be recomputed
            MarkInputsDirty(wtx.tx);
        }
    }
}

void CWallet::SyncTransaction(const CTransactionRef& ptx, const CBlockIndex *pindex, int posInBlock, bool update_tx) {
    if (!AddToWalletIfInvolvingMe(ptx, pindex, posInBlock, update_tx)) {
        return; // Not one of ours
    }

    // If a transaction changes 'conflicted' state, that changes the balance
    // available of the outputs it spends. So force those to be
    // recomputed, also:
    MarkInputsDirty(ptx);
}

void CWallet::TransactionAddedToMempool(const CTransactionRef& ptx) {
    LOCK2(cs_main, cs_wallet);
    SyncTransaction(ptx);

    auto it = mapWallet.find(ptx->GetHash());
    if (it != mapWallet.end()) {
        it->second.fInMempool = true;
    }
}

void CWallet::TransactionRemovedFromMempool(const CTransactionRef &ptx) {
    LOCK(cs_wallet);
    auto it = mapWallet.find(ptx->GetHash());
    if (it != mapWallet.end()) {
        it->second.fInMempool = false;
    }
}

void CWallet::BlockConnected(const std::shared_ptr<const CBlock>& pblock, const CBlockIndex *pindex, const std::vector<CTransactionRef>& vtxConflicted) {
    LOCK2(cs_main, cs_wallet);
    // TODO: Temporarily ensure that mempool removals are notified before
    // connected transactions.  This shouldn't matter, but the abandoned
    // state of transactions in our wallet is currently cleared when we
    // receive another notification and there is a race condition where
    // notification of a connected conflict might cause an outside process
    // to abandon a transaction and then have it inadvertently cleared by
    // the notification that the conflicted transaction was evicted.

    for (const CTransactionRef& ptx : vtxConflicted) {
        SyncTransaction(ptx);
        TransactionRemovedFromMempool(ptx);
    }
    for (size_t i = 0; i < pblock->vtx.size(); i++) {
        SyncTransaction(pblock->vtx[i], pindex, i);
        TransactionRemovedFromMempool(pblock->vtx[i]);
    }

    m_last_block_processed = pindex;
}

void CWallet::BlockDisconnected(const std::shared_ptr<const CBlock>& pblock) {
    LOCK2(cs_main, cs_wallet);

    for (const CTransactionRef& ptx : pblock->vtx) {
        SyncTransaction(ptx);

        if (ptx->IsZerocoinSpend()) {
            std::set<uint256> setSerialHashes;
            TxToSerialHashSet(ptx.get(), setSerialHashes);

            for (const uint256& hashSerial : setSerialHashes) {
                if (!IsMyZerocoinSpend(hashSerial))
                    continue;

                CMintMeta mint = zTracker->Get(hashSerial);
                zTracker->SetPubcoinNotUsed(mint.hashPubcoin);
            }
        }

        /* todo - More efficient to update mints here? Needed for disconnect?
        if (ptx->IsZerocoinMint()) {
            std::set<uint256> setPubcoinHashes;
            TxToPubcoinHashSet(ptx.get(), setPubcoinHashes);

            for (const uint256& hashPubcoin : setPubcoinHashes) {
                zTracker->
            }
        }
         */
    }
}



void CWallet::BlockUntilSyncedToCurrentChain() {
    AssertLockNotHeld(cs_main);
    AssertLockNotHeld(cs_wallet);

    {
        // Skip the queue-draining stuff if we know we're caught up with
        // chainActive.Tip()...
        // We could also take cs_wallet here, and call m_last_block_processed
        // protected by cs_wallet instead of cs_main, but as long as we need
        // cs_main here anyway, it's easier to just call it cs_main-protected.
        LOCK(cs_main);
        const CBlockIndex* initialChainTip = chainActive.Tip();

        if (m_last_block_processed && m_last_block_processed->GetAncestor(initialChainTip->nHeight) == initialChainTip) {
            return;
        }
    }

    // ...otherwise put a callback in the validation interface queue and wait
    // for the queue to drain enough to execute it (indicating we are caught up
    // at least with the time we entered this function).
    SyncWithValidationInterfaceQueue();
}

isminetype CWallet::IsMine(const CTxDestination& dest) const
{
    if (dest.type() == typeid(CStealthAddress))
        return pAnonWalletMain->HaveAddress(dest);

    return ::IsMine(*this, dest);
}

isminetype CWallet::IsMine(const CTxIn &txin, bool fCheckZerocoin, bool fCheckAnon) const
{
    {
        LOCK(cs_wallet);
        if (fCheckZerocoin && txin.IsZerocoinSpend()) {
            auto spend = TxInToZerocoinSpend(txin);
            if (!spend)
                return ISMINE_NO;
            if (IsMyZerocoinSpend(spend->getCoinSerialNumber()))
                return ISMINE_SPENDABLE;
        }

        if (fCheckAnon) {
            auto mine_anon = pAnonWalletMain->IsMine(txin);
            if (mine_anon) return mine_anon;
        }

        std::map<uint256, CWalletTx>::const_iterator mi = mapWallet.find(txin.prevout.hash);
        if (mi != mapWallet.end())
        {
            const CWalletTx& prev = (*mi).second;
            if (txin.prevout.n < prev.tx->vpout.size())
                return IsMine(prev.tx->vpout[txin.prevout.n].get());
        }

    }
    return ISMINE_NO;
}

// Note that this function doesn't distinguish between a 0-valued input,
// and a not-"is mine" (according to the filter) input.
CAmount CWallet::GetDebit(const CTxIn &txin, const isminefilter& filter) const
{
    {
        LOCK(cs_wallet);
        std::map<uint256, CWalletTx>::const_iterator mi = mapWallet.find(txin.prevout.hash);
        if (mi != mapWallet.end())
        {
            const CWalletTx& prev = (*mi).second;
            if (txin.prevout.n < prev.tx->vpout.size())
                if (IsMine(prev.tx->vpout[txin.prevout.n].get()) & filter)
                    return prev.tx->vpout[txin.prevout.n]->GetValue();
        }
    }
    return 0;
}

isminetype CWallet::IsMine(const CTxOut& txout) const
{
    return ::IsMine(*this, txout.scriptPubKey);
}

isminetype CWallet::IsMine(const CTxOutBase *txout) const
{
    switch (txout->nVersion)
    {
        case OUTPUT_STANDARD:
        {
            if (txout->IsZerocoinMint()) {
                if (IsMyMint(txout))
                    return ISMINE_SPENDABLE;
                else
                    return ISMINE_NO;
            }

            CTxOut out;
            txout->GetTxOut(out);
            return IsMine(out);
        }
        case OUTPUT_CT:
        case OUTPUT_RINGCT:
        case OUTPUT_DATA:
        {
            return pAnonWalletMain->IsMine(txout);
        }
        default:
            return ISMINE_NO;
    }
}

CAmount CWallet::GetCredit(const CTxOutBase *txout, const isminefilter &filter) const
{
    switch (txout->nVersion)
    {
        case OUTPUT_STANDARD:
        {
            CTxOut out;
            txout->GetTxOut(out);
            return GetCredit(out, filter);
        }
        case OUTPUT_CT:
        case OUTPUT_RINGCT:
        case OUTPUT_DATA:
        {
            //todo: this does not work
            //return pAnonWalletMain->GetCredit(txout, filter);
            return 0;
        }
        default:
            return 0;
    }
    return 0;
}

CAmount CWallet::GetCredit(const CTxOut& txout, const isminefilter& filter) const
{
    if (!MoneyRange(txout.nValue))
        throw std::runtime_error(std::string(__func__) + ": value out of range");
    return ((IsMine(txout) & filter) ? txout.nValue : 0);
}

CAmount CWallet::GetAnonCredit(const COutPoint& outpoint, const isminefilter& filter) const
{
    return pAnonWalletMain->GetCredit(outpoint, filter);
}

bool CWallet::IsChange(const CTxOutBase *txout) const
{
    switch(txout->GetType())
    {
        case OUTPUT_STANDARD:
        {
            CTxOut out;
            if (!txout->GetTxOut(out))
                return false;
            return IsChange(out);
        }
        case OUTPUT_CT:
        case OUTPUT_RINGCT:
        case OUTPUT_DATA:
        {
            return pAnonWalletMain->IsChange(txout);
        }
        default:
            return 0;
    }
}

bool CWallet::IsChange(const CTxOut& txout) const
{
    // TODO: fix handling of 'change' outputs. The assumption is that any
    // payment to a script that is ours, but is not in the address book
    // is change. That assumption is likely to break when we implement multisignature
    // wallets that return change back into a multi-signature-protected address;
    // a better way of identifying which outputs are 'the send' and which are
    // 'the change' will need to be implemented (maybe extend CWalletTx to remember
    // which output, if any, was change).
    if (::IsMine(*this, txout.scriptPubKey))
    {
        CTxDestination address;
        if (!ExtractDestination(txout.scriptPubKey, address))
            return true;

        LOCK(cs_wallet);
        if (!mapAddressBook.count(address))
            return true;
    }
    return false;
}

CAmount CWallet::GetChange(const CTxOut& txout) const
{
    if (!MoneyRange(txout.nValue))
        throw std::runtime_error(std::string(__func__) + ": value out of range");
    return (IsChange(txout) ? txout.nValue : 0);
}

bool CWallet::IsMine(const CTransaction& tx) const
{
    for (const CTxOutBaseRef& pOut : tx.vpout) {
        auto type = pOut->GetType();
        if (type == OUTPUT_STANDARD || type == OUTPUT_CT) {
            //Standard output and output ct both use normal pubkey
            if (::IsMine(*this, *pOut->GetPScriptPubKey()))
                return true;
        } else if (type == OUTPUT_RINGCT || type == OUTPUT_DATA ) {
            if (pAnonWalletMain->IsMine(pOut.get()))
                return true;
        }
    }

    return false;
}

bool CWallet::IsFromMe(const CTransaction& tx) const
{
    return (GetDebit(tx, ISMINE_ALL) > 0);
}

CAmount CWallet::GetDebit(const CTransaction& tx, const isminefilter& filter) const
{
    CAmount nDebit = 0;
    for (const CTxIn& txin : tx.vin)
    {
        nDebit += GetDebit(txin, filter);
        if (!MoneyRange(nDebit))
            throw std::runtime_error(std::string(__func__) + ": value out of range");
    }
    return nDebit;
}

bool CWallet::IsAllFromMe(const CTransaction& tx, const isminefilter& filter) const
{
    LOCK(cs_wallet);

    for (const CTxIn& txin : tx.vin)
    {
        auto mi = mapWallet.find(txin.prevout.hash);
        if (mi == mapWallet.end())
            return false; // any unknown inputs can't be from us

        const CWalletTx& prev = (*mi).second;

        if (txin.prevout.n >= prev.tx->vpout.size())
            return false; // invalid input!

        if (!(IsMine(prev.tx->vpout[txin.prevout.n].get()) & filter))
            return false;
    }
    return true;
}

CAmount CWallet::GetCredit(const CTransaction& tx, const isminefilter& filter) const
{
    CAmount nCredit = 0;
    for (const auto& pOut : tx.vpout)
    {
        //todo: currently GetCredit(CTxOutBase, filter) only returns basecoin, so has to call on whole tx below
        nCredit += GetCredit(pOut.get(), filter);
        if (!MoneyRange(nCredit))
            throw std::runtime_error(std::string(__func__) + ": value out of range");
    }
    if (tx.HasBlindedValues()) {
        auto nCreditAnon = pAnonWalletMain->GetCredit(tx, filter);
        nCredit += nCreditAnon;
    }

    return nCredit;
}

CAmount CWallet::GetChange(const CTransaction& tx) const
{
    CAmount nChange = 0;
    for (const auto& pOut : tx.vpout)
    {
        if (!pOut->IsStandardOutput())
            continue;
        nChange += GetChange(pOut->GetStandardOutput()->ToTxOut());
        if (!MoneyRange(nChange))
            throw std::runtime_error(std::string(__func__) + ": value out of range");
    }
    return nChange;
}

bool CWallet::SetHDSeedFromMnemonic(const std::string &mnemonic, uint512& seed)
{
    assert(!IsWalletFlagSet(WALLET_FLAG_DISABLE_PRIVATE_KEYS));
    auto blob_512 = decode_mnemonic(mnemonic);
    memcpy(seed.begin(), blob_512.begin(), blob_512.size());
    SetHDSeed_512(seed);
    return true;
}

bool CWallet::IsMyMint(const CBigNum& bnValue) const
{
    if (zTracker->HasPubcoin(bnValue))
        return true;

    return zwalletMain->IsInMintPool(bnValue);
}

bool CWallet::IsMyMint(const CTxOutBase* pout) const
{
    libzerocoin::PublicCoin pubcoin(Params().Zerocoin_Params());
    if (!OutputToPublicCoin(pout, pubcoin)) {
        return false;
    }

    return IsMyMint(pubcoin.getValue());
}

bool CWallet::IsMyZerocoinSpend(const CBigNum& bnSerial) const
{
    return zTracker->HasSerial(bnSerial);
}

bool CWallet::IsMyZerocoinSpend(const uint256& hashSerial) const
{
    return zTracker->HasSerialHash(hashSerial);
}

bool CWallet::UpdateMint(const CBigNum& bnValue, const int& nHeight, const uint256& txid, const libzerocoin::CoinDenomination& denom)
{
    uint256 hashValue = GetPubCoinHash(bnValue);
    CZerocoinMint mint;
    if (zTracker->HasPubcoinHash(hashValue)) {
        CMintMeta meta = zTracker->GetMetaFromPubcoin(hashValue);
        meta.nHeight = nHeight;
        meta.txid = txid;
        return zTracker->UpdateState(meta);
    } else {
        //Check if this mint is one that is in our mintpool (a potential future mint from our deterministic generation)
        if (zwalletMain->IsInMintPool(bnValue)) {
            if (zwalletMain->SetMintSeen(bnValue, nHeight, txid, denom))
                return true;
        }
    }

    return false;
}

void CWallet::UpdateZerocoinState(const CMintMeta& meta)
{
    zTracker->UpdateState(meta);
}

void CWallet::SetSerialSpent(const uint256& bnSerial, const uint256& txid)
{
    auto mint = zTracker->Get(bnSerial);
    zTracker->SetPubcoinUsed(mint.hashPubcoin, txid);
}

void CWallet::ArchiveZerocoin(CMintMeta& meta)
{
    zTracker->Archive(meta);
}

CPubKey CWallet::GenerateNewSeed()
{
    assert(!IsWalletFlagSet(WALLET_FLAG_DISABLE_PRIVATE_KEYS));
    CKey key;
    key.MakeNewKey(true);
    return DeriveNewSeed(key);
}

CPubKey CWallet::DeriveNewSeed(const CKey& key)
{
    int64_t nCreationTime = GetTime();
    CKeyMetadata metadata(nCreationTime);

    // calculate the seed
    CPubKey seed = key.GetPubKey();
    assert(key.VerifyPubKey(seed));

    // set the hd keypath to "s" -> Seed, refers the seed to itself
    metadata.hdKeypath     = "s";
    metadata.hd_seed_id = seed.GetID();

    {
        LOCK(cs_wallet);

        // mem store the metadata
        mapKeyMetadata[seed.GetID()] = metadata;

        // write the key&metadata to the database
        if (!AddKeyPubKey(key, seed))
            throw std::runtime_error(std::string(__func__) + ": AddKeyPubKey failed");
    }

    return seed;
}

void CWallet::SetHDSeed_512(const uint512& hashSeed)
{
    LOCK(cs_wallet);
    CHDChain newHdChain;
    newHdChain.nVersion = CHDChain::VERSION_HD_CHAIN_SPLIT;

    // Encapsulate the 512 seed data into two keys. This allows for databasing the keys without modifying the encryption and databasing scheme
    CKey key1;
    key1.Set(hashSeed.begin(), hashSeed.begin()+32, false);
    CKey key2;
    key2.Set(hashSeed.begin()+32, hashSeed.begin()+64, false);

    // Add seeds to the keystore
    if (!mapKeyMetadata.count(key1.GetPubKey().GetID())) {
        DeriveNewSeed(key1);
        DeriveNewSeed(key2);
    }

    newHdChain.seed_id = key1.GetPubKey().GetID();
    newHdChain.seed_id_r = key2.GetPubKey().GetID();
    SetHDChain(newHdChain, false);
}

void CWallet::SetHDSeed(const CPubKey& seed)
{
    LOCK(cs_wallet);
    // store the keyid (hash160) together with
    // the child index counter in the database
    // as a hdchain object
    CHDChain newHdChain;
    newHdChain.nVersion = CanSupportFeature(FEATURE_HD_SPLIT) ? CHDChain::VERSION_HD_CHAIN_SPLIT : CHDChain::VERSION_HD_BASE;
    newHdChain.seed_id = seed.GetID();
    SetHDChain(newHdChain, false);
}

void CWallet::SetHDChain(const CHDChain& chain, bool memonly)
{
    LOCK(cs_wallet);
    if (!memonly && !WalletBatch(*database).WriteHDChain(chain))
        throw std::runtime_error(std::string(__func__) + ": writing chain failed");

    hdChain = chain;
}

bool CWallet::IsHDEnabled() const
{
    return !hdChain.seed_id.IsNull();
}

void CWallet::SetWalletFlag(uint64_t flags)
{
    LOCK(cs_wallet);
    m_wallet_flags |= flags;
    if (!WalletBatch(*database).WriteWalletFlags(m_wallet_flags))
        throw std::runtime_error(std::string(__func__) + ": writing wallet flags failed");
}

bool CWallet::IsWalletFlagSet(uint64_t flag)
{
    return (m_wallet_flags & flag);
}

bool CWallet::SetWalletFlags(uint64_t overwriteFlags, bool memonly)
{
    LOCK(cs_wallet);
    m_wallet_flags = overwriteFlags;
    if (((overwriteFlags & g_known_wallet_flags) >> 32) ^ (overwriteFlags >> 32)) {
        // contains unknown non-tolerable wallet flags
        return false;
    }
    if (!memonly && !WalletBatch(*database).WriteWalletFlags(m_wallet_flags)) {
        throw std::runtime_error(std::string(__func__) + ": writing wallet flags failed");
    }

    return true;
}

int64_t CWalletTx::GetTxTime() const
{
    int64_t n = nTimeSmart;
    return n ? n : nTimeReceived;
}

// Helper for producing a max-sized low-S low-R signature (eg 71 bytes)
// or a max-sized low-S signature (e.g. 72 bytes) if use_max_sig is true
bool CWallet::DummySignInput(CTxIn &tx_in, const CTxOut &txout, bool use_max_sig) const
{
    // Fill in dummy signatures for fee calculation.
    const CScript& scriptPubKey = txout.scriptPubKey;
    SignatureData sigdata;

    if (!ProduceSignature(*this, use_max_sig ? DUMMY_MAXIMUM_SIGNATURE_CREATOR : DUMMY_SIGNATURE_CREATOR, scriptPubKey, sigdata)) {
        return false;
    }
    UpdateInput(tx_in, sigdata);
    return true;
}

// Helper for producing a bunch of max-sized low-S low-R signatures (eg 71 bytes)
bool CWallet::DummySignTx(CMutableTransaction &txNew, const std::vector<CTxOut> &txouts, bool use_max_sig) const
{
    // Fill in dummy signatures for fee calculation.
    int nIn = 0;
    for (const auto& txout : txouts)
    {
        if (!DummySignInput(txNew.vin[nIn], txout, use_max_sig)) {
            return false;
        }

        nIn++;
    }
    return true;
}

int64_t CalculateMaximumSignedTxSize(const CTransaction &tx, const CWallet *wallet, bool use_max_sig)
{
    std::vector<CTxOut> txouts;
    // Look up the inputs.  We should have already checked that this transaction
    // IsAllFromMe(ISMINE_SPENDABLE), so every input should already be in our
    // wallet, with a valid index into the vout array, and the ability to sign.
    for (auto& input : tx.vin) {
        const auto mi = wallet->mapWallet.find(input.prevout.hash);
        if (mi == wallet->mapWallet.end()) {
            return -1;
        }
        assert(input.prevout.n < mi->second.tx->vpout.size());
        txouts.emplace_back(mi->second.tx->vpout[input.prevout.n]->GetStandardOutput()->ToTxOut());
    }
    return CalculateMaximumSignedTxSize(tx, wallet, txouts, use_max_sig);
}

// txouts needs to be in the order of tx.vin
int64_t CalculateMaximumSignedTxSize(const CTransaction &tx, const CWallet *wallet, const std::vector<CTxOut>& txouts, bool use_max_sig)
{
    CMutableTransaction txNew(tx);
    if (!wallet->DummySignTx(txNew, txouts, use_max_sig)) {
        // This should never happen, because IsAllFromMe(ISMINE_SPENDABLE)
        // implies that we can sign for every input.
        return -1;
    }
    return GetVirtualTransactionSize(txNew);
}

int CalculateMaximumSignedInputSize(const CTxOut& txout, const CWallet* wallet, bool use_max_sig)
{
    CMutableTransaction txn;
    txn.vin.push_back(CTxIn(COutPoint()));
    if (!wallet->DummySignInput(txn.vin[0], txout, use_max_sig)) {
        // This should never happen, because IsAllFromMe(ISMINE_SPENDABLE)
        // implies that we can sign for every input.
        return -1;
    }
    return GetVirtualTransactionInputSize(txn.vin[0]);
}

void CWalletTx::GetAmounts(std::list<COutputEntry>& listReceived,
                           std::list<COutputEntry>& listSent, CAmount& nFee, const isminefilter& filter) const
{
    nFee = 0;
    listReceived.clear();
    listSent.clear();

    // Compute fee:
    CAmount nDebit = GetDebit(filter);
    if (nDebit > 0) // debit>0 means we signed/sent this transaction
    {
        CAmount nValueOut = tx->GetValueOut();
        nFee = nDebit - nValueOut;
    }

    // Sent/received.
    auto hashTx = tx->GetHash();
    for (unsigned int i = 0; i < tx->vpout.size(); ++i)
    {
        auto pout = tx->vpout[i];
        isminetype fIsMine = pwallet->IsMine(pout.get());

        CTxOut txout;
        if (!pout->GetTxOut(txout)) {
            if (pout->GetType() == OUTPUT_DATA)
                continue;

            //Anon output
            const AnonWallet* pwalletAnon = pwallet->GetAnonWallet_const();
            COutputRecord record;
            if (pwalletAnon->GetOutputRecord(COutPoint(hashTx, i), record)) {
                CTxDestination dest;
                record.GetDestination(dest);
                COutputEntry output = {dest, record.GetAmount(), (int)i};
                if (record.IsSend())
                    listSent.emplace_back(output);
                else
                    listReceived.emplace_back(output);
            }
            continue;
        }

        // Only need to handle txouts if AT LEAST one of these is true:
        //   1) they debit from us (sent)
        //   2) the output is to us (received)
        if (nDebit > 0)
        {
            // Don't report 'change' txouts
            if (pwallet->IsChange(txout))
                continue;
        }
        else if (!(fIsMine & filter))
            continue;

        // In either case, we need to get the destination address
        CTxDestination address;

        if (!ExtractDestination(txout.scriptPubKey, address) && !txout.scriptPubKey.IsUnspendable())
        {
            pwallet->WalletLogPrintf("CWalletTx::GetAmounts: Unknown transaction type found, txid %s\n",
                                    this->GetHash().ToString());
            address = CNoDestination();
        }

        COutputEntry output = {address, txout.nValue, (int)i};

        // If we are debited by the transaction, add the output as a "sent" entry
        if (nDebit > 0)
            listSent.push_back(output);

        // If we are receiving the output, add it as a "received" entry
        if (fIsMine & filter)
            listReceived.push_back(output);
    }

}

/**
 * Scan active chain for relevant transactions after importing keys. This should
 * be called whenever new keys are added to the wallet, with the oldest key
 * creation time.
 *
 * @return Earliest timestamp that could be successfully scanned from. Timestamp
 * returned will be higher than startTime if relevant blocks could not be read.
 */
int64_t CWallet::RescanFromTime(int64_t startTime, const WalletRescanReserver& reserver, bool update)
{
    // Find starting block. May be null if nCreateTime is greater than the
    // highest blockchain timestamp, in which case there is nothing that needs
    // to be scanned.
    CBlockIndex* startBlock = nullptr;
    {
        LOCK(cs_main);
        startBlock = chainActive.FindEarliestAtLeast(startTime - TIMESTAMP_WINDOW);
        WalletLogPrintf("%s: Rescanning last %i blocks\n", __func__, startBlock ? chainActive.Height() - startBlock->nHeight + 1 : 0);
    }

    if (startBlock) {
        const CBlockIndex* const failedBlock = ScanForWalletTransactions(startBlock, nullptr, reserver, update);
        if (failedBlock) {
            return failedBlock->GetBlockTimeMax() + TIMESTAMP_WINDOW + 1;
        }
    }
    return startTime;
}

/**
 * Scan the block chain (starting in pindexStart) for transactions
 * from or to us. If fUpdate is true, found transactions that already
 * exist in the wallet will be updated.
 *
 * Returns null if scan was successful. Otherwise, if a complete rescan was not
 * possible (due to pruning or corruption), returns pointer to the most recent
 * block that could not be scanned.
 *
 * If pindexStop is not a nullptr, the scan will stop at the block-index
 * defined by pindexStop
 *
 * Caller needs to make sure pindexStop (and the optional pindexStart) are on
 * the main chain after to the addition of any new keys you want to detect
 * transactions for.
 */
CBlockIndex* CWallet::ScanForWalletTransactions(CBlockIndex* pindexStart, CBlockIndex* pindexStop, const WalletRescanReserver &reserver, bool fUpdate)
{
    int64_t nNow = GetTime();
    const CChainParams& chainParams = Params();
    zTracker->Init();

    assert(reserver.isReserved());
    if (pindexStop) {
        assert(pindexStop->nHeight >= pindexStart->nHeight);
    }

    CBlockIndex* pindex = pindexStart;
    CBlockIndex* ret = nullptr;

    if (pindex) WalletLogPrintf("Rescan started from block %d...\n", pindex->nHeight);

    {
        fAbortRescan = false;
        uiInterface.ShowProgress(strprintf("%s " + _("Rescanning..."), GetDisplayName()), 0, false); // show rescan progress in GUI as dialog or on splashscreen, if -rescan on startup

        CBlockIndex* tip = nullptr;
        double progress_begin;
        double progress_end;
        {
            LOCK(cs_main);
            progress_begin = GuessVerificationProgress(chainParams.TxData(), pindex);
            if (pindexStop == nullptr) {
                tip = chainActive.Tip();
                progress_end = GuessVerificationProgress(chainParams.TxData(), tip);
            } else {
                progress_end = GuessVerificationProgress(chainParams.TxData(), pindexStop);
            }
        }
        double progress_current = progress_begin;
        while (pindex && !fAbortRescan && !ShutdownRequested())
        {
            int percentageDone = std::max(1, std::min(99, (int)((progress_current - progress_begin) / (progress_end - progress_begin) * 100)));
            if (pindex->nHeight % 100 == 0 && progress_end - progress_begin > 0.0) {
                uiInterface.ShowProgress(strprintf("%s " + _("Rescanning..."), GetDisplayName()), percentageDone, 0);
            }
            if (GetTime() >= nNow + 60) {
                nNow = GetTime();
                WalletLogPrintf("Still rescanning. At block %d. Progress=%f\n", pindex->nHeight, progress_current);
            }

            CBlock block;
            if (ReadBlockFromDisk(block, pindex, Params().GetConsensus())) {
                LOCK2(cs_main, cs_wallet);
                if (pindex && !chainActive.Contains(pindex)) {
                    // Abort scan if current block is no longer active, to prevent
                    // marking transactions as coming from the wrong block.
                    ret = pindex;
                    break;
                }
                for (size_t posInBlock = 0; posInBlock < block.vtx.size(); ++posInBlock) {
                    if (block.vtx[posInBlock]->IsZerocoinSpend()) {
                        uint256 txid = block.vtx[posInBlock]->GetHash();
                        std::map<libzerocoin::CoinSpend, uint256> spendInfo;
                        for (auto& in : block.vtx[posInBlock]->vin) {
                            if (!in.IsZerocoinSpend())
                                continue;

                            auto spend = TxInToZerocoinSpend(in);
                            if (spend)
                                spendInfo.emplace(*spend, txid);
                            else
                                error("%s: Failed to getspend *********************************\n");
                        }
                        pzerocoinDB->WriteCoinSpendBatch(spendInfo);
                    }
                    SyncTransaction(block.vtx[posInBlock], pindex, posInBlock, fUpdate);
                }
            } else {
                ret = pindex;
            }
            if (pindex == pindexStop) {
                break;
            }
            {
                LOCK(cs_main);
                pindex = chainActive.Next(pindex);
                progress_current = GuessVerificationProgress(chainParams.TxData(), pindex);
                if (pindexStop == nullptr && tip != chainActive.Tip()) {
                    tip = chainActive.Tip();
                    // in case the tip has changed, update progress max
                    progress_end = GuessVerificationProgress(chainParams.TxData(), tip);
                }
            }
        }
        if (pindex && fAbortRescan) {
            WalletLogPrintf("Rescan aborted at block %d. Progress=%f\n", pindex->nHeight, progress_current);
        } else if (pindex && ShutdownRequested()) {
            WalletLogPrintf("Rescan interrupted by shutdown request at block %d. Progress=%f\n", pindex->nHeight, progress_current);
        }
        uiInterface.ShowProgress(strprintf("%s " + _("Rescanning..."), GetDisplayName()), 100, 0); // hide progress dialog in GUI
    }
    return ret;
}

void CWallet::ReacceptWalletTransactions()
{
    // If transactions aren't being broadcasted, don't let them into local mempool either
    if (!fBroadcastTransactions)
        return;
    LOCK2(cs_main, cs_wallet);
    std::map<int64_t, CWalletTx*> mapSorted;

    // Sort pending wallet transactions based on their initial wallet insertion order
    for (std::pair<const uint256, CWalletTx>& item : mapWallet)
    {
        const uint256& wtxid = item.first;
        CWalletTx& wtx = item.second;
        assert(wtx.GetHash() == wtxid);

        int nDepth = wtx.GetDepthInMainChain();

        if (!wtx.IsCoinBase() && (nDepth == 0 && !wtx.isAbandoned()) && !wtx.IsCoinStake()) {
            mapSorted.insert(std::make_pair(wtx.nOrderPos, &wtx));
        }
    }

    // Try to add wallet transactions to memory pool
    for (std::pair<const int64_t, CWalletTx*>& item : mapSorted) {
        CWalletTx& wtx = *(item.second);
        CValidationState state;
        if (!wtx.AcceptToMemoryPool(maxTxFee, state)) {
            //If this is an old tx that is no longer valid, just abandon
            if (GetTime() - wtx.GetTxTime() > 60*60) {
                AbandonTransaction(wtx.GetHash());
            }
        }
    }
}

bool CWalletTx::RelayWalletTransaction(CConnman* connman)
{
    assert(pwallet->GetBroadcastTransactions());
    if (!IsCoinBase() && !IsCoinStake() && !isAbandoned() && GetDepthInMainChain() == 0)
    {
        CValidationState state;
        /* GetDepthInMainChain already catches known conflicts. */
        if (InMempool() || AcceptToMemoryPool(maxTxFee, state)) {
            pwallet->WalletLogPrintf("Relaying wtx %s\n", GetHash().ToString());
            if (connman) {
                CInv inv(MSG_TX, GetHash());
                connman->ForEachNode([&inv](CNode* pnode)
                {
                    pnode->PushInventory(inv);
                });
                return true;
            }
        }
    }
    return false;
}

std::set<uint256> CWalletTx::GetConflicts() const
{
    std::set<uint256> result;
    if (pwallet != nullptr)
    {
        uint256 myHash = GetHash();
        result = pwallet->GetConflicts(myHash);
        result.erase(myHash);
    }
    return result;
}

CAmount CWalletTx::GetDebit(const isminefilter& filter) const
{
    if (tx->vin.empty())
        return 0;

    CAmount debit = 0;
    if(filter & ISMINE_SPENDABLE)
    {
        if (fDebitCached)
            debit += nDebitCached;
        else
        {
            nDebitCached = pwallet->GetDebit(*tx, ISMINE_SPENDABLE);
            fDebitCached = true;
            debit += nDebitCached;
        }
    }
    if(filter & ISMINE_WATCH_ONLY)
    {
        if(fWatchDebitCached)
            debit += nWatchDebitCached;
        else
        {
            nWatchDebitCached = pwallet->GetDebit(*tx, ISMINE_WATCH_ONLY);
            fWatchDebitCached = true;
            debit += nWatchDebitCached;
        }
    }
    return debit;
}

CAmount CWalletTx::GetCredit(const isminefilter& filter, bool fResetCache) const
{
    // Must wait until coinbase is safely deep enough in the chain before valuing it
    if (IsCoinBase() && GetBlocksToMaturity() > 0)
        return 0;

    CAmount credit = 0;
    if (filter & ISMINE_SPENDABLE)
    {
        // GetBalance can assume transactions in mapWallet won't change
        if (fCreditCached && !fResetCache) {
            credit += nCreditCached;
        }

        else
        {
            nCreditCached = pwallet->GetCredit(*tx, ISMINE_SPENDABLE);
            fCreditCached = true;
            credit += nCreditCached;
        }
    }
    if (filter & ISMINE_WATCH_ONLY)
    {
        if (fWatchCreditCached && !fResetCache)
            credit += nWatchCreditCached;
        else
        {
            nWatchCreditCached = pwallet->GetCredit(*tx, ISMINE_WATCH_ONLY);
            fWatchCreditCached = true;
            credit += nWatchCreditCached;
        }
    }
    return credit;
}

CAmount CWalletTx::GetImmatureCredit(bool fUseCache) const
{
    if (IsCoinBase() && GetBlocksToMaturity() > 0 && IsInMainChain())
    {
        if (fUseCache && fImmatureCreditCached)
            return nImmatureCreditCached;
        nImmatureCreditCached = pwallet->GetCredit(*tx, ISMINE_SPENDABLE);
        fImmatureCreditCached = true;
        return nImmatureCreditCached;
    }

    return 0;
}

CAmount CWalletTx::GetAvailableCredit(bool fUseCache, const isminefilter& filter, bool fBaseCoinOnly) const
{
    if (pwallet == nullptr)
        return 0;

    // Must wait until coinbase is safely deep enough in the chain before valuing it
    if (IsCoinBase() && GetBlocksToMaturity() > 0)
        return 0;

    CAmount* cache = nullptr;
    bool* cache_used = nullptr;

    if (filter == ISMINE_SPENDABLE) {
        cache = &nAvailableCreditCached;
        cache_used = &fAvailableCreditCached;
    } else if (filter == ISMINE_WATCH_ONLY) {
        cache = &nAvailableWatchCreditCached;
        cache_used = &fAvailableWatchCreditCached;
    }

    if (fUseCache && cache_used && *cache_used) {
        return *cache;
    }

    CAmount nCredit = 0;
    uint256 hashTx = GetHash();
    for (unsigned int i = 0; i < tx->GetNumVOuts(); i++) {
        if (!pwallet->IsSpent(hashTx, i)) {
            auto* pout = tx->vpout[i].get();
            if (pout->IsStandardOutput()) {
                CTxOut out;
                if (pout->GetTxOut(out))
                    nCredit += pwallet->GetCredit(out, filter);
            } else if (!fBaseCoinOnly) {
                nCredit += pwallet->GetAnonCredit(COutPoint(hashTx, i), filter);
            }
            if (!MoneyRange(nCredit))
                throw std::runtime_error(std::string(__func__) + " : value out of range");
        }
    }

    if (cache) {
        *cache = nCredit;
        assert(cache_used);
        *cache_used = true;
    }
    return nCredit;
}

CAmount CWalletTx::GetImmatureWatchOnlyCredit(const bool fUseCache) const
{
    if (IsCoinBase() && GetBlocksToMaturity() > 0 && IsInMainChain())
    {
        if (fUseCache && fImmatureWatchCreditCached)
            return nImmatureWatchCreditCached;
        nImmatureWatchCreditCached = pwallet->GetCredit(*tx, ISMINE_WATCH_ONLY);
        fImmatureWatchCreditCached = true;
        return nImmatureWatchCreditCached;
    }

    return 0;
}

CAmount CWalletTx::GetChange() const
{
    if (fChangeCached)
        return nChangeCached;
    nChangeCached = pwallet->GetChange(*tx);
    fChangeCached = true;
    return nChangeCached;
}

bool CWalletTx::InMempool() const
{
    return fInMempool;
}

bool CWalletTx::IsTrusted() const
{
    // Quick answer in most cases
    if (!CheckFinalTx(*tx))
        return false;
    int nDepth = GetDepthInMainChain();
    if (nDepth >= 1)
        return true;
    if (nDepth < 0)
        return false;
    if (!pwallet->m_spend_zero_conf_change || !IsFromMe(ISMINE_ALL)) // using wtx's cached debit
        return false;

    // Don't trust unconfirmed transactions from us unless they are in the mempool.
    if (!InMempool())
        return false;

    // Trusted if all inputs are from us and are in the mempool:
    for (const CTxIn& txin : tx->vin)
    {
        // Transactions not sent by us: not trusted
        const CWalletTx* parent = pwallet->GetWalletTx(txin.prevout.hash);
        if (parent == nullptr)
            return false;
        auto parentOut = parent->tx->vpout[txin.prevout.n];
        CTxOut out;
        if (!parentOut->GetTxOut(out)) {
            //todo - how to tell if nonstandard is ours?
            return false;
        }

        if (pwallet->IsMine(out) != ISMINE_SPENDABLE)
            return false;
    }
    return true;
}

bool CWalletTx::IsEquivalentTo(const CWalletTx& _tx) const
{
        CMutableTransaction tx1 {*this->tx};
        CMutableTransaction tx2 {*_tx.tx};
        for (auto& txin : tx1.vin) txin.scriptSig = CScript();
        for (auto& txin : tx2.vin) txin.scriptSig = CScript();
        return CTransaction(tx1) == CTransaction(tx2);
}

std::vector<uint256> CWallet::ResendWalletTransactionsBefore(int64_t nTime, CConnman* connman)
{
    std::vector<uint256> result;

    LOCK(cs_wallet);

    // Sort them in chronological order
    std::multimap<unsigned int, CWalletTx*> mapSorted;
    for (std::pair<const uint256, CWalletTx>& item : mapWallet)
    {
        CWalletTx& wtx = item.second;
        // Don't rebroadcast if newer than nTime:
        if (wtx.nTimeReceived > nTime)
            continue;
        mapSorted.insert(std::make_pair(wtx.nTimeReceived, &wtx));
    }
    for (std::pair<const unsigned int, CWalletTx*>& item : mapSorted)
    {
        CWalletTx& wtx = *item.second;
        if (wtx.RelayWalletTransaction(connman))
            result.push_back(wtx.GetHash());
    }
    return result;
}

void CWallet::ResendWalletTransactions(int64_t nBestBlockTime, CConnman* connman)
{
    // Do this infrequently and randomly to avoid giving away
    // that these are our transactions.
    if (GetTime() < nNextResend || !fBroadcastTransactions)
        return;
    bool fFirst = (nNextResend == 0);
    nNextResend = GetTime() + GetRand(30 * 60);
    if (fFirst)
        return;

    // Only do it if there's been a new block since last time
    if (nBestBlockTime < nLastResend)
        return;
    nLastResend = GetTime();

    // Rebroadcast unconfirmed txes older than 5 minutes before the last
    // block was found:
    std::vector<uint256> relayed = ResendWalletTransactionsBefore(nBestBlockTime-5*60, connman);
    if (!relayed.empty())
        WalletLogPrintf("%s: rebroadcast %u unconfirmed transactions\n", __func__, relayed.size());
}

/** @} */ // end of mapWallet




/** @defgroup Actions
 *
 * @{
 */


CAmount CWallet::GetBasecoinBalance(const isminefilter& filter, const int min_depth) const
{
    CAmount nTotal = 0;
    {
        LOCK2(cs_main, cs_wallet);
        for (const auto& entry : mapWallet)
        {
            const CWalletTx* pcoin = &entry.second;
            if (pcoin->IsTrusted() && pcoin->GetDepthInMainChain() >= min_depth) {
                nTotal += pcoin->GetAvailableCredit(true, filter, /*fBasecoinOnly*/true);
            }
        }
    }

    return nTotal;
}

CAmount CWallet::GetBalance(const isminefilter& filter, const int min_depth) const
{
    CAmount nTotal = 0;
    nTotal += GetBasecoinBalance(filter, min_depth);
    nTotal += GetZerocoinBalance(false, min_depth);
    nTotal += pAnonWalletMain->GetAnonBalance(min_depth);
    nTotal += pAnonWalletMain->GetBlindBalance(min_depth);
    return nTotal;
}

bool CWallet::GetBalances(BalanceList& bal)
{
    LOCK2(cs_main, cs_wallet);
    for (const auto &item : mapWallet) {
        const CWalletTx &wtx = item.second;
        bal.nVeilImmature += wtx.GetImmatureCredit();

        if (wtx.IsTrusted()) {
            bal.nVeil += wtx.GetAvailableCredit(true, ISMINE_SPENDABLE, true);
            bal.nVeilWatchOnly += wtx.GetAvailableCredit(true, ISMINE_WATCH_ONLY, true);
        } else {
            if (wtx.GetDepthInMainChain() == 0 && wtx.InMempool()) {
                bal.nVeilUnconf += wtx.GetAvailableCredit(true, ISMINE_SPENDABLE, true);
                bal.nVeilWatchOnlyUnconf += wtx.GetAvailableCredit(true, ISMINE_WATCH_ONLY, true);
            }
        }
    }

    pAnonWalletMain->GetBalances(bal);

    //todo: this creates more iteration than is needed calling three different times
    bal.nZerocoin = GetZerocoinBalance(true);
    bal.nZerocoinUnconf = GetUnconfirmedZerocoinBalance();
    bal.nZerocoinImmature = GetImmatureZerocoinBalance();

    return true;
}

// Get a Map pairing the Denominations with the amount of Zerocoin for each Denomination
std::pair<ZerocoinSpread, ZerocoinSpread> CWallet::GetMyZerocoinDistribution() const
{
    ZerocoinSpread spreadSpendable;
    spreadSpendable[libzerocoin::ZQ_TEN] = 0;
    spreadSpendable[libzerocoin::ZQ_ONE_HUNDRED] = 0;
    spreadSpendable[libzerocoin::ZQ_ONE_THOUSAND] = 0;
    spreadSpendable[libzerocoin::ZQ_TEN_THOUSAND] = 0;

    ZerocoinSpread spreadPending;
    spreadPending[libzerocoin::ZQ_TEN] = 0;
    spreadPending[libzerocoin::ZQ_ONE_HUNDRED] = 0;
    spreadPending[libzerocoin::ZQ_ONE_THOUSAND] = 0;
    spreadPending[libzerocoin::ZQ_TEN_THOUSAND] = 0;

    {
        std::set<CMintMeta> setMints = zTracker->ListMints(true, /*fMatureOnly*/false, true);
        for (const CMintMeta& mint : setMints) {
            if ((mint.nMemFlags & MINT_CONFIRMED) && (mint.nMemFlags & MINT_MATURE))
                spreadSpendable[mint.denom]++;
            else
                spreadPending[mint.denom]++;
        }
    }

    return std::make_pair(spreadSpendable, spreadPending);
}

bool CWallet::GetMintFromStakeHash(const uint256& hashStake, CZerocoinMint& mint)
{
    CMintMeta meta;
    if (!zTracker->GetMetaFromStakeHash(hashStake, meta))
        return error("%s: failed to find meta associated with hashStake", __func__);
    return GetMint(meta.hashSerial, mint);
}

bool CWallet::GetMint(const uint256& hashSerial, CZerocoinMint& mint)
{
    if (zwalletMain->HasEmptySeed())
        return error("%s: zerocoin wallet's seed is not loaded", __func__);

    if (!zTracker->HasSerialHash(hashSerial))
        return error("%s: serialhash %s is not in tracker", __func__, hashSerial.GetHex());

    WalletBatch walletdb(*database);
    CMintMeta meta = zTracker->Get(hashSerial);
    if (meta.isDeterministic) {
        CDeterministicMint dMint;
        if (!walletdb.ReadDeterministicMint(meta.hashPubcoin, dMint))
            return error("%s: failed to read deterministic mint", __func__);
        if (!zwalletMain->RegenerateMint(dMint, mint))
            return error("%s: failed to generate mint", __func__);

        return true;
    } else if (!walletdb.ReadZerocoinMint(meta.hashPubcoin, mint)) {
        return error("%s: failed to read zerocoinmint from database", __func__);
    }

    return true;
}

bool CWallet::GetMintMeta(const uint256& hashPubcoin, CMintMeta& meta) const
{
    if (!zTracker->HasPubcoinHash(hashPubcoin))
        return false;
    meta = zTracker->GetMetaFromPubcoin(hashPubcoin);
    return true;
}

std::set<CMintMeta> CWallet::ListMints(bool fUnusedOnly, bool fMatureOnly, bool fUpdateStatus)
{
    return zTracker->ListMints(fUnusedOnly, fMatureOnly, fUpdateStatus);
}

string CWallet::GetUniqueWalletBackupName(bool fzAuto) const
{
    stringstream ssDateTime;
    std::string strWalletBackupName = strprintf("%s", FormatISO8601DateTime(GetTime()));
    ssDateTime << strWalletBackupName;

    return strprintf("wallet%s.dat%s", fzAuto ? "-autozbackup" : "", FormatISO8601DateTime(GetTime()));
}

CAmount CWallet::GetUnconfirmedBasecoinBalance() const
{
    CAmount nTotal = 0;
    {
        LOCK2(cs_main, cs_wallet);
        for (const auto& entry : mapWallet)
        {
            const CWalletTx* pcoin = &entry.second;
            if (!pcoin->IsTrusted() && pcoin->GetDepthInMainChain() == 0 && pcoin->InMempool())
                nTotal += pcoin->GetAvailableCredit();
        }
    }
    return nTotal;
}

CAmount CWallet::GetUnconfirmedBalance() const
{
    CAmount nTotal = 0;
    nTotal += GetUnconfirmedBasecoinBalance();
    nTotal += GetUnconfirmedZerocoinBalance();
    BalanceList bal;
    pAnonWalletMain->GetBalances(bal);
    nTotal += bal.nCTUnconf;
    nTotal += bal.nRingCTUnconf;

    return nTotal;
}

CAmount CWallet::GetImmatureBasecoinBalance() const
{
    CAmount nTotal = 0;
    {
        LOCK2(cs_main, cs_wallet);
        for (const auto& entry : mapWallet)
        {
            const CWalletTx* pcoin = &entry.second;
            nTotal += pcoin->GetImmatureCredit();
        }
    }
    return nTotal;
}

CAmount CWallet::GetImmatureBalance() const
{
    CAmount nTotal = 0;
    nTotal += GetImmatureBasecoinBalance();
    nTotal += GetImmatureZerocoinBalance();
    BalanceList bal;
    pAnonWalletMain->GetBalances(bal);
    nTotal += bal.nCTImmature;
    nTotal += bal.nRingCTImmature;

    return nTotal;
}

CAmount CWallet::GetUnconfirmedWatchOnlyBalance() const
{
    CAmount nTotal = 0;
    {
        LOCK2(cs_main, cs_wallet);
        for (const auto& entry : mapWallet)
        {
            const CWalletTx* pcoin = &entry.second;
            if (!pcoin->IsTrusted() && pcoin->GetDepthInMainChain() == 0 && pcoin->InMempool())
                nTotal += pcoin->GetAvailableCredit(true, ISMINE_WATCH_ONLY);
        }
    }
    return nTotal;
}

CAmount CWallet::GetImmatureWatchOnlyBalance() const
{
    CAmount nTotal = 0;
    {
        LOCK2(cs_main, cs_wallet);
        for (const auto& entry : mapWallet)
        {
            const CWalletTx* pcoin = &entry.second;
            nTotal += pcoin->GetImmatureWatchOnlyCredit();
        }
    }
    return nTotal;
}

// Calculate total balance in a different way from GetBalance. The biggest
// difference is that GetBalance sums up all unspent TxOuts paying to the
// wallet, while this sums up both spent and unspent TxOuts paying to the
// wallet, and then subtracts the values of TxIns spending from the wallet. This
// also has fewer restrictions on which unconfirmed transactions are considered
// trusted.
CAmount CWallet::GetLegacyBalance(const isminefilter& filter, int minDepth) const
{
    LOCK2(cs_main, cs_wallet);

    CAmount balance = 0;
    for (const auto& entry : mapWallet) {
        const CWalletTx& wtx = entry.second;
        const int depth = wtx.GetDepthInMainChain();
        if (depth < 0 || !CheckFinalTx(*wtx.tx) || wtx.GetBlocksToMaturity() > 0) {
            continue;
        }

        // Loop through tx outputs and add incoming payments. For outgoing txs,
        // treat change outputs specially, as part of the amount debited.
        CAmount debit = wtx.GetDebit(filter);
        const bool outgoing = debit > 0;
        for (const auto& pOut : wtx.tx->vpout) {
            CTxOut out;
            if (!pOut->GetTxOut(out))
                continue;
            if (outgoing && IsChange(out)) {
                debit -= out.nValue;
            } else if (IsMine(out) & filter && depth >= minDepth) {
                balance += out.nValue;
            }
        }

        // For outgoing txs, subtract amount debited.
        if (outgoing) {
            balance -= debit;
        }
    }

    return balance;
}

CAmount CWallet::GetAvailableBalance(const CCoinControl* coinControl) const
{
    LOCK2(cs_main, cs_wallet);

    CAmount balance = 0;
    std::vector<COutput> vCoins;
    AvailableCoins(vCoins, true, coinControl);
    for (const COutput& out : vCoins) {
        if (out.fSpendable) {
            balance += out.tx->tx->vpout[out.i]->GetValue();
        }
    }
    return balance;
}

CAmount CWallet::GetMintableBalance(std::vector<COutput>& vMintableCoins) const
{
    LOCK2(cs_main, cs_wallet);

    vMintableCoins.clear();
    CAmount balance = 0;
    std::vector<COutput> vCoins;
    /*
     * (std::vector<COutput> &vCoins, bool fOnlySafe, const CCoinControl *coinControl, const CAmount &nMinimumAmount,
                             const CAmount &nMaximumAmount, const CAmount &nMinimumSumAmount, const uint64_t nMaximumCount, const int nMinDepth,
                             const int nMaxDepth, bool fIncludeImmature)
     */
    AvailableCoins(vCoins, /**fOnlySafe**/true, /**coinControl**/nullptr, /**nMinimumAmount**/1,
    /**nMaximumAmount**/MAX_MONEY, /**nMinimumSumAmount**/MAX_MONEY, /**nMaximumCount**/0, /**nMinDepth**/6);

    for (const COutput& coin : vCoins) {
        CTxDestination address;
        if (coin.fSpendable) {
            //Veil: exclude coins held in the basecoin address
            auto pOut = FindNonChangeParentOutput(*coin.tx->tx, coin.i);
            if (!pOut->IsStandardOutput())
                continue;
            if (!ExtractDestination(*pOut->GetPScriptPubKey(), address))
                continue;
            if (mapAddressBook.count(address) && mapAddressBook.at(address).purpose == "basecoin")
                continue;
            balance += coin.tx->tx->vpout[coin.i]->GetValue();
            vMintableCoins.emplace_back(std::move(coin));
        }
    }
    return balance;
}

/*
** uint8_t filterType => binary filter of coin type
** 0x1 = basecoin;
** 0x2 = zerocoin;
** 0x4 = ct;
** 0x8 = ringct;
** default if parameter isn't included is "basecoin" for backwards compatability
** to code that hasn't been adopted to expect all unspent outputs to be returned.
*/
void CWallet::AvailableCoins(std::vector<COutput> &vCoins, bool fOnlySafe, const CCoinControl *coinControl,
                             const CAmount &nMinimumAmount, const CAmount &nMaximumAmount,
                             const CAmount &nMinimumSumAmount, const uint64_t nMaximumCount,
                             const int nMinDepth, const int nMaxDepth, bool fIncludeImmature,
                             const uint8_t filterType) const
{
    AssertLockHeld(cs_main);
    AssertLockHeld(cs_wallet);

    vCoins.clear();
    CAmount nTotal = 0;

    for (const auto& entry : mapWallet)
    {
        const uint256& wtxid = entry.first;
        const CWalletTx* pcoin = &entry.second;

        if (!CheckFinalTx(*pcoin->tx))
            continue;

        if (pcoin->IsCoinBase()) {
            if (pcoin->GetBlocksToMaturity() > 0)
                continue;
            //Null coinbase for PoS
            if (pcoin->tx->vpout[0]->GetValue() == 0)
                continue;
        }

        int nDepth = pcoin->GetDepthInMainChain();
        if (nDepth < 0)
            continue;

        // We should not consider coins which aren't at least in our mempool
        // It's possible for these to be conflicted via ancestors which we may never be able to detect
        if (nDepth == 0 && !pcoin->InMempool())
            continue;

        bool safeTx = pcoin->IsTrusted();

        // We should not consider coins from transactions that are replacing
        // other transactions.
        //
        // Example: There is a transaction A which is replaced by bumpfee
        // transaction B. In this case, we want to prevent creation of
        // a transaction B' which spends an output of B.
        //
        // Reason: If transaction A were initially confirmed, transactions B
        // and B' would no longer be valid, so the user would have to create
        // a new transaction C to replace B'. However, in the case of a
        // one-block reorg, transactions B' and C might BOTH be accepted,
        // when the user only wanted one of them. Specifically, there could
        // be a 1-block reorg away from the chain where transactions A and C
        // were accepted to another chain where B, B', and C were all
        // accepted.
        if (nDepth == 0 && pcoin->mapValue.count("replaces_txid")) {
            safeTx = false;
        }

        // Similarly, we should not consider coins from transactions that
        // have been replaced. In the example above, we would want to prevent
        // creation of a transaction A' spending an output of A, because if
        // transaction B were initially confirmed, conflicting with A and
        // A', we wouldn't want to the user to create a transaction D
        // intending to replace A', but potentially resulting in a scenario
        // where A, A', and D could all be accepted (instead of just B and
        // D, or just A and A' like the user would want).
        if (nDepth == 0 && pcoin->mapValue.count("replaced_by_txid")) {
            safeTx = false;
        }

        if (fOnlySafe && !safeTx) {
            continue;
        }

        if (nDepth < nMinDepth || nDepth > nMaxDepth) {
            continue;
        }

        for (unsigned int i = 0; i < pcoin->tx->vpout.size(); i++) {
            // Null first output on coinstake
            if (pcoin->tx->IsCoinStake() && i == 0) {
                continue;
            }

            auto pout = pcoin->tx->vpout[i];

            // For backwards compatability, we need to be able to limit to basecoin, so allow
            // the caller to filter by cointype as we transition.
            const OutputTypes outType = (OutputTypes) pout->GetType();
            switch (outType) {
              case OUTPUT_STANDARD:
                if (pout->IsZerocoinMint() && !(filterType & FILTER_ZEROCOIN))
                    continue;          // caller isn't requesting zerocoin
                else
                    if (!(filterType & FILTER_BASECOIN))
                        continue;      // caller isn't requesting basecoin
                break;
              case OUTPUT_RINGCT:
                    if (!(filterType & FILTER_RINGCT))
                        continue;      // caller isn't requesting RingCT outputs
                break;
              case OUTPUT_CT:
                    if (!(filterType & FILTER_CT))
                        continue;      // caller isn't requesting stealth outputs
                break;
              default:
                // Disallow unknown types; shouldn't happen. May want to assert instead
                continue;
            }

            // Value calculated for search conditions
            CAmount nValue = 0;
            if (!pcoin->tx->HasBlindedValues() || pout->IsZerocoinMint()) {
                nValue = pout->GetValue();
            } else {
                MapRecords_t::iterator mi = pAnonWalletMain->mapRecords.find(pcoin->tx->GetHash());
                if (mi != pAnonWalletMain->mapRecords.end()) {
                    const COutputRecord *outputRecord = mi->second.GetOutput(i);
                    if (outputRecord != nullptr) {
                        // check if the ringct is already spent.  Should be rolled into
                        // CWallet::IsSpent() as well as the zerocoin isUsed()
                        if (outputRecord->IsSpent()) {
                            continue;
                        }
                        nValue = outputRecord->GetAmount();
                    }
                }
            }

            // Some ringct have zero value, skip those or if the value isn't in the filter
            if (!nValue || ((nValue < nMinimumAmount) || (nValue > nMaximumAmount)))
                continue;

            if (coinControl && coinControl->HasSelected() && !coinControl->fAllowOtherInputs
                && !coinControl->IsSelected(COutPoint(entry.first, i)))
                continue;

            if (IsLockedCoin(entry.first, i))
                continue;

            if (IsSpent(wtxid, i))
                continue;

            // should eventually roll this into IsSpent()
            if (pout->IsZerocoinMint()) {
              libzerocoin::PublicCoin pubcoin(Params().Zerocoin_Params());
              if (OutputToPublicCoin(pout.get(), pubcoin)) {
                  uint256 hashPubcoin = GetPubCoinHash(pubcoin.getValue());
                  if (IsMyMint(pubcoin.getValue())) {
                      CMintMeta meta;
                      if (GetMintMeta(hashPubcoin, meta)) {
                          if (meta.isUsed)
                              continue;
                      }
                  }
               }
            }

            isminetype mine = IsMine(pout.get());

            if (mine == ISMINE_NO) {
                continue;
            }

            // default to solvable or spendable, if it isn't
            // standard output it won't be a watch address
            bool solvable = true;
            bool spendable = true;
            if (pout->IsStandardOutput()) {
                solvable = IsSolvable(*this, *pout->GetPScriptPubKey());
                spendable = ((mine & ISMINE_SPENDABLE) != ISMINE_NO)
                             || (((mine & ISMINE_WATCH_ONLY) != ISMINE_NO)
                                 && (coinControl && coinControl->fAllowWatchOnly && solvable));
            }

            vCoins.push_back(COutput(pcoin, i, nDepth, spendable, solvable, safeTx, false,
                    (pout->IsStandardOutput() && coinControl && coinControl->fAllowWatchOnly)));

            // Checks the sum amount of all UTXOs.
            if (nMinimumSumAmount != MAX_MONEY) {
                nTotal += nValue;

                if (nTotal >= nMinimumSumAmount) {
                    return;
                }
            }

            // Checks the maximum number of UTXOs.
            if (nMaximumCount > 0 && vCoins.size() >= nMaximumCount) {
                return;
            }
        }
    }
}

std::map<CTxDestination, std::vector<COutput>> CWallet::ListCoins() const
{
    // TODO: Add AssertLockHeld(cs_wallet) here.
    //
    // Because the return value from this function contains pointers to
    // CWalletTx objects, callers to this function really should acquire the
    // cs_wallet lock before calling it. However, the current caller doesn't
    // acquire this lock yet. There was an attempt to add the missing lock in
    // https://github.com/bitcoin/bitcoin/pull/10340, but that change has been
    // postponed until after https://github.com/bitcoin/bitcoin/pull/10244 to
    // avoid adding some extra complexity to the Qt code.

    std::map<CTxDestination, std::vector<COutput>> result;
    std::vector<COutput> availableCoins;

    LOCK2(cs_main, cs_wallet);
    AvailableCoins(availableCoins);

    for (auto& coin : availableCoins) {
        CTxDestination address;
        if (coin.fSpendable) {
            auto pOut = FindNonChangeParentOutput(*coin.tx->tx, coin.i);
            if (!pOut->IsStandardOutput())
                continue;
            if (!ExtractDestination(*pOut->GetPScriptPubKey(), address))
                continue;
            result[address].emplace_back(std::move(coin));
        }
    }

    std::vector<COutPoint> lockedCoins;
    ListLockedCoins(lockedCoins);
    for (const auto& output : lockedCoins) {
        auto it = mapWallet.find(output.hash);
        if (it != mapWallet.end()) {
            int depth = it->second.GetDepthInMainChain();
            if (depth >= 0 && output.n < it->second.tx->vpout.size() &&
                IsMine(it->second.tx->vpout[output.n].get()) == ISMINE_SPENDABLE) {
                CTxDestination address;
                auto pOut = FindNonChangeParentOutput(*it->second.tx, output.n);
                if (!pOut->IsStandardOutput())
                    continue;
                if (!ExtractDestination(*pOut->GetPScriptPubKey(), address))
                    continue;
                result[address].emplace_back(&it->second, output.n, depth, true /* spendable */, true /* solvable */, false /* safe */);
            }
        }
    }

    return result;
}

const CTxOutBaseRef& CWallet::FindNonChangeParentOutput(const CTransaction& tx, int output) const
{
    const CTransaction* ptx = &tx;
    int n = output;
    while (IsChange(ptx->vpout[n].get()) && ptx->vin.size() > 0) {
        const COutPoint& prevout = ptx->vin[0].prevout;
        auto it = mapWallet.find(prevout.hash);
        if (it == mapWallet.end() || it->second.tx->vpout.size() <= prevout.n ||
            !IsMine(it->second.tx->vpout[prevout.n].get())) {
            break;
        }
        ptx = it->second.tx.get();
        n = prevout.n;
    }
    return ptx->vpout[n];
}

bool CWallet::SelectCoinsMinConf(const CAmount& nTargetValue, const CoinEligibilityFilter& eligibility_filter, std::vector<OutputGroup> groups,
                                 std::set<CInputCoin>& setCoinsRet, CAmount& nValueRet, const CoinSelectionParams& coin_selection_params, bool& bnb_used) const
{
    setCoinsRet.clear();
    nValueRet = 0;

    std::vector<OutputGroup> utxo_pool;
    if (coin_selection_params.use_bnb) {
        // Get long term estimate
        FeeCalculation feeCalc;
        CCoinControl temp;
        temp.m_confirm_target = 1008;
        CFeeRate long_term_feerate = GetMinimumFeeRate(*this, temp, ::mempool, ::feeEstimator, &feeCalc);

        // Calculate cost of change
        CAmount cost_of_change = GetDiscardRate(*this, ::feeEstimator).GetFee(coin_selection_params.change_spend_size) + coin_selection_params.effective_fee.GetFee(coin_selection_params.change_output_size);

        // Filter by the min conf specs and add to utxo_pool and calculate effective value
        for (OutputGroup& group : groups) {
            if (!group.EligibleForSpending(eligibility_filter)) continue;

            group.fee = 0;
            group.long_term_fee = 0;
            group.effective_value = 0;
            for (auto it = group.m_outputs.begin(); it != group.m_outputs.end(); ) {
                const CInputCoin& coin = *it;
                CAmount effective_value = coin.txout.nValue - (coin.m_input_bytes < 0 ? 0 : coin_selection_params.effective_fee.GetFee(coin.m_input_bytes));
                // Only include outputs that are positive effective value (i.e. not dust)
                if (effective_value > 0) {
                    group.fee += coin.m_input_bytes < 0 ? 0 : coin_selection_params.effective_fee.GetFee(coin.m_input_bytes);
                    group.long_term_fee += coin.m_input_bytes < 0 ? 0 : long_term_feerate.GetFee(coin.m_input_bytes);
                    group.effective_value += effective_value;
                    ++it;
                } else {
                    it = group.Discard(coin);
                }
            }
            if (group.effective_value > 0) utxo_pool.push_back(group);
        }
        // Calculate the fees for things that aren't inputs
        CAmount not_input_fees = coin_selection_params.effective_fee.GetFee(coin_selection_params.tx_noinputs_size);
        bnb_used = true;
        return SelectCoinsBnB(utxo_pool, nTargetValue, cost_of_change, setCoinsRet, nValueRet, not_input_fees);
    } else {
        // Filter by the min conf specs and add to utxo_pool
        for (const OutputGroup& group : groups) {
            if (!group.EligibleForSpending(eligibility_filter)) continue;
            utxo_pool.push_back(group);
        }
        bnb_used = false;
        return KnapsackSolver(nTargetValue, utxo_pool, setCoinsRet, nValueRet);
    }
}

bool CWallet::SelectCoins(const std::vector<COutput>& vAvailableCoins, const CAmount& nTargetValue, std::set<CInputCoin>& setCoinsRet,
        CAmount& nValueRet, const CCoinControl& coin_control, CoinSelectionParams& coin_selection_params, bool& bnb_used) const
{
    std::vector<COutput> vCoins(vAvailableCoins);

    // coin control -> return all selected outputs (we want all selected to go into the transaction for sure)
    if (coin_control.HasSelected() && !coin_control.fAllowOtherInputs)
    {
        // We didn't use BnB here, so set it to false.
        bnb_used = false;

        for (const COutput& out : vCoins)
        {
            if (!out.fSpendable)
                 continue;
            nValueRet += out.tx->tx->vpout[out.i]->GetValue();
            setCoinsRet.insert(out.GetInputCoin());
        }
        return (nValueRet >= nTargetValue);
    }

    // calculate value from preset inputs and store them
    std::set<CInputCoin> setPresetCoins;
    CAmount nValueFromPresetInputs = 0;

    std::vector<COutPoint> vPresetInputs;
    coin_control.ListSelected(vPresetInputs);
    for (const COutPoint& outpoint : vPresetInputs)
    {
        // For now, don't use BnB if preset inputs are selected. TODO: Enable this later
        bnb_used = false;
        coin_selection_params.use_bnb = false;

        std::map<uint256, CWalletTx>::const_iterator it = mapWallet.find(outpoint.hash);
        if (it != mapWallet.end())
        {
            const CWalletTx* pcoin = &it->second;
            // Clearly invalid input, fail
            if (pcoin->tx->vpout.size() <= outpoint.n)
                return false;
            // Just to calculate the marginal byte size
            nValueFromPresetInputs += pcoin->tx->vpout[outpoint.n]->GetValue();
            setPresetCoins.insert(CInputCoin(pcoin->tx, outpoint.n));
        } else
            return false; // TODO: Allow non-wallet inputs
    }

    // remove preset inputs from vCoins
    for (std::vector<COutput>::iterator it = vCoins.begin(); it != vCoins.end() && coin_control.HasSelected();)
    {
        if (setPresetCoins.count(it->GetInputCoin()))
            it = vCoins.erase(it);
        else
            ++it;
    }

    // form groups from remaining coins; note that preset coins will not
    // automatically have their associated (same address) coins included
    if (coin_control.m_avoid_partial_spends && vCoins.size() > OUTPUT_GROUP_MAX_ENTRIES) {
        // Cases where we have 11+ outputs all pointing to the same destination may result in
        // privacy leaks as they will potentially be deterministically sorted. We solve that by
        // explicitly shuffling the outputs before processing
        std::shuffle(vCoins.begin(), vCoins.end(), FastRandomContext());
    }
    std::vector<OutputGroup> groups = GroupOutputs(vCoins, !coin_control.m_avoid_partial_spends);

    size_t max_ancestors = (size_t)std::max<int64_t>(1, gArgs.GetArg("-limitancestorcount", DEFAULT_ANCESTOR_LIMIT));
    size_t max_descendants = (size_t)std::max<int64_t>(1, gArgs.GetArg("-limitdescendantcount", DEFAULT_DESCENDANT_LIMIT));
    bool fRejectLongChains = gArgs.GetBoolArg("-walletrejectlongchains", DEFAULT_WALLET_REJECT_LONG_CHAINS);

    bool res = nTargetValue <= nValueFromPresetInputs ||
        SelectCoinsMinConf(nTargetValue - nValueFromPresetInputs, CoinEligibilityFilter(1, 6, 0), groups, setCoinsRet, nValueRet, coin_selection_params, bnb_used) ||
        SelectCoinsMinConf(nTargetValue - nValueFromPresetInputs, CoinEligibilityFilter(1, 1, 0), groups, setCoinsRet, nValueRet, coin_selection_params, bnb_used) ||
        (m_spend_zero_conf_change && SelectCoinsMinConf(nTargetValue - nValueFromPresetInputs, CoinEligibilityFilter(0, 1, 2), groups, setCoinsRet, nValueRet, coin_selection_params, bnb_used)) ||
        (m_spend_zero_conf_change && SelectCoinsMinConf(nTargetValue - nValueFromPresetInputs, CoinEligibilityFilter(0, 1, std::min((size_t)4, max_ancestors/3), std::min((size_t)4, max_descendants/3)), groups, setCoinsRet, nValueRet, coin_selection_params, bnb_used)) ||
        (m_spend_zero_conf_change && SelectCoinsMinConf(nTargetValue - nValueFromPresetInputs, CoinEligibilityFilter(0, 1, max_ancestors/2, max_descendants/2), groups, setCoinsRet, nValueRet, coin_selection_params, bnb_used)) ||
        (m_spend_zero_conf_change && SelectCoinsMinConf(nTargetValue - nValueFromPresetInputs, CoinEligibilityFilter(0, 1, max_ancestors-1, max_descendants-1), groups, setCoinsRet, nValueRet, coin_selection_params, bnb_used)) ||
        (m_spend_zero_conf_change && !fRejectLongChains && SelectCoinsMinConf(nTargetValue - nValueFromPresetInputs, CoinEligibilityFilter(0, 1, std::numeric_limits<uint64_t>::max()), groups, setCoinsRet, nValueRet, coin_selection_params, bnb_used));

    // because SelectCoinsMinConf clears the setCoinsRet, we now add the possible inputs to the coinset
    util::insert(setCoinsRet, setPresetCoins);

    // add preset inputs to the total value selected
    nValueRet += nValueFromPresetInputs;

    return res;
}

std::map<libzerocoin::CoinDenomination, int> mapMintMaturity;
int nLastMaturityCheck = 0;
CAmount CWallet::GetZerocoinBalance(bool fMatureOnly, const int min_depth) const
{
    if (fMatureOnly) {
        if (chainActive.Height() > nLastMaturityCheck)
            mapMintMaturity = GetMintMaturityHeight();
        nLastMaturityCheck = chainActive.Height();

        CAmount nBalance = 0;
        std::vector<CMintMeta> vMints = zTracker->GetMints(true);
        for (auto meta : vMints) {
            if (!mapMintMaturity.count(meta.denom) || meta.nHeight >= mapMintMaturity.at(meta.denom) || meta.nHeight >= chainActive.Height() || meta.nHeight == 0 || (min_depth > (chainActive.Height() - meta.nHeight)))
                continue;
            nBalance += libzerocoin::ZerocoinDenominationToAmount(meta.denom);
        }
        return nBalance;
    }

    return zTracker->GetBalance(false, false, min_depth);
}

CAmount CWallet::GetUnconfirmedZerocoinBalance() const
{
    return zTracker->GetUnconfirmedBalance();
}

CAmount CWallet::GetImmatureZerocoinBalance() const
{
    if (chainActive.Height() > nLastMaturityCheck)
        mapMintMaturity = GetMintMaturityHeight();
    nLastMaturityCheck = chainActive.Height();

    CAmount nBalance = 0;
    std::vector<CMintMeta> vMints = zTracker->GetMints(false);
    for (auto meta : vMints) {
        meta.nMemFlags = CzTracker::GetMintMemFlags(meta, nLastMaturityCheck, mapMintMaturity);
        //If not confirmed, this is considered unconfirmed, not immature.
        if (!(meta.nMemFlags & MINT_CONFIRMED) || meta.nMemFlags & MINT_MATURE)
            continue;
        nBalance += libzerocoin::ZerocoinDenominationToAmount(meta.denom);

    }
    return nBalance;
}

bool CWallet::MintableCoins()
{
    LOCK(cs_main);
    CAmount nZBalance = GetZerocoinBalance(false);

    int nRequiredDepth = Params().Zerocoin_RequiredStakeDepth();
    if (chainActive.Height() >= Params().HeightLightZerocoin())
        nRequiredDepth = Params().Zerocoin_RequiredStakeDepthV2();

    // zero coin
    if (nZBalance > 0) {
        std::set<CMintMeta> setMints = zTracker->ListMints(true, true, true);
        for (auto mint : setMints) {
            if (mint.nVersion < CZerocoinMint::STAKABLE_VERSION)
                continue;
            if (mint.nHeight > chainActive.Height() - nRequiredDepth)
                continue;
            return true;
        }
    }

    return false;
}

bool CWallet::SignTransaction(CMutableTransaction &tx)
{
    AssertLockHeld(cs_wallet); // mapWallet

    // sign the new tx
    int nIn = 0;
    for (auto& input : tx.vin) {
        std::map<uint256, CWalletTx>::const_iterator mi = mapWallet.find(input.prevout.hash);
        if(mi == mapWallet.end() || input.prevout.n >= mi->second.tx->vpout.size()) {
            return false;
        }
        CScript scriptPubKey;
        if (!mi->second.tx->vpout[input.prevout.n]->GetScriptPubKey(scriptPubKey))
            return error("%s: Failed to get scriptpubkey from output, probably wrong type", __func__);
        const CAmount& amount = mi->second.tx->vpout[input.prevout.n]->GetValue();
        SignatureData sigdata;
        std::vector<uint8_t> vchAmount(8);
        memcpy(&vchAmount[0], &amount, 8);
        if (!ProduceSignature(*this, MutableTransactionSignatureCreator(&tx, nIn, vchAmount, SIGHASH_ALL), scriptPubKey, sigdata)) {
            return false;
        }
        UpdateInput(input, sigdata);
        nIn++;
    }
    return true;
}

bool CWallet::FundTransaction(CMutableTransaction& tx, CAmount& nFeeRet, int& nChangePosInOut, std::string& strFailReason,
        bool lockUnspents, const std::set<int>& setSubtractFeeFromOutputs, CCoinControl coinControl)
{
    std::vector<CRecipient> vecSend;

    // Turn the txout set into a CRecipient vector.
    for (size_t idx = 0; idx < tx.vpout.size(); idx++) {
        const auto& pOut = tx.vpout[idx];
        CTxOut txOut;
        if (!pOut->GetTxOut(txOut))
            continue;
        CRecipient recipient = {txOut.scriptPubKey, txOut.nValue, setSubtractFeeFromOutputs.count(idx) == 1};
        vecSend.push_back(recipient);
    }

    coinControl.fAllowOtherInputs = true;

    for (const CTxIn& txin : tx.vin) {
        coinControl.Select(txin.prevout, 0); //todo select amount
    }

    // Acquire the locks to prevent races to the new locked unspents between the
    // CreateTransaction call and LockCoin calls (when lockUnspents is true).
    LOCK2(cs_main, cs_wallet);

    CReserveKey reservekey(this);
    CTransactionRef tx_new;
    if (!CreateTransaction(vecSend, tx_new, reservekey, nFeeRet, nChangePosInOut, strFailReason, coinControl, false)) {
        return false;
    }

    if (nChangePosInOut != -1) {
        tx.vpout.insert(tx.vpout.begin() + nChangePosInOut, tx_new->vpout[nChangePosInOut]);
        // We don't have the normal Create/Commit cycle, and don't want to risk
        // reusing change, so just remove the key from the keypool here.
        reservekey.KeepKey();
    }

    // Copy output sizes from new transaction; they may have had the fee
    // subtracted from them.
    for (unsigned int idx = 0; idx < tx.vpout.size(); idx++) {
        if (!tx.vpout[idx]->IsStandardOutput())
            continue;
        auto txOutStd = (CTxOutStandard*)tx.vpout[idx].get();
        auto nValueNew = tx_new->vpout[idx]->GetValue();
        txOutStd->nValue = nValueNew;
    }

    // Add new txins while keeping original txin scriptSig/order.
    for (const CTxIn& txin : tx_new->vin) {
        if (!coinControl.IsSelected(txin.prevout)) {
            tx.vin.push_back(txin);

            if (lockUnspents) {
                LockCoin(txin.prevout);
            }
        }
    }

    return true;
}

OutputType CWallet::TransactionChangeType(OutputType change_type, const std::vector<CRecipient>& vecSend)
{
    // If -changetype is specified, always use that change type.
    if (change_type != OutputType::CHANGE_AUTO) {
        return change_type;
    }

    // if m_default_address_type is legacy, use legacy address as change (even
    // if some of the outputs are P2WPKH or P2WSH).
    if (m_default_address_type == OutputType::LEGACY) {
        return OutputType::LEGACY;
    }

    // if any destination is P2WPKH or P2WSH, use P2WPKH for the change
    // output.
    for (const auto& recipient : vecSend) {
        // Check if any destination contains a witness program:
        int witnessversion = 0;
        std::vector<unsigned char> witnessprogram;
        if (recipient.scriptPubKey.IsWitnessProgram(witnessversion, witnessprogram)) {
            return OutputType::BECH32;
        }
    }

    // else use m_default_address_type for change
    return m_default_address_type;
}

bool CWallet::CreateTransaction(const std::vector<CRecipient>& vecSend, CTransactionRef& tx, CReserveKey& reservekey, CAmount& nFeeRet,
                         int& nChangePosInOut, std::string& strFailReason, const CCoinControl& coin_control, bool sign)
{
    CAmount nValue = 0;
    int nChangePosRequest = nChangePosInOut;
    unsigned int nSubtractFeeFromAmount = 0;
    for (const auto& recipient : vecSend)
    {
        if (nValue < 0 || recipient.nAmount < 0)
        {
            strFailReason = _("Transaction amounts must not be negative");
            return false;
        }
        nValue += recipient.nAmount;

        if (recipient.fSubtractFeeFromAmount)
            nSubtractFeeFromAmount++;
    }
    if (vecSend.empty())
    {
        strFailReason = _("Transaction must have at least one recipient");
        return false;
    }

    CMutableTransaction txNew;

    // Discourage fee sniping.
    //
    // For a large miner the value of the transactions in the best block and
    // the mempool can exceed the cost of deliberately attempting to mine two
    // blocks to orphan the current best block. By setting nLockTime such that
    // only the next block can include the transaction, we discourage this
    // practice as the height restricted and limited blocksize gives miners
    // considering fee sniping fewer options for pulling off this attack.
    //
    // A simple way to think about this is from the wallet's point of view we
    // always want the blockchain to move forward. By setting nLockTime this
    // way we're basically making the statement that we only want this
    // transaction to appear in the next block; we don't want to potentially
    // encourage reorgs by allowing transactions to appear at lower heights
    // than the next block in forks of the best chain.
    //
    // Of course, the subsidy is high enough, and transaction volume low
    // enough, that fee sniping isn't a problem yet, but by implementing a fix
    // now we ensure code won't be written that makes assumptions about
    // nLockTime that preclude a fix later.
    txNew.nLockTime = chainActive.Height();

    // Secondly occasionally randomly pick a nLockTime even further back, so
    // that transactions that are delayed after signing for whatever reason,
    // e.g. high-latency mix networks and some CoinJoin implementations, have
    // better privacy.
    if (GetRandInt(10) == 0)
        txNew.nLockTime = std::max(0, (int)txNew.nLockTime - GetRandInt(100));

    assert(txNew.nLockTime <= (unsigned int)chainActive.Height());
    assert(txNew.nLockTime < LOCKTIME_THRESHOLD);
    FeeCalculation feeCalc;
    CAmount nFeeNeeded;
    int nBytes;
    {
        std::set<CInputCoin> setCoins;
        LOCK2(cs_main, cs_wallet);
        {
            std::vector<COutput> vAvailableCoins;
            AvailableCoins(vAvailableCoins, true, &coin_control);
            CoinSelectionParams coin_selection_params; // Parameters for coin selection, init with dummy

            // Create change script that will be used if we need change
            // TODO: pass in scriptChange instead of reservekey so
            // change transaction isn't always pay-to-veil-address
            CScript scriptChange;

            // coin control: send change to custom address
            if (!boost::get<CNoDestination>(&coin_control.destChange)) {
                scriptChange = GetScriptForDestination(coin_control.destChange);
            } else { // no coin control: send change to newly generated address
                // Note: We use a new key here to keep it from being obvious which side is the change.
                //  The drawback is that by not reusing a previous key, the change may be lost if a
                //  backup is restored, if the backup doesn't have the new private key for the change.
                //  If we reused the old key, it would be possible to add code to look for and
                //  rediscover unknown transactions that were written with keys of ours to recover
                //  post-backup change.

                // Reserve a new key pair from key pool
                if (IsWalletFlagSet(WALLET_FLAG_DISABLE_PRIVATE_KEYS)) {
                    strFailReason = _("Can't generate a change-address key. Private keys are disabled for this wallet.");
                    return false;
                }
                CPubKey vchPubKey;
                bool ret;
                ret = reservekey.GetReservedKey(vchPubKey, true);
                if (!ret)
                {
                    strFailReason = _("Keypool ran out, please call keypoolrefill first");
                    return false;
                }

                const OutputType change_type = TransactionChangeType(coin_control.m_change_type ? *coin_control.m_change_type : m_default_change_type, vecSend);

                LearnRelatedScripts(vchPubKey, change_type);
                scriptChange = GetScriptForDestination(GetDestinationForKey(vchPubKey, change_type));
            }
            CTxOut change_prototype_txout(0, scriptChange);
            coin_selection_params.change_output_size = GetSerializeSize(change_prototype_txout, SER_DISK, 0);

            CFeeRate discard_rate = GetDiscardRate(*this, ::feeEstimator);

            // Get the fee rate to use effective values in coin selection
            CFeeRate nFeeRateNeeded = GetMinimumFeeRate(*this, coin_control, ::mempool, ::feeEstimator, &feeCalc);

            nFeeRet = 0;
            bool pick_new_inputs = true;
            CAmount nValueIn = 0;

            // BnB selector is the only selector used when this is true.
            // That should only happen on the first pass through the loop.
            coin_selection_params.use_bnb = nSubtractFeeFromAmount == 0; // If we are doing subtract fee from recipient, then don't use BnB
            // Start with no fee and loop until there is enough fee
            while (true)
            {
                nChangePosInOut = nChangePosRequest;
                txNew.vin.clear();
                txNew.vpout.clear();
                bool fFirst = true;

                CAmount nValueToSelect = nValue;
                if (nSubtractFeeFromAmount == 0)
                    nValueToSelect += nFeeRet;

                // vouts to the payees
                coin_selection_params.tx_noinputs_size = 11; // Static vsize overhead + outputs vsize. 4 nVersion, 4 nLocktime, 1 input count, 1 output count, 1 witness overhead (dummy, flag, stack size)
                for (const auto& recipient : vecSend)
                {
                    CTxOut txout(recipient.nAmount, recipient.scriptPubKey);

                    if (recipient.fSubtractFeeFromAmount)
                    {
                        assert(nSubtractFeeFromAmount != 0);
                        txout.nValue -= nFeeRet / nSubtractFeeFromAmount; // Subtract fee equally from each selected recipient

                        if (fFirst) // first receiver pays the remainder not divisible by output count
                        {
                            fFirst = false;
                            txout.nValue -= nFeeRet % nSubtractFeeFromAmount;
                        }
                    }
                    // Include the fee cost for outputs. Note this is only used for BnB right now
                    coin_selection_params.tx_noinputs_size += ::GetSerializeSize(txout, SER_NETWORK, PROTOCOL_VERSION);

                    if (IsDust(txout, ::dustRelayFee))
                    {
                        if (recipient.fSubtractFeeFromAmount && nFeeRet > 0)
                        {
                            if (txout.nValue < 0)
                                strFailReason = _("The transaction amount is too small to pay the fee");
                            else
                                strFailReason = _("The transaction amount is too small to send after the fee has been deducted");
                        }
                        else
                            strFailReason = _("Transaction amount too small");
                        return false;
                    }
                    txNew.vpout.push_back(txout.GetSharedPtr());
                }

                // Choose coins to use
                bool bnb_used;
                if (pick_new_inputs) {
                    nValueIn = 0;
                    setCoins.clear();
                    coin_selection_params.change_spend_size = CalculateMaximumSignedInputSize(change_prototype_txout, this);
                    coin_selection_params.effective_fee = nFeeRateNeeded;

                    if (!SelectCoins(vAvailableCoins, nValueToSelect, setCoins, nValueIn, coin_control, coin_selection_params, bnb_used))
                    {
                        // If BnB was used, it was the first pass. No longer the first pass and continue loop with knapsack.
                        if (bnb_used) {
                            coin_selection_params.use_bnb = false;
                            continue;
                        }
                        else {
                            strFailReason = _("Insufficient funds");
                            return false;
                        }
                    }
                }

                const CAmount nChange = nValueIn - nValueToSelect;
                if (nChange > 0)
                {
                    // Fill a vout to ourself
                    CTxOut newTxOut(nChange, scriptChange);

                    // Never create dust outputs; if we would, just
                    // add the dust to the fee.
                    // The nChange when BnB is used is always going to go to fees.
                    if (IsDust(newTxOut, discard_rate) || bnb_used)
                    {
                        nChangePosInOut = -1;
                        nFeeRet += nChange;
                    }
                    else
                    {
                        if (nChangePosInOut == -1)
                        {
                            // Insert change txn at random position:
                            nChangePosInOut = GetRandInt(txNew.vpout.size()+1);
                        }
                        else if ((unsigned int)nChangePosInOut > txNew.vpout.size())
                        {
                            strFailReason = _("Change index out of range");
                            return false;
                        }

                        std::vector<CTxOutBaseRef>::iterator position = txNew.vpout.begin()+nChangePosInOut;
                        txNew.vpout.insert(position, newTxOut.GetSharedPtr());
                    }
                } else {
                    nChangePosInOut = -1;
                }

                // Dummy fill vin for maximum size estimation
                //
                for (const auto& coin : setCoins) {
                    txNew.vin.push_back(CTxIn(coin.outpoint,CScript()));
                }

                nBytes = CalculateMaximumSignedTxSize(txNew, this, coin_control.fAllowWatchOnly);
                if (nBytes < 0) {
                    strFailReason = _("Signing transaction failed");
                    return false;
                }

                nFeeNeeded = GetMinimumFee(*this, nBytes, coin_control, ::mempool, ::feeEstimator, &feeCalc);
                if (feeCalc.reason == FeeReason::FALLBACK && !m_allow_fallback_fee) {
                    // eventually allow a fallback fee
                    strFailReason = _("Fee estimation failed. Fallbackfee is disabled. Wait a few blocks or enable -fallbackfee.");
                    return false;
                }

                // If we made it here and we aren't even able to meet the relay fee on the next pass, give up
                // because we must be at the maximum allowed fee.
                if (nFeeNeeded < ::minRelayTxFee.GetFee(nBytes))
                {
                    strFailReason = _("Transaction too large for fee policy");
                    return false;
                }

                if (nFeeRet >= nFeeNeeded) {
                    // Reduce fee to only the needed amount if possible. This
                    // prevents potential overpayment in fees if the coins
                    // selected to meet nFeeNeeded result in a transaction that
                    // requires less fee than the prior iteration.

                    // If we have no change and a big enough excess fee, then
                    // try to construct transaction again only without picking
                    // new inputs. We now know we only need the smaller fee
                    // (because of reduced tx size) and so we should add a
                    // change output. Only try this once.
                    if (nChangePosInOut == -1 && nSubtractFeeFromAmount == 0 && pick_new_inputs) {
                        unsigned int tx_size_with_change = nBytes + coin_selection_params.change_output_size + 2; // Add 2 as a buffer in case increasing # of outputs changes compact size
                        CAmount fee_needed_with_change = GetMinimumFee(*this, tx_size_with_change, coin_control, ::mempool, ::feeEstimator, nullptr);
                        CAmount minimum_value_for_change = GetDustThreshold(change_prototype_txout, discard_rate);
                        if (nFeeRet >= fee_needed_with_change + minimum_value_for_change) {
                            pick_new_inputs = false;
                            nFeeRet = fee_needed_with_change;
                            continue;
                        }
                    }

                    // If we have change output already, just increase it
                    if (nFeeRet > nFeeNeeded && nChangePosInOut != -1 && nSubtractFeeFromAmount == 0) {
                        CAmount extraFeePaid = nFeeRet - nFeeNeeded;
                        std::vector<CTxOutBaseRef>::iterator change_position = txNew.vpout.begin()+nChangePosInOut;
                        change_position->get()->AddToValue(extraFeePaid);
                        nFeeRet -= extraFeePaid;
                    }
                    break; // Done, enough fee included.
                }
                else if (!pick_new_inputs) {
                    // This shouldn't happen, we should have had enough excess
                    // fee to pay for the new output and still meet nFeeNeeded
                    // Or we should have just subtracted fee from recipients and
                    // nFeeNeeded should not have changed
                    strFailReason = _("Transaction fee and change calculation failed");
                    return false;
                }

                // Try to reduce change to include necessary fee
                if (nChangePosInOut != -1 && nSubtractFeeFromAmount == 0) {
                    CAmount additionalFeeNeeded = nFeeNeeded - nFeeRet;
                    std::vector<CTxOutBaseRef>::iterator change_position = txNew.vpout.begin()+nChangePosInOut;
                    // Only reduce change if remaining amount is still a large enough output.
                    if (change_position->get()->GetValue() >= MIN_FINAL_CHANGE + additionalFeeNeeded) {
                        change_position->get()->AddToValue(-additionalFeeNeeded);
                        nFeeRet += additionalFeeNeeded;
                        break; // Done, able to increase fee from change
                    }
                }

                // If subtracting fee from recipients, we now know what fee we
                // need to subtract, we have no reason to reselect inputs
                if (nSubtractFeeFromAmount > 0) {
                    pick_new_inputs = false;
                }

                // Include more fee and try again.
                nFeeRet = nFeeNeeded;
                coin_selection_params.use_bnb = false;
                continue;
            }
        }

        if (nChangePosInOut == -1) reservekey.ReturnKey(); // Return any reserved key if we don't have change

        // Shuffle selected coins and fill in final vin
        txNew.vin.clear();
        std::vector<CInputCoin> selected_coins(setCoins.begin(), setCoins.end());
        std::shuffle(selected_coins.begin(), selected_coins.end(), FastRandomContext());

        // Note how the sequence number is set to non-maxint so that
        // the nLockTime set above actually works.
        //
        // BIP125 defines opt-in RBF as any nSequence < maxint-1, so
        // we use the highest possible value in that range (maxint-2)
        // to avoid conflicting with other possible uses of nSequence,
        // and in the spirit of "smallest possible change from prior
        // behavior."
        const uint32_t nSequence = coin_control.m_signal_bip125_rbf.get_value_or(m_signal_rbf) ? MAX_BIP125_RBF_SEQUENCE : (CTxIn::SEQUENCE_FINAL - 1);
        for (const auto& coin : selected_coins) {
            txNew.vin.push_back(CTxIn(coin.outpoint, CScript(), nSequence));
        }

        if (sign)
        {
            int nIn = 0;
            for (const auto& coin : selected_coins)
            {
                const CScript& scriptPubKey = coin.txout.scriptPubKey;
                SignatureData sigdata;

                auto amount = coin.txout.nValue;
                std::vector<uint8_t> vchAmount(8);
                memcpy(&vchAmount[0], &amount, 8);
                if (!ProduceSignature(*this, MutableTransactionSignatureCreator(&txNew, nIn, vchAmount, SIGHASH_ALL), scriptPubKey, sigdata))
                {
                    strFailReason = _("Signing transaction failed");
                    return false;
                } else {
                    UpdateInput(txNew.vin.at(nIn), sigdata);
                }

                nIn++;
            }
        }

        // Return the constructed transaction data.
        tx = MakeTransactionRef(std::move(txNew));

        // Limit size
        if (GetTransactionWeight(*tx) > MAX_STANDARD_TX_WEIGHT)
        {
            strFailReason = _("Transaction too large");
            return false;
        }
    }

    if (gArgs.GetBoolArg("-walletrejectlongchains", DEFAULT_WALLET_REJECT_LONG_CHAINS)) {
        // Lastly, ensure this tx will pass the mempool's chain limits
        LockPoints lp;
        CTxMemPoolEntry entry(tx, 0, 0, 0, false, 0, lp);
        CTxMemPool::setEntries setAncestors;
        size_t nLimitAncestors = gArgs.GetArg("-limitancestorcount", DEFAULT_ANCESTOR_LIMIT);
        size_t nLimitAncestorSize = gArgs.GetArg("-limitancestorsize", DEFAULT_ANCESTOR_SIZE_LIMIT)*1000;
        size_t nLimitDescendants = gArgs.GetArg("-limitdescendantcount", DEFAULT_DESCENDANT_LIMIT);
        size_t nLimitDescendantSize = gArgs.GetArg("-limitdescendantsize", DEFAULT_DESCENDANT_SIZE_LIMIT)*1000;
        std::string errString;
        LOCK(::mempool.cs);
        if (!::mempool.CalculateMemPoolAncestors(entry, setAncestors, nLimitAncestors, nLimitAncestorSize, nLimitDescendants, nLimitDescendantSize, errString)) {
            strFailReason = _("Transaction has too long of a mempool chain");
            return false;
        }
    }

    WalletLogPrintf("Fee Calculation: Fee:%d Bytes:%u Needed:%d Tgt:%d (requested %d) Reason:\"%s\" Decay %.5f: Estimation: (%g - %g) %.2f%% %.1f/(%.1f %d mem %.1f out) Fail: (%g - %g) %.2f%% %.1f/(%.1f %d mem %.1f out)\n",
              nFeeRet, nBytes, nFeeNeeded, feeCalc.returnedTarget, feeCalc.desiredTarget, StringForFeeReason(feeCalc.reason), feeCalc.est.decay,
              feeCalc.est.pass.start, feeCalc.est.pass.end,
              100 * feeCalc.est.pass.withinTarget / (feeCalc.est.pass.totalConfirmed + feeCalc.est.pass.inMempool + feeCalc.est.pass.leftMempool),
              feeCalc.est.pass.withinTarget, feeCalc.est.pass.totalConfirmed, feeCalc.est.pass.inMempool, feeCalc.est.pass.leftMempool,
              feeCalc.est.fail.start, feeCalc.est.fail.end,
              100 * feeCalc.est.fail.withinTarget / (feeCalc.est.fail.totalConfirmed + feeCalc.est.fail.inMempool + feeCalc.est.fail.leftMempool),
              feeCalc.est.fail.withinTarget, feeCalc.est.fail.totalConfirmed, feeCalc.est.fail.inMempool, feeCalc.est.fail.leftMempool);
    return true;
}

bool CWallet::CreateCoinStake(const CBlockIndex* pindexBest, unsigned int nBits, CMutableTransaction& txNew, unsigned int& nTxNewTime, int64_t& nComputeTimeStart)
{
    // The following split & combine thresholds are important to security
    // Should not be adjusted if you don't understand the consequences
    //int64_t nCombineThreshold = 0;
    txNew.vin.clear();
    txNew.vpout.clear();

    // Mark coin stake transaction
    CScript scriptEmpty;
    scriptEmpty.clear();
    txNew.vpout.emplace_back(CTxOut(0, scriptEmpty).GetSharedPtr());

    // Get the list of stakable inputs
    std::list<std::unique_ptr<ZerocoinStake> > listInputs;
    if (!SelectStakeCoins(listInputs))
        return false;

    if (listInputs.empty())
        return false;

    //Small sleep if too far back on timing
    if (GetAdjustedTime() - chainActive.Tip()->GetBlockTime() < 1)
        MilliSleep(2500);

    CAmount nCredit = 0;
    CScript scriptPubKeyKernel;
    bool fKernelFound = false;
    for (std::unique_ptr<ZerocoinStake>& stakeInput : listInputs) {
        // Make sure the wallet is unlocked and shutdown hasn't been requested
        if (IsLocked() || ShutdownRequested())
            return false;

        CBlockIndex *pindexFrom = stakeInput->GetIndexFrom();
        if (!pindexFrom || pindexFrom->nHeight < 1) {
            LogPrintf("*** no pindexfrom\n");
            continue;
        }

        // Read block header
        uint256 hashProofOfStake;
        nTxNewTime = GetAdjustedTime();
        auto nTimeMinBlock = std::max(pindexBest->GetBlockTime() - MAX_PAST_BLOCK_TIME, pindexBest->GetMedianTimePast());
        if (nTxNewTime < nTimeMinBlock)
            nTxNewTime = nTimeMinBlock + 1;

        if (pindexFrom->GetBlockTime() + nStakeMinAge > nTxNewTime) {
            // Skip this one as it doesn't meet the minimum age
            continue;
        }

        //iterates each utxo inside of CheckStakeKernelHash()
        bool fWeightStake = true;
        if (Stake(stakeInput.get(), nBits, pindexFrom->GetBlockTime(), nTxNewTime, pindexBest, hashProofOfStake, fWeightStake)) {
            int nHeight = 0;
            {
                LOCK(cs_main);
                //Double check that this will pass time requirements
                if (nTxNewTime <= nTimeMinBlock) {
                    LogPrint(BCLog::STAKING, "%s : kernel found, but it is too far in the past \n", __func__);
                    continue;
                }
                nHeight = chainActive.Height();
            }

            nComputeTimeStart = GetTimeMillis();

            // Found a kernel
            LogPrintf("CreateCoinStake : kernel found\n");
            nCredit += stakeInput->GetValue();

            // Calculate reward
            CAmount nBlockReward, nFounderPayment, nFoundationPayment, nBudgetPayment;
            veil::Budget().GetBlockRewards(nHeight, nBlockReward, nFounderPayment, nFoundationPayment, nBudgetPayment);
            nCredit += nBlockReward;
            CBlockIndex* pindexPrev = chainActive.Tip();
            assert(pindexPrev != nullptr);
            CAmount nNetworkRewardReserve = pindexPrev ? pindexPrev->nNetworkRewardReserve : 0;
            CAmount nNetworkReward = nNetworkRewardReserve > Params().MaxNetworkReward() ? Params().MaxNetworkReward() : nNetworkRewardReserve;
            nCredit += nNetworkReward;

            // Create the output transaction(s)
            vector<CTxOut> vout;
            if (!stakeInput->CreateTxOuts(this, vout, nBlockReward)) {
                LogPrintf("%s : failed to get scriptPubKey\n", __func__);
                continue;
            }
            txNew.vpout.clear();
            txNew.vpout.emplace_back(CTxOut(0, scriptEmpty).GetSharedPtr());
            for (auto& txOut : vout)
                txNew.vpout.emplace_back(txOut.GetSharedPtr());

            // Limit size
            unsigned int nBytes = ::GetSerializeSize(txNew, SER_NETWORK, PROTOCOL_VERSION | SERIALIZE_TRANSACTION_NO_WITNESS) * WITNESS_SCALE_FACTOR;

            if (nBytes >= MAX_BLOCK_WEIGHT / 5)
                return error("CreateCoinStake : exceeded coinstake size limit");

            uint256 hashTxOut = txNew.GetOutputsHash();
            CTxIn in;
            {
                if (!stakeInput->CreateTxIn(this, in, hashTxOut)) {
                    LogPrintf("%s : failed to create TxIn\n", __func__);
                    txNew.vin.clear();
                    txNew.vpout.clear();
                    nCredit = 0;
                    continue;
                }
            }
            txNew.vin.emplace_back(in);

            //Mark mints as spent
            auto* z = (ZerocoinStake*)stakeInput.get();
            if (!z->MarkSpent(this, txNew.GetHash()))
                return error("%s: failed to mark mint as used\n", __func__);

            fKernelFound = true;
            break;
        }
        if (fKernelFound)
            break; // if kernel is found stop searching
    }
    return fKernelFound;
}
bool CWallet::SelectStakeCoins(std::list<std::unique_ptr<ZerocoinStake> >& listInputs)
{
    LOCK(cs_main);

    //Only update zerocoin set once per update interval
    bool fUpdate = false;
    static int64_t nTimeLastUpdate = 0;
    if (GetAdjustedTime() - nTimeLastUpdate > nStakeSetUpdateTime) {
        fUpdate = true;
        nTimeLastUpdate = GetAdjustedTime();
    }

    int nRequiredDepth = Params().Zerocoin_RequiredStakeDepth();
    if (chainActive.Height() >= Params().HeightLightZerocoin())
        nRequiredDepth = Params().Zerocoin_RequiredStakeDepthV2();

    std::set<CMintMeta> setMints = zTracker->ListMints(true, true, fUpdate);
    for (auto meta : setMints) {
        if (meta.hashStake == uint256()) {
            CZerocoinMint mint;
            if (GetMint(meta.hashSerial, mint)) {
                uint256 hashStake = mint.GetSerialNumber().getuint256();
                hashStake = Hash(hashStake.begin(), hashStake.end());
                meta.hashStake = hashStake;
                zTracker->UpdateState(meta);
            }
        }
        if (meta.nVersion < CZerocoinMint::STAKABLE_VERSION)
            continue;
        if (meta.nHeight < chainActive.Height() - nRequiredDepth) {
            std::unique_ptr<ZerocoinStake> input(new ZerocoinStake(meta.denom, meta.hashStake));
            listInputs.emplace_back(std::move(input));
        }
    }

    LogPrint(BCLog::STAKING, "%s: FOUND %d STAKABLE ZEROCOINS\n", __func__, listInputs.size());

    return true;
}


/**
 * Call after CreateTransaction unless you want to abort
 */
bool CWallet::CommitTransaction(CTransactionRef tx, mapValue_t mapValue, std::vector<std::pair<std::string, std::string>> orderForm,
        CReserveKey* reservekey, CConnman* connman, CValidationState& state, int computeTime)
{
    {
        LOCK2(cs_main, cs_wallet);

        CWalletTx wtxNew(this, std::move(tx));
        wtxNew.mapValue = std::move(mapValue);
        wtxNew.vOrderForm = std::move(orderForm);
        wtxNew.fTimeReceivedIsTxTime = true;
        wtxNew.fFromMe = true;
        if (computeTime)
            wtxNew.nComputeTime = computeTime;

        if (!wtxNew.tx)
            return error("%s: FIXME wallet transaction is not linked to a tx", __func__);
        LogPrintf("CommitTransaction:\n%s", wtxNew.tx->ToString()); /* Continued */
        {
            // Take key pair from key pool so it won't be used again
            if (reservekey)
                reservekey->KeepKey();

            if (wtxNew.tx->HasBlindedValues())
                setAnonTx.emplace(wtxNew.tx->GetHash());

            // Add tx to wallet, because if it has change it's also ours,
            // otherwise just for transaction history.
            AddToWallet(wtxNew);

            // Notify that old coins are spent
            if (!wtxNew.tx->IsZerocoinSpend()) {
                for (const CTxIn &txin : wtxNew.tx->vin) {
                    if (!mapWallet.count(txin.prevout.hash)) {
                        //LogPrintf("%s: %s ***** FIXMEEEEEEEEEEEEEEEEEEEEE Notify that anon input has been spend\n", __func__, __LINE__);
                        continue;
                    }

                    CWalletTx &coin = mapWallet.at(txin.prevout.hash);
                    coin.BindWallet(this);
                    NotifyTransactionChanged(this, coin.GetHash(), CT_UPDATED);
                }
            }
        }

        // Get the inserted-CWalletTx from mapWallet so that the
        // fInMempool flag is cached properly
        CWalletTx& wtx = mapWallet.at(wtxNew.GetHash());

        // Broadcast
        if (!wtx.AcceptToMemoryPool(maxTxFee, state)) {
            return error("CommitTransaction(): Transaction cannot be broadcast immediately, %s\n", FormatStateMessage(state));
        } else {
            wtx.RelayWalletTransaction(connman);
        }

    }
    return true;
}

DBErrors CWallet::LoadWallet(bool& fFirstRunRet)
{
    LOCK2(cs_main, cs_wallet);

    fFirstRunRet = false;
    DBErrors nLoadWalletRet = WalletBatch(*database,"cr+").LoadWallet(this);
    if (nLoadWalletRet == DBErrors::NEED_REWRITE)
    {
        if (database->Rewrite("\x04pool"))
        {
            setInternalKeyPool.clear();
            setExternalKeyPool.clear();
            m_pool_key_to_index.clear();
            // Note: can't top-up keypool here, because wallet is locked.
            // User will be prompted to unlock wallet the next operation
            // that requires a new key.
        }
    }

    {
        LOCK(cs_KeyStore);
        // This wallet is in its first run if all of these are empty
        fFirstRunRet = mapKeys.empty() && mapCryptedKeys.empty() && mapWatchKeys.empty() && setWatchOnly.empty() && mapScripts.empty() && !IsWalletFlagSet(WALLET_FLAG_DISABLE_PRIVATE_KEYS);
    }

    if (nLoadWalletRet != DBErrors::LOAD_OK)
        return nLoadWalletRet;

    return DBErrors::LOAD_OK;
}

DBErrors CWallet::ZapSelectTx(std::vector<uint256>& vHashIn, std::vector<uint256>& vHashOut)
{
    AssertLockHeld(cs_wallet); // mapWallet
    DBErrors nZapSelectTxRet = WalletBatch(*database,"cr+").ZapSelectTx(vHashIn, vHashOut);
    for (uint256 hash : vHashOut) {
        const auto& it = mapWallet.find(hash);
        wtxOrdered.erase(it->second.m_it_wtxOrdered);
        mapWallet.erase(it);
    }

    if (nZapSelectTxRet == DBErrors::NEED_REWRITE)
    {
        if (database->Rewrite("\x04pool"))
        {
            setInternalKeyPool.clear();
            setExternalKeyPool.clear();
            m_pool_key_to_index.clear();
            // Note: can't top-up keypool here, because wallet is locked.
            // User will be prompted to unlock wallet the next operation
            // that requires a new key.
        }
    }

    if (nZapSelectTxRet != DBErrors::LOAD_OK)
        return nZapSelectTxRet;

    MarkDirty();

    return DBErrors::LOAD_OK;

}

DBErrors CWallet::ZapWalletTx(std::vector<CWalletTx>& vWtx)
{
    DBErrors nZapWalletTxRet = WalletBatch(*database,"cr+").ZapWalletTx(vWtx);
    if (nZapWalletTxRet == DBErrors::NEED_REWRITE)
    {
        if (database->Rewrite("\x04pool"))
        {
            LOCK(cs_wallet);
            setInternalKeyPool.clear();
            setExternalKeyPool.clear();
            m_pool_key_to_index.clear();
            // Note: can't top-up keypool here, because wallet is locked.
            // User will be prompted to unlock wallet the next operation
            // that requires a new key.
        }
    }

    if (nZapWalletTxRet != DBErrors::LOAD_OK)
        return nZapWalletTxRet;

    return DBErrors::LOAD_OK;
}

bool CWallet::SetAddressBook(const CTxDestination& address, const std::string& strName, const std::string& strPurpose, bool bench32) {
    bool fUpdated = false;
    {
        LOCK(cs_wallet); // mapAddressBook
        std::map<CTxDestination, CAddressBookData>::iterator mi = mapAddressBook.find(address);
        fUpdated = mi != mapAddressBook.end();
        mapAddressBook[address].name = strName;
        if (!strPurpose.empty()) /* update purpose only if requested */
            mapAddressBook[address].purpose = strPurpose;
    }
    NotifyAddressBookChanged(this, address, strName, ::IsMine(*this, address) != ISMINE_NO, strPurpose, (fUpdated ? CT_UPDATED : CT_NEW));
    if (!strPurpose.empty() && !WalletBatch(*database).WritePurpose(EncodeDestination(address, bench32), strPurpose))
        return false;
    return WalletBatch(*database).WriteName(EncodeDestination(address,bench32), strName);
}

bool CWallet::SetAddressBook(const CTxDestination& address, const std::string& strName, const std::string& strPurpose)
{
    bool fUpdated = false;
    {
        LOCK(cs_wallet); // mapAddressBook
        std::map<CTxDestination, CAddressBookData>::iterator mi = mapAddressBook.find(address);
        fUpdated = mi != mapAddressBook.end();
        mapAddressBook[address].name = strName;
        if (!strPurpose.empty()) /* update purpose only if requested */
            mapAddressBook[address].purpose = strPurpose;
    }
    NotifyAddressBookChanged(this, address, strName, ::IsMine(*this, address) != ISMINE_NO,
                             strPurpose, (fUpdated ? CT_UPDATED : CT_NEW) );
    if (!strPurpose.empty() && !WalletBatch(*database).WritePurpose(EncodeDestination(address), strPurpose))
        return false;
    return WalletBatch(*database).WriteName(EncodeDestination(address), strName);
}

bool CWallet::DelAddressBook(const CTxDestination& address)
{
    {
        LOCK(cs_wallet); // mapAddressBook

        // Delete destdata tuples associated with address
        std::string strAddress = EncodeDestination(address);
        for (const std::pair<const std::string, std::string> &item : mapAddressBook[address].destdata)
        {
            WalletBatch(*database).EraseDestData(strAddress, item.first);
        }
        mapAddressBook.erase(address);
    }

    NotifyAddressBookChanged(this, address, "", ::IsMine(*this, address) != ISMINE_NO, "", CT_DELETED);

    WalletBatch(*database).ErasePurpose(EncodeDestination(address));
    return WalletBatch(*database).EraseName(EncodeDestination(address));
}

bool CWallet::SetAutoSpendAddress(const CTxDestination& address) {
    return WalletBatch(*database).WriteAutoSpend(EncodeDestination(address,true));
}

bool CWallet::GetAutoSpendAddress(std::string& address)
{
    return WalletBatch(*database).ReadAutoSpend(address);
}

bool CWallet::EraseAutoSpendAddress()
{
    return WalletBatch(*database).EraseAutoSpend();
}

const std::string& CWallet::GetLabelName(const CScript& scriptPubKey) const
{
    CTxDestination address;
    if (ExtractDestination(scriptPubKey, address) && !scriptPubKey.IsUnspendable()) {
        auto mi = mapAddressBook.find(address);
        if (mi != mapAddressBook.end()) {
            return mi->second.name;
        }
    }
    // A scriptPubKey that doesn't have an entry in the address book is
    // associated with the default label ("").
    const static std::string DEFAULT_LABEL_NAME;
    return DEFAULT_LABEL_NAME;
}

/**
 * Mark old keypool keys as used,
 * and generate all new keys
 */
bool CWallet::NewKeyPool()
{
    if (IsWalletFlagSet(WALLET_FLAG_DISABLE_PRIVATE_KEYS)) {
        return false;
    }
    {
        LOCK(cs_wallet);
        WalletBatch batch(*database);

        for (int64_t nIndex : setInternalKeyPool) {
            batch.ErasePool(nIndex);
        }
        setInternalKeyPool.clear();

        for (int64_t nIndex : setExternalKeyPool) {
            batch.ErasePool(nIndex);
        }
        setExternalKeyPool.clear();

        for (int64_t nIndex : set_pre_split_keypool) {
            batch.ErasePool(nIndex);
        }
        set_pre_split_keypool.clear();

        m_pool_key_to_index.clear();

        if (!TopUpKeyPool()) {
            return false;
        }
        WalletLogPrintf("CWallet::NewKeyPool rewrote keypool\n");
    }
    return true;
}

size_t CWallet::KeypoolCountExternalKeys()
{
    AssertLockHeld(cs_wallet); // setExternalKeyPool
    return setExternalKeyPool.size() + set_pre_split_keypool.size();
}

void CWallet::LoadKeyPool(int64_t nIndex, const CKeyPool &keypool)
{
    AssertLockHeld(cs_wallet);
    if (keypool.m_pre_split) {
        set_pre_split_keypool.insert(nIndex);
    } else if (keypool.fInternal) {
        setInternalKeyPool.insert(nIndex);
    } else {
        setExternalKeyPool.insert(nIndex);
    }
    m_max_keypool_index = std::max(m_max_keypool_index, nIndex);
    m_pool_key_to_index[keypool.vchPubKey.GetID()] = nIndex;

    // If no metadata exists yet, create a default with the pool key's
    // creation time. Note that this may be overwritten by actually
    // stored metadata for that key later, which is fine.
    CKeyID keyid = keypool.vchPubKey.GetID();
    if (mapKeyMetadata.count(keyid) == 0)
        mapKeyMetadata[keyid] = CKeyMetadata(keypool.nTime);
}

bool CWallet::TopUpKeyPool(unsigned int kpSize)
{
    if (IsWalletFlagSet(WALLET_FLAG_DISABLE_PRIVATE_KEYS)) {
        return false;
    }
    {
        LOCK(cs_wallet);

        if (IsLocked() || IsUnlockedForStakingOnly())
            return false;

        // Top up key pool
        unsigned int nTargetSize;
        if (kpSize > 0)
            nTargetSize = kpSize;
        else
            nTargetSize = std::max(gArgs.GetArg("-keypool", DEFAULT_KEYPOOL_SIZE), (int64_t) 0);

        // count amount of available keys (internal, external)
        // make sure the keypool of external and internal keys fits the user selected target (-keypool)
        int64_t missingExternal = std::max(std::max((int64_t) nTargetSize, (int64_t) 1) - (int64_t)setExternalKeyPool.size(), (int64_t) 0);
        int64_t missingInternal = std::max(std::max((int64_t) nTargetSize, (int64_t) 1) - (int64_t)setInternalKeyPool.size(), (int64_t) 0);

        if (!CWallet::IsHDEnabled() || !CanSupportFeature(FEATURE_HD_SPLIT))
        {
            // don't create extra internal keys
            missingInternal = 0;
        }
        bool internal = false;
        WalletBatch batch(*database);
        for (int64_t i = missingInternal + missingExternal; i--;)
        {
            if (i < missingInternal) {
                internal = true;
            }

            assert(m_max_keypool_index < std::numeric_limits<int64_t>::max()); // How in the hell did you use so many keys?
            int64_t index = ++m_max_keypool_index;

            CPubKey pubkey(GenerateNewKey(batch, internal));
            if (!batch.WritePool(index, CKeyPool(pubkey, internal))) {
                throw std::runtime_error(std::string(__func__) + ": writing generated key failed");
            }

            if (internal) {
                setInternalKeyPool.insert(index);
            } else {
                setExternalKeyPool.insert(index);
            }
            m_pool_key_to_index[pubkey.GetID()] = index;
        }
        if (missingInternal + missingExternal > 0) {
            WalletLogPrintf("keypool added %d keys (%d internal), size=%u (%u internal)\n", missingInternal + missingExternal, missingInternal, setInternalKeyPool.size() + setExternalKeyPool.size() + set_pre_split_keypool.size(), setInternalKeyPool.size());
        }
    }
    return true;
}

bool CWallet::ReserveKeyFromKeyPool(int64_t& nIndex, CKeyPool& keypool, bool fRequestedInternal)
{
    nIndex = -1;
    keypool.vchPubKey = CPubKey();
    {
        LOCK(cs_wallet);

        if (!IsLocked() && !IsUnlockedForStakingOnly())
            TopUpKeyPool();

        bool fReturningInternal = CWallet::IsHDEnabled() && CanSupportFeature(FEATURE_HD_SPLIT) && fRequestedInternal;
        bool use_split_keypool = set_pre_split_keypool.empty();
        std::set<int64_t>& setKeyPool = use_split_keypool ? (fReturningInternal ? setInternalKeyPool : setExternalKeyPool) : set_pre_split_keypool;

        // Get the oldest key
        if (setKeyPool.empty()) {
            return false;
        }

        WalletBatch batch(*database);

        auto it = setKeyPool.begin();
        nIndex = *it;
        setKeyPool.erase(it);
        if (!batch.ReadPool(nIndex, keypool)) {
            throw std::runtime_error(std::string(__func__) + ": read failed");
        }
        if (!HaveKey(keypool.vchPubKey.GetID())) {
            throw std::runtime_error(std::string(__func__) + ": unknown key in key pool");
        }
        // If the key was pre-split keypool, we don't care about what type it is
        if (use_split_keypool && keypool.fInternal != fReturningInternal) {
            throw std::runtime_error(std::string(__func__) + ": keypool entry misclassified");
        }
        if (!keypool.vchPubKey.IsValid()) {
            throw std::runtime_error(std::string(__func__) + ": keypool entry invalid");
        }

        m_pool_key_to_index.erase(keypool.vchPubKey.GetID());
        WalletLogPrintf("keypool reserve %d\n", nIndex);
    }
    return true;
}

void CWallet::KeepKey(int64_t nIndex)
{
    // Remove from key pool
    WalletBatch batch(*database);
    batch.ErasePool(nIndex);
    WalletLogPrintf("keypool keep %d\n", nIndex);
}

void CWallet::ReturnKey(int64_t nIndex, bool fInternal, const CPubKey& pubkey)
{
    // Return to key pool
    {
        LOCK(cs_wallet);
        if (fInternal) {
            setInternalKeyPool.insert(nIndex);
        } else if (!set_pre_split_keypool.empty()) {
            set_pre_split_keypool.insert(nIndex);
        } else {
            setExternalKeyPool.insert(nIndex);
        }
        m_pool_key_to_index[pubkey.GetID()] = nIndex;
    }
    WalletLogPrintf("keypool return %d\n", nIndex);
}

bool CWallet::GetKeyFromPool(CPubKey& result, bool internal)
{
    if (IsWalletFlagSet(WALLET_FLAG_DISABLE_PRIVATE_KEYS)) {
        return false;
    }

    CKeyPool keypool;
    {
        LOCK(cs_wallet);
        int64_t nIndex;
        if (!ReserveKeyFromKeyPool(nIndex, keypool, internal)) {
            if (IsLocked() || IsUnlockedForStakingOnly()) return false;
            WalletBatch batch(*database);
            result = GenerateNewKey(batch, internal);
            return true;
        }
        KeepKey(nIndex);
        result = keypool.vchPubKey;
    }
    return true;
}

static int64_t GetOldestKeyTimeInPool(const std::set<int64_t>& setKeyPool, WalletBatch& batch) {
    if (setKeyPool.empty()) {
        return GetTime();
    }

    CKeyPool keypool;
    int64_t nIndex = *(setKeyPool.begin());
    if (!batch.ReadPool(nIndex, keypool)) {
        throw std::runtime_error(std::string(__func__) + ": read oldest key in keypool failed");
    }
    assert(keypool.vchPubKey.IsValid());
    return keypool.nTime;
}

int64_t CWallet::GetOldestKeyPoolTime()
{
    LOCK(cs_wallet);

    WalletBatch batch(*database);

    // load oldest key from keypool, get time and return
    int64_t oldestKey = GetOldestKeyTimeInPool(setExternalKeyPool, batch);
    if (CWallet::IsHDEnabled() && CanSupportFeature(FEATURE_HD_SPLIT)) {
        oldestKey = std::max(GetOldestKeyTimeInPool(setInternalKeyPool, batch), oldestKey);
        if (!set_pre_split_keypool.empty()) {
            oldestKey = std::max(GetOldestKeyTimeInPool(set_pre_split_keypool, batch), oldestKey);
        }
    }

    return oldestKey;
}

std::map<CTxDestination, CAmount> CWallet::GetAddressBalances()
{
    std::map<CTxDestination, CAmount> balances;

    {
        LOCK(cs_wallet);
        for (const auto& walletEntry : mapWallet)
        {
            const CWalletTx *pcoin = &walletEntry.second;

            if (!pcoin->IsTrusted())
                continue;

            if (pcoin->IsCoinBase() && pcoin->GetBlocksToMaturity() > 0)
                continue;

            int nDepth = pcoin->GetDepthInMainChain();
            if (nDepth < (pcoin->IsFromMe(ISMINE_ALL) ? 0 : 1))
                continue;

            for (unsigned int i = 0; i < pcoin->tx->vpout.size(); i++)
            {
                CTxDestination addr;
                if (!IsMine(pcoin->tx->vpout[i].get()))
                    continue;
                CScript scriptCheck;
                if (!pcoin->tx->vpout[i]->GetScriptPubKey(scriptCheck))
                    continue;
                if(!ExtractDestination(scriptCheck, addr))
                    continue;

                CAmount n = IsSpent(walletEntry.first, i) ? 0 : pcoin->tx->vpout[i]->GetValue();

                if (!balances.count(addr))
                    balances[addr] = 0;
                balances[addr] += n;
            }
        }
    }

    return balances;
}

std::set< std::set<CTxDestination> > CWallet::GetAddressGroupings()
{
    AssertLockHeld(cs_wallet); // mapWallet
    std::set< std::set<CTxDestination> > groupings;
    std::set<CTxDestination> grouping;

    for (const auto& walletEntry : mapWallet)
    {
        const CWalletTx *pcoin = &walletEntry.second;

        if (pcoin->tx->vin.size() > 0)
        {
            bool any_mine = false;
            // group all input addresses with each other
            for (CTxIn txin : pcoin->tx->vin)
            {
                CTxDestination address;
                if(!IsMine(txin)) /* If this input isn't mine, ignore it */
                    continue;
                CScript scriptCheck;
                auto mi = mapWallet.find(txin.prevout.hash);
                if (mi == mapWallet.end() || !mi->second.tx->vpout[txin.prevout.n]->GetScriptPubKey(scriptCheck))
                    continue;
                if(!ExtractDestination(scriptCheck, address))
                    continue;
                grouping.insert(address);
                any_mine = true;
            }

            // group change with input addresses
            if (any_mine)
            {
               for (auto pout : pcoin->tx->vpout)
                   if (IsChange(pout.get()))
                   {
                       CTxDestination txoutAddr;
                       if(!ExtractDestination(pout, txoutAddr))
                           continue;
                       grouping.insert(txoutAddr);
                   }
            }
            if (grouping.size() > 0)
            {
                groupings.insert(grouping);
                grouping.clear();
            }
        }

        // group lone addrs by themselves
        for (const auto& pout : pcoin->tx->vpout)
            if (IsMine(pout.get()))
            {
                CTxDestination address;
                if(!ExtractDestination(pout, address))
                    continue;
                grouping.insert(address);
                groupings.insert(grouping);
                grouping.clear();
            }
    }

    std::set< std::set<CTxDestination>* > uniqueGroupings; // a set of pointers to groups of addresses
    std::map< CTxDestination, std::set<CTxDestination>* > setmap;  // map addresses to the unique group containing it
    for (std::set<CTxDestination> _grouping : groupings)
    {
        // make a set of all the groups hit by this new group
        std::set< std::set<CTxDestination>* > hits;
        std::map< CTxDestination, std::set<CTxDestination>* >::iterator it;
        for (CTxDestination address : _grouping)
            if ((it = setmap.find(address)) != setmap.end())
                hits.insert((*it).second);

        // merge all hit groups into a new single group and delete old groups
        std::set<CTxDestination>* merged = new std::set<CTxDestination>(_grouping);
        for (std::set<CTxDestination>* hit : hits)
        {
            merged->insert(hit->begin(), hit->end());
            uniqueGroupings.erase(hit);
            delete hit;
        }
        uniqueGroupings.insert(merged);

        // update setmap
        for (CTxDestination element : *merged)
            setmap[element] = merged;
    }

    std::set< std::set<CTxDestination> > ret;
    for (std::set<CTxDestination>* uniqueGrouping : uniqueGroupings)
    {
        ret.insert(*uniqueGrouping);
        delete uniqueGrouping;
    }

    return ret;
}

std::set<CTxDestination> CWallet::GetLabelAddresses(const std::string& label) const
{
    LOCK(cs_wallet);
    std::set<CTxDestination> result;
    for (const std::pair<const CTxDestination, CAddressBookData>& item : mapAddressBook)
    {
        const CTxDestination& address = item.first;
        const std::string& strName = item.second.name;
        if (strName == label)
            result.insert(address);
    }
    return result;
}

bool CReserveKey::GetReservedKey(CPubKey& pubkey, bool internal)
{
    LOCK(pwallet->cs_wallet);
    if (nIndex == -1)
    {
        CKeyPool keypool;
        if (!pwallet->ReserveKeyFromKeyPool(nIndex, keypool, internal)) {
            return false;
        }
        vchPubKey = keypool.vchPubKey;
        fInternal = keypool.fInternal;
    }
    assert(vchPubKey.IsValid());
    pubkey = vchPubKey;
    return true;
}

void CReserveKey::KeepKey()
{
    LOCK(pwallet->cs_wallet);
    if (nIndex != -1) {
        pwallet->KeepKey(nIndex);
    }
    nIndex = -1;
    vchPubKey = CPubKey();
}

void CReserveKey::ReturnKey()
{
    LOCK(pwallet->cs_wallet);
    if (nIndex != -1) {
        pwallet->ReturnKey(nIndex, fInternal, vchPubKey);
    }
    nIndex = -1;
    vchPubKey = CPubKey();
}

void CWallet::MarkReserveKeysAsUsed(int64_t keypool_id)
{
    AssertLockHeld(cs_wallet);
    bool internal = setInternalKeyPool.count(keypool_id);
    if (!internal) assert(setExternalKeyPool.count(keypool_id) || set_pre_split_keypool.count(keypool_id));
    std::set<int64_t> *setKeyPool = internal ? &setInternalKeyPool : (set_pre_split_keypool.empty() ? &setExternalKeyPool : &set_pre_split_keypool);
    auto it = setKeyPool->begin();

    WalletBatch batch(*database);
    while (it != std::end(*setKeyPool)) {
        const int64_t& index = *(it);
        if (index > keypool_id) break; // set*KeyPool is ordered

        CKeyPool keypool;
        if (batch.ReadPool(index, keypool)) { //TODO: This should be unnecessary
            m_pool_key_to_index.erase(keypool.vchPubKey.GetID());
        }
        LearnAllRelatedScripts(keypool.vchPubKey);
        batch.ErasePool(index);
        WalletLogPrintf("keypool index %d removed\n", index);
        it = setKeyPool->erase(it);
    }
}

void CWallet::GetScriptForMiningReserveKey(std::shared_ptr<CReserveScript> &script)
{
    std::shared_ptr<CReserveKey> rKey = std::make_shared<CReserveKey>(this);
    CPubKey pubkey;
    if (!rKey->GetReservedKey(pubkey))
        return;

    script = rKey;
    script->reserveScript = CScript() << ToByteVector(pubkey) << OP_CHECKSIG;
}

void CWallet::GetScriptForMining(std::shared_ptr<CReserveScript> &script)
{
    std::string sAddress = gArgs.GetArg("-miningaddress", "");
    if (!sAddress.empty()) {
        CTxDestination dest = DecodeDestination(sAddress);

        script = std::make_shared<CReserveScript>();
        script->reserveScript = GetScriptForDestination(dest);
        return;
    }
    return GetScriptForMiningReserveKey(script);
}

void CWallet::LockCoin(const COutPoint& output)
{
    AssertLockHeld(cs_wallet); // setLockedCoins
    setLockedCoins.insert(output);
}

void CWallet::UnlockCoin(const COutPoint& output)
{
    AssertLockHeld(cs_wallet); // setLockedCoins
    setLockedCoins.erase(output);
}

void CWallet::UnlockAllCoins()
{
    AssertLockHeld(cs_wallet); // setLockedCoins
    setLockedCoins.clear();
}

bool CWallet::IsLockedCoin(uint256 hash, unsigned int n) const
{
    AssertLockHeld(cs_wallet); // setLockedCoins
    COutPoint outpt(hash, n);

    return (setLockedCoins.count(outpt) > 0);
}

void CWallet::ListLockedCoins(std::vector<COutPoint>& vOutpts) const
{
    AssertLockHeld(cs_wallet); // setLockedCoins
    for (std::set<COutPoint>::iterator it = setLockedCoins.begin();
         it != setLockedCoins.end(); it++) {
        COutPoint outpt = (*it);
        vOutpts.push_back(outpt);
    }
}

/** @} */ // end of Actions

void CWallet::GetKeyBirthTimes(std::map<CTxDestination, int64_t> &mapKeyBirth) const {
    AssertLockHeld(cs_wallet); // mapKeyMetadata
    mapKeyBirth.clear();

    // get birth times for keys with metadata
    for (const auto& entry : mapKeyMetadata) {
        if (entry.second.nCreateTime) {
            mapKeyBirth[entry.first] = entry.second.nCreateTime;
        }
    }

    // map in which we'll infer heights of other keys
    CBlockIndex *pindexMax = chainActive[std::max(0, chainActive.Height() - 144)]; // the tip can be reorganized; use a 144-block safety margin
    std::map<CKeyID, CBlockIndex*> mapKeyFirstBlock;
    for (const CKeyID &keyid : GetKeys()) {
        if (mapKeyBirth.count(keyid) == 0)
            mapKeyFirstBlock[keyid] = pindexMax;
    }

    // if there are no such keys, we're done
    if (mapKeyFirstBlock.empty())
        return;

    // find first block that affects those keys, if there are any left
    std::vector<CKeyID> vAffected;
    for (const auto& entry : mapWallet) {
        // iterate over all wallet transactions...
        const CWalletTx &wtx = entry.second;
        CBlockIndex* pindex = LookupBlockIndex(wtx.hashBlock);
        if (pindex && chainActive.Contains(pindex)) {
            // ... which are already in a block
            int nHeight = pindex->nHeight;
            for (const auto& pout : wtx.tx->vpout) {
                CScript scriptCheck;
                if (!pout->GetScriptPubKey(scriptCheck))
                    continue;
                // iterate over all their outputs
                CAffectedKeysVisitor(*this, vAffected).Process(scriptCheck);
                for (const CKeyID &keyid : vAffected) {
                    // ... and all their affected keys
                    std::map<CKeyID, CBlockIndex*>::iterator rit = mapKeyFirstBlock.find(keyid);
                    if (rit != mapKeyFirstBlock.end() && nHeight < rit->second->nHeight)
                        rit->second = pindex;
                }
                vAffected.clear();
            }
        }
    }

    // Extract block timestamps for those keys
    for (const auto& entry : mapKeyFirstBlock)
        mapKeyBirth[entry.first] = entry.second->GetBlockTime() - TIMESTAMP_WINDOW; // block times can be 2h off
}

/**
 * Compute smart timestamp for a transaction being added to the wallet.
 *
 * Logic:
 * - If sending a transaction, assign its timestamp to the current time.
 * - If receiving a transaction outside a block, assign its timestamp to the
 *   current time.
 * - If receiving a block with a future timestamp, assign all its (not already
 *   known) transactions' timestamps to the current time.
 * - If receiving a block with a past timestamp, before the most recent known
 *   transaction (that we care about), assign all its (not already known)
 *   transactions' timestamps to the same timestamp as that most-recent-known
 *   transaction.
 * - If receiving a block with a past timestamp, but after the most recent known
 *   transaction, assign all its (not already known) transactions' timestamps to
 *   the block time.
 *
 * For more information see CWalletTx::nTimeSmart,
 * https://bitcointalk.org/?topic=54527, or
 * https://github.com/bitcoin/bitcoin/pull/1393.
 */
unsigned int CWallet::ComputeTimeSmart(const CWalletTx& wtx) const
{
    unsigned int nTimeSmart = wtx.nTimeReceived;
    if (!wtx.hashUnset()) {
        if (const CBlockIndex* pindex = LookupBlockIndex(wtx.hashBlock)) {
            int64_t latestNow = wtx.nTimeReceived;
            int64_t latestEntry = 0;

            // Tolerate times up to the last timestamp in the wallet not more than 5 minutes into the future
            int64_t latestTolerated = latestNow + 300;
            const TxItems& txOrdered = wtxOrdered;
            for (auto it = txOrdered.rbegin(); it != txOrdered.rend(); ++it) {
                CWalletTx* const pwtx = it->second;
                if (pwtx == &wtx) {
                    continue;
                }
                int64_t nSmartTime;
                nSmartTime = pwtx->nTimeSmart;
                if (!nSmartTime) {
                    nSmartTime = pwtx->nTimeReceived;
                }
                if (nSmartTime <= latestTolerated) {
                    latestEntry = nSmartTime;
                    if (nSmartTime > latestNow) {
                        latestNow = nSmartTime;
                    }
                    break;
                }
            }

            int64_t blocktime = pindex->GetBlockTime();
            nTimeSmart = std::max(latestEntry, std::min(blocktime, latestNow));
        } else {
            WalletLogPrintf("%s: found %s in block %s not in index\n", __func__, wtx.GetHash().ToString(), wtx.hashBlock.ToString());
        }
    }
    return nTimeSmart;
}

bool CWallet::AddDestData(const CTxDestination &dest, const std::string &key, const std::string &value)
{
    if (boost::get<CNoDestination>(&dest))
        return false;

    mapAddressBook[dest].destdata.insert(std::make_pair(key, value));
    return WalletBatch(*database).WriteDestData(EncodeDestination(dest), key, value);
}

bool CWallet::EraseDestData(const CTxDestination &dest, const std::string &key)
{
    if (!mapAddressBook[dest].destdata.erase(key))
        return false;
    return WalletBatch(*database).EraseDestData(EncodeDestination(dest), key);
}

void CWallet::LoadDestData(const CTxDestination &dest, const std::string &key, const std::string &value)
{
    mapAddressBook[dest].destdata.insert(std::make_pair(key, value));
}

bool CWallet::GetDestData(const CTxDestination &dest, const std::string &key, std::string *value) const
{
    std::map<CTxDestination, CAddressBookData>::const_iterator i = mapAddressBook.find(dest);
    if(i != mapAddressBook.end())
    {
        CAddressBookData::StringMap::const_iterator j = i->second.destdata.find(key);
        if(j != i->second.destdata.end())
        {
            if(value)
                *value = j->second;
            return true;
        }
    }
    return false;
}

std::vector<std::string> CWallet::GetDestValues(const std::string& prefix) const
{
    LOCK(cs_wallet);
    std::vector<std::string> values;
    for (const auto& address : mapAddressBook) {
        for (const auto& data : address.second.destdata) {
            if (!data.first.compare(0, prefix.size(), prefix)) {
                values.emplace_back(data.second);
            }
        }
    }
    return values;
}

// CWallet::AutoZeromint() gets called with each new incoming block
void CWallet::AutoZeromint()
{
    if (gArgs.GetBoolArg("-automintoff", false)) {
        return;
    }

    // After sync wait even more to reduce load when wallet was just started
    int64_t nWaitTime = GetAdjustedTime() - nAutoMintStartupTime;
    if (nWaitTime < AUTOMINT_DELAY){
        LogPrint(BCLog::SELECTCOINS, "CWallet::AutoZeromint(): time since sync-completion or last Automint (%ld sec) < default waiting time (%ld sec). Waiting again...\n", nWaitTime, AUTOMINT_DELAY);
        return;
    }

    CAmount nZerocoinBalance = GetZerocoinBalance(false); //false includes both pending and mature Zerocoins. Need total balance for this so nothing is overminted.
    CAmount nMintAmount = 0;
    CAmount nToMintAmount = 0;

    std::vector<COutput> vOutputs;
    CCoinControl coinControl;
    CAmount nBalance = GetMintableBalance(vOutputs); // won't consider locked outputs or basecoin address

    CAmount nSelectedCoinBalance = 0;
    BalanceList bal;
    int inputtype = OUTPUT_NULL;
    if (GetMainWallet()->GetBalances(bal)) {
        if (bal.nRingCT > libzerocoin::ZerocoinDenominationToAmount(libzerocoin::CoinDenomination::ZQ_TEN) && chainActive.Tip()->nAnonOutputs > 20) {
            inputtype = OUTPUT_RINGCT;
            nSelectedCoinBalance = bal.nRingCT;
        }
        else if (bal.nCT > libzerocoin::ZerocoinDenominationToAmount(libzerocoin::CoinDenomination::ZQ_TEN)) {
            if (bal.nCT > nSelectedCoinBalance) {
                inputtype = OUTPUT_CT;
                nSelectedCoinBalance = bal.nCT;
            }
        }
    }
    if (nBalance > libzerocoin::ZerocoinDenominationToAmount(libzerocoin::CoinDenomination::ZQ_TEN)) {
        if (nBalance > nSelectedCoinBalance) {
            inputtype = OUTPUT_STANDARD;
            nSelectedCoinBalance = nBalance;
        }
    }

    if (inputtype == OUTPUT_NULL) {
        LogPrint(BCLog::SELECTCOINS, "CWallet::AutoZeromint(): all available basecoin (%ld) available ringct (%ld) available ct (%ld) ringctoutputcoount (%ld) too small for minting Zerocoin\n", nBalance, bal.nRingCT, bal.nCT, chainActive.Tip()->nAnonOutputs);
        return;
    }

    double dPercentage = 100 * (double)nZerocoinBalance / (double)(nZerocoinBalance + nBalance);

    // Check if minting is actually needed
    if(dPercentage == nZeromintPercentage){
        //LogPrintf("CWallet::AutoZeromint() @block %ld: percentage of existing Zerocoin (%lf%%) already >= configured percentage (%d%%). No minting needed...\n",
        //       chainActive.Tip()->nHeight, dPercentage, nZeromintPercentage);
        return;
    }

    // Zerocoin amount needed for the target percentage
    nToMintAmount = ((nZerocoinBalance + nSelectedCoinBalance) * nZeromintPercentage / 100);

    // Zerocoin amount missing from target (must be minted)
    nToMintAmount = (nToMintAmount - nZerocoinBalance) / COIN;

    // Use the biggest denomination smaller than the needed Zerocoin. We'll only mint exact denomination to make minting faster.
    // Exception: for big amounts use 11110 (11110 = 1*10000 + 1*1000 + 1*100 + 1*10) to create all
    // possible denominations to avoid having 10000 denominations only.
    // If a preferred denomination is used (means nPreferredDenom != 0) do nothing until we have enough Veilcoin to mint this denomination

    if (nPreferredDenom > 0){
        if (nToMintAmount >= nPreferredDenom)
            nToMintAmount = nPreferredDenom;  // Enough coins => mint preferred denomination
        else
            nToMintAmount = 0;                // Not enough coins => do nothing and wait for more coins

        // Only one denom per cycle
        if (nToMintAmount >= ZQ_11110){
            nMintAmount = ZQ_11110;
        } else if (nToMintAmount >= libzerocoin::CoinDenomination::ZQ_TEN_THOUSAND){
            nMintAmount = libzerocoin::CoinDenomination::ZQ_TEN_THOUSAND;
        } else if (nToMintAmount >= libzerocoin::CoinDenomination::ZQ_ONE_THOUSAND){
            nMintAmount = libzerocoin::CoinDenomination::ZQ_ONE_THOUSAND;
        } else if (nToMintAmount >= libzerocoin::CoinDenomination::ZQ_ONE_HUNDRED){
            nMintAmount = libzerocoin::CoinDenomination::ZQ_ONE_HUNDRED;
        } else if (nToMintAmount >= libzerocoin::CoinDenomination::ZQ_TEN){
            nMintAmount = libzerocoin::CoinDenomination::ZQ_TEN;
        } else {
            nMintAmount = 0;
        }

    }else{
        // nPreferredDenom is -1 when the automatic full mint is selected
        if(nPreferredDenom == -1){
            nMintAmount = nSelectedCoinBalance / COIN; // We are are multiplying by COIN later, so divide it
            nMintAmount = nMintAmount - (nMintAmount % 10); // Make sure this value is divisible by 10
        } else
            nMintAmount = 0;
    }

    if (nMintAmount > 0){

        if (inputtype == OUTPUT_STANDARD) {
            if (nSelectedCoinBalance < nMintAmount * COIN) {
                LogPrintf("%s : Can't auto mint, mintable basecoin value (%lu) is less than the selected denomination: (%ld)\n", __func__, nSelectedCoinBalance, nMintAmount);
                return;
            }
        } else if (inputtype == OUTPUT_RINGCT || inputtype == OUTPUT_CT) {
            if (nSelectedCoinBalance < nMintAmount * COIN) {
                std::string strType = "ringct";
                if (inputtype == OUTPUT_CT)
                    strType = "ct";

                LogPrintf("%s : Can't auto mint, mintable %s value (%lu) is less than the selected denomination: (%ld)\n", __func__, strType, nSelectedCoinBalance, nMintAmount);
                return;
            }
        }

        // Instead of making one mint at a time, try to make up to 9 at a time if the balance supports it
        int div = nSelectedCoinBalance / (nMintAmount * COIN);
        if (div < 1)
            div = 1;
        if (div > 9)
            div = 9;
        nMintAmount = nMintAmount * div;

        CWalletTx wtx(NULL, NULL);
        std::vector<CDeterministicMint> vDMints;

        string strError = GetMainWallet()->MintZerocoin(nMintAmount*COIN, wtx, vDMints, OutputTypes(inputtype), nullptr);

        // Return if something went wrong during minting
        if (strError != ""){
            LogPrintf("%s: auto minting failed with error: %s\n", __func__, strError);
            return;
        }
        nZerocoinBalance = GetZerocoinBalance(false);
        std::vector<COutput> vOutputs;
        CAmount nNewBalance = 0;
        if (inputtype == OUTPUT_STANDARD) {
            nNewBalance = GetMintableBalance(vOutputs);
        } else {
            GetBalances(bal);
            if (inputtype == OUTPUT_RINGCT) {
                nNewBalance = bal.nRingCT;
            } else if (inputtype == OUTPUT_CT)
                nNewBalance = bal.nCT;
        }
        dPercentage = 100 * (double)nZerocoinBalance / (double)(nZerocoinBalance + nNewBalance);
        LogPrintf("CWallet::AutoZeromint() @ block %ld: successfully minted %ld Zerocoin. Current percentage of Zerocoin: %lf%%\n",
                  chainActive.Tip()->nHeight, nMintAmount, dPercentage);
        // Re-adjust startup time to delay next Automint for 5 minutes
        nAutoMintStartupTime = GetAdjustedTime();
    }
    else {
        WalletLogPrintf("CWallet::AutoZeromint(): Nothing minted because either not enough funds available or the requested denomination size (%d) is not yet reached.\n", nPreferredDenom);
    }
}

void CWallet::MarkPreSplitKeys()
{
    WalletBatch batch(*database);
    for (auto it = setExternalKeyPool.begin(); it != setExternalKeyPool.end();) {
        int64_t index = *it;
        CKeyPool keypool;
        if (!batch.ReadPool(index, keypool)) {
            throw std::runtime_error(std::string(__func__) + ": read keypool entry failed");
        }
        keypool.m_pre_split = true;
        if (!batch.WritePool(index, keypool)) {
            throw std::runtime_error(std::string(__func__) + ": writing modified keypool entry failed");
        }
        set_pre_split_keypool.insert(index);
        it = setExternalKeyPool.erase(it);
    }
}

bool CWallet::Verify(std::string wallet_file, bool salvage_wallet, std::string& error_string, std::string& warning_string)
{
    // Do some checking on wallet path. It should be either a:
    //
    // 1. Path where a directory can be created.
    // 2. Path to an existing directory.
    // 3. Path to a symlink to a directory.
    // 4. For backwards compatibility, the name of a data file in -walletdir.
    LOCK(cs_wallets);
    fs::path wallet_path = fs::absolute(wallet_file, GetWalletDir());
    fs::file_type path_type = fs::symlink_status(wallet_path).type();
    if (!(path_type == fs::file_not_found || path_type == fs::directory_file ||
          (path_type == fs::symlink_file && fs::is_directory(wallet_path)) ||
          (path_type == fs::regular_file && fs::path(wallet_file).filename() == wallet_file))) {
        error_string = strprintf(
              "Invalid -wallet path '%s'. -wallet path should point to a directory where wallet.dat and "
              "database/log.?????????? files can be stored, a location where such a directory could be created, "
              "or (for backwards compatibility) the name of an existing data file in -walletdir (%s)",
              wallet_file, GetWalletDir());
        return false;
    }

    // Make sure that the wallet path doesn't clash with an existing wallet path
    for (auto wallet : GetWallets()) {
        if (fs::absolute(wallet->GetName(), GetWalletDir()) == wallet_path) {
            error_string = strprintf("Error loading wallet %s. Duplicate -wallet filename specified.", wallet_file);
            return false;
        }
    }

    try {
        if (!WalletBatch::VerifyEnvironment(wallet_path, error_string)) {
            return false;
        }
    } catch (const fs::filesystem_error& e) {
        error_string = strprintf("Error loading wallet %s. %s", wallet_file, e.what());
        return false;
    }

    if (salvage_wallet) {
        // Recover readable keypairs:
        CWallet dummyWallet("dummy", WalletDatabase::CreateDummy());
        std::string backup_filename;
        if (!WalletBatch::Recover(wallet_path, (void *)&dummyWallet, WalletBatch::RecoverKeysOnlyFilter, backup_filename)) {
            return false;
        }
    }

    return WalletBatch::VerifyDatabaseFile(wallet_path, warning_string, error_string);
}

std::shared_ptr<CWallet> CWallet::CreateWalletFromFile(const std::string& name, const fs::path& path, uint64_t wallet_creation_flags, uint512* pseed)
{
    const std::string& walletFile = name;

    // needed to restore wallet transaction meta data after -zapwallettxes
    std::vector<CWalletTx> vWtx;

    if (gArgs.GetBoolArg("-zapwallettxes", false)) {
        uiInterface.InitMessage(_("Zapping all transactions from wallet..."));

        std::unique_ptr<CWallet> tempWallet = MakeUnique<CWallet>(name, WalletDatabase::Create(path));
        DBErrors nZapWalletRet = tempWallet->ZapWalletTx(vWtx);
        if (nZapWalletRet != DBErrors::LOAD_OK) {
            InitError(strprintf(_("Error loading %s: Wallet corrupted"), walletFile));
            return nullptr;
        }
    }

    uiInterface.InitMessage(_("Loading wallet..."));

    int64_t nStart = GetTimeMillis();
    bool fFirstRun = true;
    // TODO: Can't use std::make_shared because we need a custom deleter but
    // should be possible to use std::allocate_shared.
    std::shared_ptr<CWallet> walletInstance(std::shared_ptr<CWallet>(new CWallet(name, WalletDatabase::Create(path)), ReleaseWallet));

    fs::path backupDir = GetDataDir() / "backups";
    if (!fs::exists(backupDir)) {
        LogPrintf("%s: Backups folder does not exist. Will try to create.\n", __func__);
        // Always create backup folder to not confuse the operating system's file browser
        fs::create_directories(backupDir);
    }

    DBErrors nLoadWalletRet = walletInstance->LoadWallet(fFirstRun);
    if (nLoadWalletRet != DBErrors::LOAD_OK)
    {
        if (nLoadWalletRet == DBErrors::CORRUPT) {
            InitError(strprintf(_("Error loading %s: Wallet corrupted"), walletFile));
            return nullptr;
        }
        else if (nLoadWalletRet == DBErrors::NONCRITICAL_ERROR)
        {
            InitWarning(strprintf(_("Error reading %s! All keys read correctly, but transaction data"
                                         " or address book entries might be missing or incorrect."),
                walletFile));
        }
        else if (nLoadWalletRet == DBErrors::TOO_NEW) {
            InitError(strprintf(_("Error loading %s: Wallet requires newer version of %s"), walletFile, _(PACKAGE_NAME)));
            return nullptr;
        }
        else if (nLoadWalletRet == DBErrors::NEED_REWRITE)
        {
            InitError(strprintf(_("Wallet needed to be rewritten: restart %s to complete"), _(PACKAGE_NAME)));
            return nullptr;
        }
        else {
            InitError(strprintf(_("Error loading %s"), walletFile));
            return nullptr;
        }
    }

    int prev_version = walletInstance->nWalletVersion;
    if (gArgs.GetBoolArg("-upgradewallet", fFirstRun))
    {
        int nMaxVersion = gArgs.GetArg("-upgradewallet", 0);
        if (nMaxVersion == 0) // the -upgradewallet without argument case
        {
            walletInstance->WalletLogPrintf("Performing wallet upgrade to %i\n", FEATURE_LATEST);
            nMaxVersion = FEATURE_LATEST;
            walletInstance->SetMinVersion(FEATURE_LATEST); // permanently upgrade the wallet immediately
        }
        else
            walletInstance->WalletLogPrintf("Allowing wallet upgrade up to %i\n", nMaxVersion);
        if (nMaxVersion < walletInstance->GetVersion())
        {
            InitError(_("Cannot downgrade wallet"));
            return nullptr;
        }
        walletInstance->SetMaxVersion(nMaxVersion);
    }

    // Upgrade to HD if explicit upgrade
    if (gArgs.GetBoolArg("-upgradewallet", false)) {
        LOCK(walletInstance->cs_wallet);

        // Do not upgrade versions to any version between HD_SPLIT and FEATURE_PRE_SPLIT_KEYPOOL unless already supporting HD_SPLIT
        int max_version = walletInstance->nWalletVersion;
        if (!walletInstance->CanSupportFeature(FEATURE_HD_SPLIT) && max_version >=FEATURE_HD_SPLIT && max_version < FEATURE_PRE_SPLIT_KEYPOOL) {
            InitError(_("Cannot upgrade a non HD split wallet without upgrading to support pre split keypool. Please use -upgradewallet=169900 or -upgradewallet with no version specified."));
            return nullptr;
        }

        bool hd_upgrade = false;
        bool split_upgrade = false;
        if (walletInstance->CanSupportFeature(FEATURE_HD) && !walletInstance->IsHDEnabled()) {
            walletInstance->WalletLogPrintf("Upgrading wallet to HD\n");
            walletInstance->SetMinVersion(FEATURE_HD);

            // generate a new master key
            CPubKey masterPubKey = walletInstance->GenerateNewSeed();
            walletInstance->SetHDSeed(masterPubKey);
            hd_upgrade = true;
        }
        // Upgrade to HD chain split if necessary
        if (walletInstance->CanSupportFeature(FEATURE_HD_SPLIT)) {
            walletInstance->WalletLogPrintf("Upgrading wallet to use HD chain split\n");
            walletInstance->SetMinVersion(FEATURE_PRE_SPLIT_KEYPOOL);
            split_upgrade = FEATURE_HD_SPLIT > prev_version;
        }
        // Mark all keys currently in the keypool as pre-split
        if (split_upgrade) {
            walletInstance->MarkPreSplitKeys();
        }
        // Regenerate the keypool if upgraded to HD
        if (hd_upgrade) {
            if (!walletInstance->TopUpKeyPool()) {
                InitError(_("Unable to generate keys"));
                return nullptr;
            }
        }
    }

    if (fFirstRun || pseed)
    {
        // ensure this wallet.dat can only be opened by clients supporting HD with chain split and expects no default key
        if (!gArgs.GetBoolArg("-usehd", true)) {
            InitError(strprintf(_("Error creating %s: You can't create non-HD wallets with this version."), walletFile));
            return nullptr;
        }
        walletInstance->SetMinVersion(FEATURE_LATEST);

        if ((wallet_creation_flags & WALLET_FLAG_DISABLE_PRIVATE_KEYS)) {
            //selective allow to set flags
            walletInstance->SetWalletFlag(WALLET_FLAG_DISABLE_PRIVATE_KEYS);
        } else {
            // generate a new seed
            if (pseed) {
                walletInstance->SetHDSeed_512(*pseed);
            } else {
                CPubKey seed = walletInstance->GenerateNewSeed();
                walletInstance->SetHDSeed(seed);
            }
        }

        // Top up the keypool
        if (!walletInstance->IsWalletFlagSet(WALLET_FLAG_DISABLE_PRIVATE_KEYS) && !walletInstance->TopUpKeyPool()) {
            InitError(_("Unable to generate initial keys"));
            return nullptr;
        }

        walletInstance->ChainStateFlushed(chainActive.GetLocator());
    } else if (wallet_creation_flags & WALLET_FLAG_DISABLE_PRIVATE_KEYS) {
        // Make it impossible to disable private keys after creation
        InitError(strprintf(_("Error loading %s: Private keys can only be disabled during creation"), walletFile));
        return NULL;
    } else if (walletInstance->IsWalletFlagSet(WALLET_FLAG_DISABLE_PRIVATE_KEYS)) {
        LOCK(walletInstance->cs_KeyStore);
        if (!walletInstance->mapKeys.empty() || !walletInstance->mapCryptedKeys.empty()) {
            InitWarning(strprintf(_("Warning: Private keys detected in wallet {%s} with disabled private keys"), walletFile));
        }
    } else if (gArgs.IsArgSet("-usehd")) {
        bool useHD = gArgs.GetBoolArg("-usehd", true);
        if (walletInstance->IsHDEnabled() && !useHD) {
            InitError(strprintf(_("Error loading %s: You can't disable HD on an already existing HD wallet"), walletFile));
            return nullptr;
        }
        if (!walletInstance->IsHDEnabled() && useHD) {
            InitError(strprintf(_("Error loading %s: You can't enable HD on an already existing non-HD wallet"), walletFile));
            return nullptr;
        }
    }

    if (!gArgs.GetArg("-addresstype", "").empty() && !ParseOutputType(gArgs.GetArg("-addresstype", ""), walletInstance->m_default_address_type)) {
        InitError(strprintf("Unknown address type '%s'", gArgs.GetArg("-addresstype", "")));
        return nullptr;
    }

    if (!gArgs.GetArg("-changetype", "").empty() && !ParseOutputType(gArgs.GetArg("-changetype", ""), walletInstance->m_default_change_type)) {
        InitError(strprintf("Unknown change type '%s'", gArgs.GetArg("-changetype", "")));
        return nullptr;
    }

    if (gArgs.IsArgSet("-mintxfee")) {
        CAmount n = 0;
        if (!ParseMoney(gArgs.GetArg("-mintxfee", ""), n) || 0 == n) {
            InitError(AmountErrMsg("mintxfee", gArgs.GetArg("-mintxfee", "")));
            return nullptr;
        }
        if (n > HIGH_TX_FEE_PER_KB) {
            InitWarning(AmountHighWarn("-mintxfee") + " " +
                        _("This is the minimum transaction fee you pay on every transaction."));
        }
        walletInstance->m_min_fee = CFeeRate(n);
    }

    walletInstance->m_allow_fallback_fee = Params().IsFallbackFeeEnabled();
    if (gArgs.IsArgSet("-fallbackfee")) {
        CAmount nFeePerK = 0;
        if (!ParseMoney(gArgs.GetArg("-fallbackfee", ""), nFeePerK)) {
            InitError(strprintf(_("Invalid amount for -fallbackfee=<amount>: '%s'"), gArgs.GetArg("-fallbackfee", "")));
            return nullptr;
        }
        if (nFeePerK > HIGH_TX_FEE_PER_KB) {
            InitWarning(AmountHighWarn("-fallbackfee") + " " +
                        _("This is the transaction fee you may pay when fee estimates are not available."));
        }
        walletInstance->m_fallback_fee = CFeeRate(nFeePerK);
        walletInstance->m_allow_fallback_fee = nFeePerK != 0; //disable fallback fee in case value was set to 0, enable if non-null value
    }
    if (gArgs.IsArgSet("-discardfee")) {
        CAmount nFeePerK = 0;
        if (!ParseMoney(gArgs.GetArg("-discardfee", ""), nFeePerK)) {
            InitError(strprintf(_("Invalid amount for -discardfee=<amount>: '%s'"), gArgs.GetArg("-discardfee", "")));
            return nullptr;
        }
        if (nFeePerK > HIGH_TX_FEE_PER_KB) {
            InitWarning(AmountHighWarn("-discardfee") + " " +
                        _("This is the transaction fee you may discard if change is smaller than dust at this level"));
        }
        walletInstance->m_discard_rate = CFeeRate(nFeePerK);
    }
    if (gArgs.IsArgSet("-paytxfee")) {
        CAmount nFeePerK = 0;
        if (!ParseMoney(gArgs.GetArg("-paytxfee", ""), nFeePerK)) {
            InitError(AmountErrMsg("paytxfee", gArgs.GetArg("-paytxfee", "")));
            return nullptr;
        }
        if (nFeePerK > HIGH_TX_FEE_PER_KB) {
            InitWarning(AmountHighWarn("-paytxfee") + " " +
                        _("This is the transaction fee you will pay if you send a transaction."));
        }
        walletInstance->m_pay_tx_fee = CFeeRate(nFeePerK, 1000);
        if (walletInstance->m_pay_tx_fee < ::minRelayTxFee) {
            InitError(strprintf(_("Invalid amount for -paytxfee=<amount>: '%s' (must be at least %s)"),
                gArgs.GetArg("-paytxfee", ""), ::minRelayTxFee.ToString()));
            return nullptr;
        }
    }
    walletInstance->m_confirm_target = gArgs.GetArg("-txconfirmtarget", DEFAULT_TX_CONFIRM_TARGET);
    walletInstance->m_spend_zero_conf_change = gArgs.GetBoolArg("-spendzeroconfchange", DEFAULT_SPEND_ZEROCONF_CHANGE);
    walletInstance->m_signal_rbf = gArgs.GetBoolArg("-walletrbf", DEFAULT_WALLET_RBF);

    walletInstance->WalletLogPrintf("Wallet completed loading in %15dms\n", GetTimeMillis() - nStart);

    // Try to top up keypool. No-op if the wallet is locked.
    walletInstance->TopUpKeyPool();

    LOCK(cs_main);

    CBlockIndex *pindexRescan = chainActive.Genesis();
    if (!gArgs.GetBoolArg("-rescan", false))
    {
        WalletBatch batch(*walletInstance->database);
        CBlockLocator locator;
        if (batch.ReadBestBlock(locator))
            pindexRescan = FindForkInGlobalIndex(chainActive, locator);
    }

    walletInstance->m_last_block_processed = chainActive.Tip();

    //Load zerocoin wallet
    CzWallet* zwallet = new CzWallet(walletInstance.get());
    walletInstance->setZWallet(zwallet);
    if (fFirstRun || pseed) {
        CKey keyTemp;
        if (!walletInstance->GetZerocoinSeed(keyTemp))
            throw std::runtime_error(strprintf("%s: could not get zerocoin seed on wallet load", __func__));
        zwallet->SetMasterSeed(keyTemp);
        LogPrintf("Set zerocoin master seed to keyid %s\n", keyTemp.GetPubKey().GetID().GetHex());
    }

    //Load Anon wallet
    AnonWallet* pAnonWallet = new AnonWallet(walletInstance, "anonwallet", walletInstance->database);
    if (fFirstRun || pseed) {
        CExtKey extMasterAnon;
        if (!walletInstance->GetAnonWalletSeed(extMasterAnon))
            throw std::runtime_error(strprintf("%s: could not get anon wallet seed on wallet load", __func__));
        assert(pAnonWallet->Initialise(&extMasterAnon));
        LogPrintf("%s: loaded new Anon wallet", __func__);
    } else {
        assert(pAnonWallet->Initialise());
    }

    walletInstance->SetAnonWallet(pAnonWallet);

    if (chainActive.Tip() && chainActive.Tip() != pindexRescan)
    {
        //We can't rescan beyond non-pruned blocks, stop and throw an error
        //this might happen if a user uses an old wallet within a pruned node
        // or if he ran -disablewallet for a longer time, then decided to re-enable
        if (fPruneMode)
        {
            CBlockIndex *block = chainActive.Tip();
            while (block && block->pprev && (block->pprev->nStatus & BLOCK_HAVE_DATA) && block->pprev->nTx > 0 && pindexRescan != block)
                block = block->pprev;

            if (pindexRescan != block) {
                InitError(_("Prune: last wallet synchronisation goes beyond pruned data. You need to -reindex (download the whole blockchain again in case of pruned node)"));
                return nullptr;
            }
        }

        uiInterface.InitMessage(_("Rescanning..."));
        walletInstance->WalletLogPrintf("Rescanning last %i blocks (from block %i)...\n", chainActive.Height() - pindexRescan->nHeight, pindexRescan->nHeight);

        // No need to read and scan block if block was created before
        // our wallet birthday (as adjusted for block time variability)
        while (pindexRescan && walletInstance->nTimeFirstKey && (pindexRescan->GetBlockTime() < (walletInstance->nTimeFirstKey - TIMESTAMP_WINDOW))) {
            pindexRescan = chainActive.Next(pindexRescan);
        }

        nStart = GetTimeMillis();
        {
            WalletRescanReserver reserver(walletInstance.get());
            if (!reserver.reserve()) {
                InitError(_("Failed to rescan the wallet during initialization"));
                return nullptr;
            }
            walletInstance->ScanForWalletTransactions(pindexRescan, nullptr, reserver, true);
        }
        walletInstance->WalletLogPrintf("Rescan completed in %15dms\n", GetTimeMillis() - nStart);
        walletInstance->ChainStateFlushed(chainActive.GetLocator());
        walletInstance->database->IncrementUpdateCounter();

        // Restore wallet transaction metadata after -zapwallettxes=1
        if (gArgs.GetBoolArg("-zapwallettxes", false) && gArgs.GetArg("-zapwallettxes", "1") != "2")
        {
            WalletBatch batch(*walletInstance->database);

            for (const CWalletTx& wtxOld : vWtx)
            {
                uint256 hash = wtxOld.GetHash();
                std::map<uint256, CWalletTx>::iterator mi = walletInstance->mapWallet.find(hash);
                if (mi != walletInstance->mapWallet.end())
                {
                    const CWalletTx* copyFrom = &wtxOld;
                    CWalletTx* copyTo = &mi->second;
                    copyTo->mapValue = copyFrom->mapValue;
                    copyTo->vOrderForm = copyFrom->vOrderForm;
                    copyTo->nTimeReceived = copyFrom->nTimeReceived;
                    copyTo->nTimeSmart = copyFrom->nTimeSmart;
                    copyTo->fFromMe = copyFrom->fFromMe;
                    copyTo->nOrderPos = copyFrom->nOrderPos;
                    batch.WriteTx(*copyTo);
                }
            }
        }
    }

    uiInterface.LoadWallet(walletInstance);

    // Register with the validation interface. It's ok to do this after rescan since we're still holding cs_main.
    RegisterValidationInterface(walletInstance.get());

    walletInstance->SetBroadcastTransactions(gArgs.GetBoolArg("-walletbroadcast", DEFAULT_WALLETBROADCAST));

    {
        LOCK(walletInstance->cs_wallet);
        walletInstance->WalletLogPrintf("setKeyPool.size() = %u\n",      walletInstance->GetKeyPoolSize());
        walletInstance->WalletLogPrintf("mapWallet.size() = %u\n",       walletInstance->mapWallet.size());
        walletInstance->WalletLogPrintf("mapAddressBook.size() = %u\n",  walletInstance->mapAddressBook.size());
    }

    //Load zerocoin mint hashes to memory
    walletInstance->zTracker->Init();
    CKey keyZerocoin;
    zwallet->LoadMintPoolFromDB();
    if (!walletInstance->IsLocked() && !walletInstance->IsUnlockedForStakingOnly()) {
        assert(walletInstance->GetZerocoinSeed(keyZerocoin));
        zwallet = walletInstance->GetZWallet();
        auto idExpect = zwallet->GetMasterSeedID();
        assert(keyZerocoin.GetPubKey().GetID() == idExpect);
        zwallet->SetMasterSeed(keyZerocoin);
        zwallet->LoadMintPoolFromDB();
        zwallet->GenerateMintPool();
        zwallet->SyncWithChain();

        CExtKey extMasterAnon;
        if (!walletInstance->GetAnonWalletSeed(extMasterAnon))
            throw std::runtime_error(strprintf("%s: could not get anon wallet seed on wallet load", __func__));
        AnonWallet* anonwallet = walletInstance->GetAnonWallet();
        assert(anonwallet->UnlockWallet(extMasterAnon));
    }

    return walletInstance;
}

void CWallet::postInitProcess()
{
    // Add wallet transactions that aren't already in a block to mempool
    // Do this here as mempool requires genesis block to be loaded
    ReacceptWalletTransactions();
}

bool CWallet::BackupWallet(const std::string& strDest)
{
    return database->Backup(strDest);
}

CKeyPool::CKeyPool()
{
    nTime = GetTime();
    fInternal = false;
    m_pre_split = false;
}

CKeyPool::CKeyPool(const CPubKey& vchPubKeyIn, bool internalIn)
{
    nTime = GetTime();
    vchPubKey = vchPubKeyIn;
    fInternal = internalIn;
    m_pre_split = false;
}

CWalletKey::CWalletKey(int64_t nExpires)
{
    nTimeCreated = (nExpires ? GetTime() : 0);
    nTimeExpires = nExpires;
}

void CMerkleTx::SetMerkleBranch(const CBlockIndex* pindex, int posInBlock)
{
    // Update the tx's hashBlock
    hashBlock = pindex->GetBlockHash();

    // set the position of the transaction in the block
    nIndex = posInBlock;
}

int CMerkleTx::GetDepthInMainChain() const
{
    if (hashUnset())
        return 0;

    AssertLockHeld(cs_main);

    // Find the block it claims to be in
    CBlockIndex* pindex = LookupBlockIndex(hashBlock);
    if (!pindex || !chainActive.Contains(pindex))
        return 0;

    return ((nIndex == -1) ? (-1) : 1) * (chainActive.Height() - pindex->nHeight + 1);
}

int CMerkleTx::GetBlocksToMaturity() const
{
    if (!IsCoinBase())
        return 0;
    int chain_depth = GetDepthInMainChain();
    assert(chain_depth >= 0); // coinbase tx should not be conflicted
    return std::max(0, (Params().CoinbaseMaturity()+1) - chain_depth);
}

bool CMerkleTx::IsImmatureCoinBase() const
{
    // note GetBlocksToMaturity is 0 for non-coinbase tx
    return GetBlocksToMaturity() > 0;
}


bool CWalletTx::AcceptToMemoryPool(const CAmount& nAbsurdFee, CValidationState& state)
{
    // We must set fInMempool here - while it will be re-set to true by the
    // entered-mempool callback, if we did not there would be a race where a
    // user could call sendmoney in a loop and hit spurious out of funds errors
    // because we think that this newly generated transaction's change is
    // unavailable as we're not yet aware that it is in the mempool.
    bool ret = ::AcceptToMemoryPool(mempool, state, tx, nullptr /* pfMissingInputs */,
                                nullptr /* plTxnReplaced */, false /* bypass_limits */, nAbsurdFee);
    fInMempool |= ret;
    return ret;
}

void CWallet::LearnRelatedScripts(const CPubKey& key, OutputType type)
{
    if (key.IsCompressed() && (type == OutputType::P2SH_SEGWIT || type == OutputType::BECH32)) {
        CTxDestination witdest = WitnessV0KeyHash(key.GetID());
        CScript witprog = GetScriptForDestination(witdest);
        // Make sure the resulting program is solvable.
        assert(IsSolvable(*this, witprog));
        AddCScript(witprog);
    }
}

void CWallet::LearnAllRelatedScripts(const CPubKey& key)
{
    // OutputType::P2SH_SEGWIT always adds all necessary scripts for all types.
    LearnRelatedScripts(key, OutputType::P2SH_SEGWIT);
}

std::vector<OutputGroup> CWallet::GroupOutputs(const std::vector<COutput>& outputs, bool single_coin) const {
    std::vector<OutputGroup> groups;
    std::map<CTxDestination, OutputGroup> gmap;
    CTxDestination dst;
    for (const auto& output : outputs) {
        if (output.fSpendable) {
            CInputCoin input_coin = output.GetInputCoin();

            size_t ancestors, descendants;
            mempool.GetTransactionAncestry(output.tx->GetHash(), ancestors, descendants);
            if (!single_coin && ExtractDestination(output.tx->tx->vpout[output.i], dst)) {
                // Limit output groups to no more than 10 entries, to protect
                // against inadvertently creating a too-large transaction
                // when using -avoidpartialspends
                if (gmap[dst].m_outputs.size() >= OUTPUT_GROUP_MAX_ENTRIES) {
                    groups.push_back(gmap[dst]);
                    gmap.erase(dst);
                }
                gmap[dst].Insert(input_coin, output.nDepth, output.tx->IsFromMe(ISMINE_ALL), ancestors, descendants);
            } else {
                groups.emplace_back(input_coin, output.nDepth, output.tx->IsFromMe(ISMINE_ALL), ancestors, descendants);
            }
        }
    }
    if (!single_coin) {
        for (const auto& it : gmap) groups.push_back(it.second);
    }
    return groups;
}

// Given a set of inputs, find the public key that contributes the most coins to the input set
CScript GetLargestContributor(std::set<CInputCoin>& setCoins)
{
    std::map<CScript, CAmount> mapScriptsOut;
    for (const CInputCoin& coin : setCoins) {
        mapScriptsOut[coin.txout.scriptPubKey] += coin.txout.nValue;
    }

    CScript scriptLargest;
    CAmount nLargestContributor = 0;
    for (auto it : mapScriptsOut) {
        if (it.second > nLargestContributor) {
            scriptLargest = it.first;
            nLargestContributor = it.second;
        }
    }

    return scriptLargest;
}

bool CWallet::MintToTxIn(CZerocoinMint zerocoinSelected, int nSecurityLevel, const uint256& hashTxOut, CTxIn& newTxIn,
                         CZerocoinSpendReceipt& receipt, libzerocoin::SpendType spendType, CBlockIndex* pindexCheckpoint)
{
    auto hashSerial = GetSerialHash(zerocoinSelected.GetSerialNumber());
    CMintMeta meta = zTracker->Get(hashSerial);

    // Default error status if not changed below
    receipt.SetStatus(_("Transaction Mint Started"), ZTXMINT_GENERAL);

    // 2. Get pubcoin from the private coin
    libzerocoin::CoinDenomination denomination = zerocoinSelected.GetDenomination();
    libzerocoin::PublicCoin pubCoinSelected(Params().Zerocoin_Params(), zerocoinSelected.GetValue(), denomination);
    if (!pubCoinSelected.validate()) {
        receipt.SetStatus(_("The selected mint coin is an invalid coin"), ZINVALID_COIN);
        return false;
    }

    // 3. Compute Accumulator and Witness
    string strFailReason = "";
    AccumulatorMap mapAccumulators(Params().Zerocoin_Params());
    libzerocoin::Accumulator accumulator = mapAccumulators.GetAccumulator(denomination);
    libzerocoin::AccumulatorWitness accumulatorWitness(Params().Zerocoin_Params(), accumulator, pubCoinSelected);
    int nMintsAdded = 0;
    bool fLightZerocoin = chainActive.Height() + 1 >= Params().HeightLightZerocoin();
    if (!fLightZerocoin) {
        if (!GenerateAccumulatorWitness(pubCoinSelected, accumulator, accumulatorWitness, nSecurityLevel, nMintsAdded, strFailReason, pindexCheckpoint)) {
            receipt.SetStatus(_("Try to spend with a higher security level to include more coins"), ZFAILED_ACCUMULATOR_INITIALIZATION);
            return error("%s : %s", __func__, receipt.GetStatusMessage());
        }
    }

    // Construct the CoinSpend object. This acts like a signature on the transaction.
    libzerocoin::PrivateCoin privateCoin(Params().Zerocoin_Params(), denomination, false);
    privateCoin.setPublicCoin(pubCoinSelected);
    privateCoin.setRandomness(zerocoinSelected.GetRandomness());
    privateCoin.setSerialNumber(zerocoinSelected.GetSerialNumber());

    // Version 2 zerocoins have a privkey associated with them. This key is stored in the wallet keystore and has to be retrieved
    uint8_t nVersion = zerocoinSelected.GetVersion();
    privateCoin.setVersion(nVersion);
    CKey key;
    if (!zerocoinSelected.GetKeyPair(key))
        return error("%s: failed to set zerocoin privkey mint version=%d", __func__, nVersion);
    privateCoin.setPrivKey(key.GetPrivKey());

    uint256 nChecksum = GetChecksum(accumulator.getValue());
    if (fLightZerocoin) {
        if (pindexCheckpoint)
            nChecksum = pindexCheckpoint->mapAccumulatorHashes.at(denomination);
        else
            nChecksum = chainActive[chainActive.Height() - 20]->mapAccumulatorHashes.at(denomination);
    }
    CBigNum bnValue;
    if (!GetAccumulatorValueFromChecksum(nChecksum, false, bnValue) || bnValue == 0)
        return error("%s: could not find checksum used for spend\n", __func__);

    try {

        //Figure out if limp mod is enabled. If this is a PoS tx need to see if the next block will have it enabled too
        bool fZCLimpMode = false;
        ThresholdState thresholdState = VersionBitsTipState(Params().GetConsensus(), Consensus::DEPLOYMENT_ZC_LIMP);
        if (thresholdState == ThresholdState::LOCKED_IN && spendType == libzerocoin::STAKE) {
            int nHeightSince = VersionBitsTipStateSinceHeight(Params().GetConsensus(), Consensus::DEPLOYMENT_ZC_LIMP);
            if (chainActive.Height()+1 - nHeightSince == Params().BIP9Period())
                fZCLimpMode = true;
        } else if (thresholdState == ThresholdState::ACTIVE) {
            fZCLimpMode = true;
        }

        uint8_t nVersion = fZCLimpMode ? libzerocoin::CoinSpend::V4_LIMP : libzerocoin::CoinSpend::V3_SMALL_SOK;

        //Link to the outpoint that the mint came from if using light zerocoin mode
        uint256 txidMintFrom = uint256();
        int pos = -1;
        if (fLightZerocoin) {
            txidMintFrom = meta.txid;
            CTransactionRef ptx;
            uint256 hashblock;
            if (!GetTransaction(meta.txid, ptx, Params().GetConsensus(), hashblock, true))
                return error("failed to find mint transaction");
            int i = -1;
            for (auto txout : ptx->vpout) {
                i++;
                if (!txout->IsZerocoinMint())
                    continue;
                libzerocoin::PublicCoin pubcoin(Params().Zerocoin_Params());
                if (OutputToPublicCoin(txout.get(), pubcoin)) {
                     if (meta.hashPubcoin == GetPubCoinHash(pubcoin.getValue())) {
                         pos = i;
                         break;
                     }

                }
            }
            if (pos == -1)
                return error("Could not find zerocoin mint outpoint");
        }

        libzerocoin::CoinSpend spend(Params().Zerocoin_Params(), privateCoin, accumulator, nChecksum, accumulatorWitness, hashTxOut, spendType, nVersion, fLightZerocoin, txidMintFrom, pos);

        std::string strError;
        bool fVerifySoK = !fLightZerocoin;
        bool fVerifyZKP = fVerifySoK;
        if (!spend.Verify(accumulator, strError, privateCoin.getPublicCoin().getValue(), fVerifySoK, /*VerifyPubcoin*/true, fVerifyZKP)) {
            receipt.SetStatus(_("The new spend coin transaction did not verify"), ZINVALID_WITNESS);
            return false;
        }
        // Deserialize the CoinSpend intro a fresh object
        CDataStream serializedCoinSpend(SER_NETWORK, PROTOCOL_VERSION);
        serializedCoinSpend << spend;
        std::vector<unsigned char> data(serializedCoinSpend.begin(), serializedCoinSpend.end());

        //Add the coin spend into a VEIL transaction
        newTxIn.scriptSig = CScript() << OP_ZEROCOINSPEND << data.size();
        newTxIn.scriptSig.insert(newTxIn.scriptSig.end(), data.begin(), data.end());
        newTxIn.prevout.SetNull();

        //use nSequence as a shorthand lookup of denomination
        //NOTE that this should never be used in place of checking the value in the final blockchain acceptance/verification
        //of the transaction
        newTxIn.nSequence = denomination;
        newTxIn.nSequence |= CTxIn::SEQUENCE_LOCKTIME_DISABLE_FLAG; //Don't use any relative locktime for zerocoin spend

        if (IsSerialKnown(spend.getCoinSerialNumber())) {
            //Tried to spend an already spent Zerocoin
            receipt.SetStatus(_("The coin spend has been used"), ZSPENT_USED_ZPIV);

            uint256 hashSerial = GetSerialHash(spend.getCoinSerialNumber());
            if (!zTracker->HasSerialHash(hashSerial))
                return error("%s: serialhash %s not found in tracker", __func__, hashSerial.GetHex());

            CMintMeta meta = zTracker->Get(hashSerial);
            meta.isUsed = true;
            if (!zTracker->UpdateState(meta))
                LogPrintf("%s: failed to write zerocoinmint\n", __func__);

            auto pwalletmain = GetMainWallet();

            //todo:
            //pwalletmain->NotifyZerocoinChanged(pwalletmain, zerocoinSelected.GetValue().GetHex(), "Used", CT_UPDATED);
            return false;
        }

        auto nAccumulatorChecksum = GetChecksum(accumulator.getValue());
        CZerocoinSpend zcSpend(spend.getCoinSerialNumber(), uint256(), zerocoinSelected.GetValue(),
                zerocoinSelected.GetDenomination(), nAccumulatorChecksum);
        zcSpend.SetMintCount(nMintsAdded);
        receipt.AddSpend(zcSpend);
    } catch (const std::exception&) {
        receipt.SetStatus(_("CoinSpend: Accumulator witness does not verify"), ZINVALID_WITNESS);
        return false;
    }
    receipt.SetStatus(_("Spend Valid"), ZSPEND_OKAY); // Everything okay

    return true;
}

string CWallet::MintZerocoinFromOutPoint(CAmount nValue, CWalletTx& wtxNew, std::vector<CDeterministicMint>& vDMints,
        const std::vector<COutPoint> vOutpts)
{
    CCoinControl* coinControl = new CCoinControl();
    for (const COutPoint& output : vOutpts) {
        coinControl->Select(output, 0); //todo add amount
    }

    if (!coinControl->HasSelected()) {
        string strError = _("Error: No valid utxo!");
        LogPrintf("%s: %s\n", __func__, strError.c_str());
        return strError;
    }

    string strError = MintZerocoin(nValue, wtxNew, vDMints, /*inputtype*/OUTPUT_STANDARD, coinControl);
    delete coinControl;
    return strError;
}

string CWallet::MintZerocoin(CAmount nValue, CWalletTx& wtxNew, vector<CDeterministicMint>& vDMints, OutputTypes inputtype, const CCoinControl* coinControl)
{
    // Check amount
    if (nValue < libzerocoin::ZerocoinDenominationToAmount(libzerocoin::CoinDenomination::ZQ_TEN))
        return _("Invalid amount");

    CAmount nValueRequired = nValue + Params().Zerocoin_MintFee();

    // User must specifically request to use basecoin, or else must have the anon balance for it
    CAmount nBalance = 0;
    std::string strTypeName = "";
    //todo cs_main lock?
    if (inputtype == OUTPUT_RINGCT && chainActive.Tip()->nAnonOutputs > 20) {
        nBalance = pAnonWalletMain->GetAnonBalance();
        strTypeName = "RingCT";
    } else if (inputtype == OUTPUT_CT) {
        nBalance = pAnonWalletMain->GetBlindBalance();
        strTypeName = "CT";
    } else if (inputtype == OUTPUT_STANDARD) {
        nBalance = GetBasecoinBalance();
        strTypeName = "Basecoin";
    }

    if (nValueRequired > nBalance)
        return strprintf("Insufficient %s Funds", strTypeName);

    int64_t nComputeTimeStart = GetTimeMillis();

    CReserveKey reserveKey(this);
    int64_t nFeeRequired;

    if (IsLocked() || IsUnlockedForStakingOnly()) {
        string strError = _("Error: Wallet locked, unable to create transaction!");
        LogPrintf("%s: %s\n", __func__, strError.c_str());
        return strError;
    }

    string strError;
    CMutableTransaction txNew;
    std::vector<CTempRecipient> vecSend;
    if (!CreateZerocoinMintTransaction(nValue, txNew, vDMints, nFeeRequired, strError, vecSend, inputtype,
                                       coinControl)) {
        if (nValue + nFeeRequired > GetBasecoinBalance())
            return strprintf(_("Error: Failed to create transaction: %s"), strError);
        return strError;
    }

    CTransactionRef txRef = std::shared_ptr<CTransaction>(new CTransaction(txNew));
    wtxNew = CWalletTx(this, txRef);
    wtxNew.fFromMe = true;
    wtxNew.fTimeReceivedIsTxTime = true;

    // Limit size
    unsigned int nBytes = ::GetSerializeSize(txNew, SER_NETWORK, PROTOCOL_VERSION);
    if (nBytes >= MAX_ZEROCOIN_TX_SIZE) {
        return _("Error: The transaction is larger than the maximum allowed transaction size!");
    }

    //commit the transaction to the network
    CValidationState state;
    mapValue_t mapValue;

    int64_t nComputeTimeFinish = GetTimeMillis();

    if (!CommitTransaction(wtxNew.tx, std::move(mapValue), {}, &reserveKey, g_connman.get(), state, nComputeTimeFinish - nComputeTimeStart)) {
        return _("Error: The transaction was rejected! This might happen if some of the coins in your wallet were already "
                 "spent, such as if you used a copy of wallet.dat and coins were spent in the copy but not marked as spent here.");
    } else {
        //update mints with full transaction hash and then database them
        WalletBatch walletdb(*this->database);
        for (CDeterministicMint dMint : vDMints) {
            dMint.SetTxHash(wtxNew.tx->GetHash());
            zTracker->Add(dMint, true);
        }
    }

    //Create a backup of the wallet
    if (fBackupMints)
        ZBackupWallet();

    return "";
}

CAmount CWallet::GetAvailableZerocoinBalance(const CCoinControl* coinControl) const
{
    LOCK2(cs_main, cs_wallet);

    CAmount balance = 0;
    std::set<CMintMeta> setMeta;
    AvailableZerocoins(setMeta, coinControl);
    for (auto meta : setMeta) {
        balance += meta.denom * COIN;
    }
    return balance;
}

bool CWallet::AvailableZerocoins(std::set<CMintMeta>& setMints, const CCoinControl *coinControl) const
{
    auto setMintsTemp = zTracker->ListMints(true, true, true); // need to find mints to spend
    for (const CMintMeta& mint : setMintsTemp) {
        if (mint.nMemFlags & MINT_PENDINGSPEND)
            continue;

        if (coinControl && coinControl->HasSelected() && !coinControl->fAllowOtherInputs && !coinControl->IsSelected(mint.hashSerial))
            continue;

        setMints.emplace(mint);
    }

    return true;
}

bool CWallet::SpendZerocoin(CAmount nValue, int nSecurityLevel, CZerocoinSpendReceipt& receipt,
        std::vector<CZerocoinMint>& vMintsSelected, bool fMintChange, bool fMinimizeChange,
        libzerocoin::CoinDenomination denomFilter, CTxDestination* addressTo)
{
    int64_t nComputeTimeStart = GetTimeMillis();

    std::vector<CommitData> vCommitData;
    if (!PrepareZerocoinSpend(nValue, nSecurityLevel, receipt, vMintsSelected, fMintChange, fMinimizeChange,
            vCommitData, denomFilter, addressTo))
        return error("%s : PrepareZerocoinSpend %s", __func__, receipt.GetStatusMessage());

    int64_t nComputeTimeFinish = GetTimeMillis();

    if (!CommitZerocoinSpend(receipt, vCommitData, nComputeTimeFinish - nComputeTimeStart))
        return error("%s : CommitZerocoinSpend: %s", __func__, receipt.GetStatusMessage());

    return true;
}

bool CWallet::PrepareZerocoinSpend(CAmount nValue, int nSecurityLevel, CZerocoinSpendReceipt& receipt,
                            std::vector<CZerocoinMint>& vMintsSelected, bool fMintChange, bool fMinimizeChange,
                            std::vector<CommitData>& vCommitData, libzerocoin::CoinDenomination denomFilter, CTxDestination* addressTo)
{
    // Default: assume something goes wrong. Depending on the problem this gets more specific below
    int nStatus = ZSPEND_ERROR;
    if (IsLocked() || IsUnlockedForStakingOnly()) {
        receipt.SetStatus("Error: Wallet locked, unable to create transaction!", ZWALLET_LOCKED);
        return error("%s : %s", __func__, receipt.GetStatusMessage());
    }
    // If not already given pre-selected mints, then select mints from the wallet
    if (vMintsSelected.empty()) {
        if(!CollectMintsForSpend(nValue, vMintsSelected, receipt, nStatus, fMinimizeChange, denomFilter)) {
            return error("%s : %s", __func__, receipt.GetStatusMessage());
        }
    }

    // todo: should we use a different reserve key for each transaction?
    const uint32_t nMaxSpends = Params().Zerocoin_MaxSpendsPerTransaction(); // Maximum possible spends for one z transaction
    CAmount nRemainingValue = nValue;
    for (auto start = 0; static_cast<uint32_t>(start) < vMintsSelected.size(); start += nMaxSpends) {
        std::vector<CZerocoinMint> vBatchMints;
        auto itStart = vMintsSelected.begin() + start;
        CAmount nBatchValue = 0;
        if (start + nMaxSpends >= vMintsSelected.size()) {
            vBatchMints = std::vector<CZerocoinMint>(itStart, vMintsSelected.end());
            nBatchValue = nRemainingValue;
        } else {
            vBatchMints = std::vector<CZerocoinMint>(itStart, itStart + nMaxSpends);
            for (const CZerocoinMint& mint: vBatchMints)
                nBatchValue += mint.GetDenominationAsAmount();
        }

        std::vector<CDeterministicMint> vCurNewMints;
        CWalletTx wtxCurrent(this, nullptr);
        if (!CreateZerocoinSpendTransaction(nBatchValue, nSecurityLevel, wtxCurrent, receipt,
                                            vBatchMints, vCurNewMints, fMintChange, fMinimizeChange, addressTo)) {
            return error("%s : %s", __func__, receipt.GetStatusMessage());
        }
        CValidationState state;
        LOCK(cs_main);
        if (!AcceptToMemoryPool(mempool, state, wtxCurrent.tx, nullptr /* pfMissingInputs */, nullptr /* plTxnReplaced */,
                                false /* bypass_limits */, maxTxFee, true)) {
            // failed mempool validation for one of the transactions so no partial transaction is being committed
            return error("%s : %s", __func__, "Failed to get accepted to memory pool");
        }

        vCommitData.emplace_back(std::make_tuple(wtxCurrent, vCurNewMints, vBatchMints));
        nRemainingValue -= nBatchValue;
    }

    if (fMintChange && fBackupMints)
        ZBackupWallet();

    receipt.SetStatus("Preparation Successful", ZSPEND_PREPARED);  // When we reach this point the preparation was successful
    return true;
}

bool CWallet::CommitZerocoinSpend(CZerocoinSpendReceipt& receipt, std::vector<CommitData>& vCommitData, int computeTime)
{
    WalletBatch walletdb(*this->database);
    CValidationState state;
    mapValue_t mapValue;
    int nStatus = ZSPEND_ERROR;

    std::vector<CTransactionRef> vtx = receipt.GetTransactions();
    for (unsigned int i = 0; i < vCommitData.size(); i++) {
        const auto& commitData = vCommitData[i];
        auto wtxCurrent = std::get<0>(commitData);
        auto vNewMints = std::get<1>(commitData);
        auto vNewSelectedMints = std::get<2>(commitData);

        bool fTxFail = false;
        if (vtx.size() <= i) {
            error("%s: FIXME: vtx size does not match expected value!! vtx %d i=%d", __func__, vtx.size(), i);
            fTxFail = true;
        }

        if (fTxFail || !CommitTransaction(vtx[i], {}, {}, /*CReserveKey*/nullptr, g_connman.get(), state, computeTime)) {
            LogPrintf("%s: failed to commit\n", __func__);
            nStatus = ZCOMMIT_FAILED;

            //reset all mints
            for (const CZerocoinMint& mint : vNewSelectedMints) {
                uint256 hashPubcoin = GetPubCoinHash(mint.GetValue());
                zTracker->SetPubcoinNotUsed(hashPubcoin);
            }

            //erase spends
            for (const CZerocoinSpend& spend : receipt.GetSpends(i)) {
                if (!walletdb.EraseZerocoinSpendSerialEntry(spend.GetSerial())) {
                    receipt.SetStatus("Error: It cannot delete coin serial number in wallet", ZERASE_SPENDS_FAILED);
                }

                //Remove from public zerocoinDB
                RemoveSerialFromDB(spend.GetSerial());
            }

            // erase new mints
            for (auto &dMint : vNewMints) {
                if (!walletdb.EraseDeterministicMint(dMint.GetPubcoinHash())) {
                    receipt.SetStatus("Error: Unable to cannot delete zerocoin mint in wallet",
                                      ZERASE_NEW_MINTS_FAILED);
                }
            }

            receipt.SetStatus(
                    "Error: The transaction was rejected! This might happen if some of the coins in your wallet "
                    "were already spent, such as if you used a copy of wallet.dat and coins were spent in the copy "
                    "but not marked as spent here.", nStatus);

            return false;
        }

        //Set spent mints as used
        uint256 txidSpend = wtxCurrent.tx->GetHash();
        for (const CZerocoinMint& mint : vNewSelectedMints) {
            uint256 hashPubcoin = GetPubCoinHash(mint.GetValue());
            zTracker->SetPubcoinUsed(hashPubcoin, txidSpend);

            CMintMeta metaCheck = zTracker->GetMetaFromPubcoin(hashPubcoin);
            if (!metaCheck.isUsed) {
                receipt.SetStatus("Error, the mint did not get marked as used", nStatus);
                return false;
            }
        }

        // write new Mints to db
        for (auto &dMint : vNewMints) {
            dMint.SetTxHash(txidSpend);
            zTracker->Add(dMint, true);
        }
    }

    receipt.SetStatus("Spend Successful", ZSPEND_OKAY);
    return true;
}

bool IsMintInChain(const uint256& hashPubcoin, uint256& txid, int& nHeight)
{
    return IsPubcoinInBlockchain(hashPubcoin, nHeight, txid, chainActive.Tip());
}

void CWallet::ReconsiderZerocoins(std::list<CZerocoinMint>& listMintsRestored, std::list<CDeterministicMint>& listDMintsRestored)
{
    WalletBatch walletdb(*database);
    list<CZerocoinMint> listMints = walletdb.ListArchivedZerocoins();
    list<CDeterministicMint> listDMints = walletdb.ListArchivedDeterministicMints();

    if (listMints.empty() && listDMints.empty())
        return;

    for (CZerocoinMint mint : listMints) {
        uint256 txid;
        int nHeight;
        uint256 hashPubcoin = GetPubCoinHash(mint.GetValue());
        if (!IsMintInChain(hashPubcoin, txid, nHeight))
            continue;

        mint.SetTxHash(txid);
        mint.SetHeight(nHeight);
        mint.SetUsed(IsSerialInBlockchain(mint.GetSerialNumber(), nHeight));

        if (!zTracker->UnArchive(hashPubcoin, false)) {
            LogPrintf("%s : failed to unarchive mint %s\n", __func__, mint.GetValue().GetHex());
        } else {
            zTracker->UpdateZerocoinMint(mint);
        }
        listMintsRestored.emplace_back(mint);
    }

    for (CDeterministicMint dMint : listDMints) {
        uint256 txid;
        int nHeight;
        if (!IsMintInChain(dMint.GetPubcoinHash(), txid, nHeight))
            continue;

        dMint.SetTxHash(txid);
        dMint.SetHeight(nHeight);
        uint256 txidSpend;
        dMint.SetUsed(IsSerialInBlockchain(dMint.GetSerialHash(), nHeight, txidSpend));

        if (!zTracker->UnArchive(dMint.GetPubcoinHash(), true)) {
            LogPrintf("%s : failed to unarchive deterministic mint %s\n", __func__, dMint.GetPubcoinHash().GetHex());
        } else {
            zTracker->Add(dMint, true);
        }
        listDMintsRestored.emplace_back(dMint);
    }
}

void CWallet::ZBackupWallet()
{
    fs::path backupDir = GetDataDir() / "backups";
    fs::path backupPath;
    string strNewBackupName;

    for (int i = 0; i < 10; i++) {
        strNewBackupName = strprintf("wallet-autozbackup-%d.dat", i);
        backupPath = backupDir / strNewBackupName;

        if (fs::exists(backupPath)) {
            //Keep up to 10 backups
            if (i <= 8) {
                //If the next file backup exists and is newer, then iterate
                fs::path nextBackupPath = backupDir / strprintf("wallet-autozbackup-%d.dat", i + 1);
                if (fs::exists(nextBackupPath)) {
                    time_t timeThis = fs::last_write_time(backupPath);
                    time_t timeNext = fs::last_write_time(nextBackupPath);
                    if (timeThis > timeNext) {
                        //The next backup is created before this backup was
                        //The next backup is the correct path to use
                        backupPath = nextBackupPath;
                        break;
                    }
                }
                //Iterate to the next filename/number
                continue;
            }
            //reset to 0 because name with 9 already used
            strNewBackupName = strprintf("wallet-autozbackup-%d.dat", 0);
            backupPath = backupDir / strNewBackupName;
            break;
        }
        //This filename is fresh, break here and backup
        break;
    }

    BackupWallet(backupPath.string());

    if(!gArgs.GetArg("-zbackuppath", "").empty()) {
        fs::path customPath(gArgs.GetArg("-zbackuppath", ""));
        fs::create_directories(customPath);

        if(!customPath.has_extension()) {
            customPath /= GetUniqueWalletBackupName(true);
        }

        BackupWallet(customPath.string());
    }
}

bool CWallet::CreateZOutPut(libzerocoin::CoinDenomination denomination, CTxOut& outMint, CDeterministicMint& dMint)
{
    // mint a new coin (create Pedersen Commitment) and extract PublicCoin that is shareable from it
    auto zerocoinParams = Params().Zerocoin_Params();
    libzerocoin::PrivateCoin coin(zerocoinParams, denomination, false);

    if (zwalletMain->HasEmptySeed())
        return error("%s: Zerocoin seed is not loaded!", __func__);

    zwalletMain->GenerateDeterministicZerocoin(denomination, coin, dMint);

    libzerocoin::PublicCoin pubCoin = coin.getPublicCoin();

    // Validate
    if(!pubCoin.validate())
        return error("%s: newly created pubcoin is not valid", __func__);

    zwalletMain->UpdateCount();

    CScript scriptSerializedCoin = CScript() << OP_ZEROCOINMINT << pubCoin.getValue().getvch().size() << pubCoin.getValue().getvch();
    outMint = CTxOut(libzerocoin::ZerocoinDenominationToAmount(denomination), scriptSerializedCoin);

    return true;
}

bool CWallet::CreateZerocoinMintTransaction(const CAmount nValue, CMutableTransaction& txNew,
        std::vector<CDeterministicMint>& vDMints, int64_t& nFeeRet, std::string& strFailReason,
        std::vector<CTempRecipient>& vecSend, OutputTypes inputtype, const CCoinControl* coinControl, const bool isZCSpendChange)
{
    if (IsLocked() || IsUnlockedForStakingOnly()) {
        strFailReason = "Error: Wallet locked, unable to create transaction!";
        LogPrintf("SpendZerocoin() : %s", strFailReason.c_str());
        return false;
    }

    // Enforce that the amount requested is a multiple of the minimum mintable denomination
    CAmount nValueMinDenom = libzerocoin::ZerocoinDenominationToAmount(libzerocoin::CoinDenomination::ZQ_TEN);
    if (nValue % nValueMinDenom != 0) {
        strFailReason = strprintf("Error: Requested mint amount needs to be multiple of %s!", FormatMoney(nValueMinDenom));
        LogPrintf("SpendZerocoin() : %s", strFailReason.c_str());
        return false;
    }

    //add multiple mints that will fit the amount requested as closely as possible
    CAmount nMintingValue = 0;
    CAmount nValueRemaining = 0;
    while (true) {
        //mint a coin with the closest denomination to what is being requested
        nFeeRet = std::max(static_cast<int>(txNew.vpout.size()), 1) * Params().Zerocoin_MintFee();
        nValueRemaining = nValue - nMintingValue;// - (isZCSpendChange ? nFeeRet : 0);

        libzerocoin::CoinDenomination denomination = libzerocoin::AmountToClosestDenomination(nValueRemaining, nValueRemaining);
        if (denomination == libzerocoin::ZQ_ERROR) {
            break;
        }

        CAmount nValueNewMint = libzerocoin::ZerocoinDenominationToAmount(denomination);
        nMintingValue += nValueNewMint;

        CTxOut outMint;
        CDeterministicMint dMint;
        if (!CreateZOutPut(denomination, outMint, dMint)) {
            strFailReason = strprintf("%s: failed to create new z output", __func__);
            return error(strFailReason.c_str());
        }

        if (isZCSpendChange || inputtype == OUTPUT_STANDARD) {
            txNew.vpout.emplace_back(outMint.GetSharedPtr());
        }

        //store as CZerocoinMint for later use
        LogPrintf("%s: new mint %s\n", __func__, dMint.ToString());
        vDMints.emplace_back(dMint);

        //Fill out a temp recipient, which is what ringct code uses to eventually convert this into an output
        CTempRecipient tempRecipient;
        tempRecipient.nType = OUTPUT_STANDARD;
        tempRecipient.SetAmount(outMint.nValue);
        tempRecipient.fSubtractFeeFromAmount = false;
        tempRecipient.scriptPubKey = outMint.scriptPubKey;
        tempRecipient.fScriptSet = true;
        tempRecipient.fZerocoin = true;
        tempRecipient.fZerocoinMint = true;
        tempRecipient.fExemptFeeSub = true;
        tempRecipient.fSubtractFeeFromAmount = false;
        vecSend.emplace_back(tempRecipient);
    }

    // Inputs are already selected for zerocoinspend
    if (isZCSpendChange)
        return true;

    std::set<CInputCoin> setCoins;

    /** Select RingCT or CT Inputs **/
    // create output variables for add anon inputs
    CTransactionRef tx_new;
    CWalletTx wtx(this, tx_new);

    // protecting against nullptr dereference
    CCoinControl cControl;
    if (coinControl)
        cControl = *coinControl;

    CTransactionRecord rtx;
    std::string sError;

    if (inputtype == OUTPUT_RINGCT)  {
        // default parameters for ring sig
        if (!pAnonWalletMain->AddAnonInputs(wtx, rtx, vecSend, true, Params().DefaultRingSize(), /**nInputsPerSig**/ 32, nFeeRet, &cControl, sError)) {
            strFailReason = strprintf("Failed to add ringctinputs: %s", sError);
            return false;
        }
    } else if (inputtype == OUTPUT_CT) {
        if (0 != pAnonWalletMain->AddBlindedInputs(wtx, rtx, vecSend, true, nFeeRet, &cControl, sError)) {
            strFailReason = strprintf("Failed to add ringctinputs: %s", sError);
            return false;
        }
    } else if (inputtype == OUTPUT_STANDARD) {
        if (0 != pAnonWalletMain->AddStandardInputs(wtx, rtx, vecSend, true, nFeeRet, &cControl, sError, /*fZerocoinInputs*/false, /*InputValue*/0)) {
            strFailReason = strprintf("Failed to add basecoin inputs: %s", sError);
            return false;
        }
    }

    txNew = CMutableTransaction(*wtx.tx);
    return true;
}

bool CWallet::CollectMintsForSpend(CAmount nValue, std::vector<CZerocoinMint>& vMints, CZerocoinSpendReceipt& receipt, int nStatus, bool fMinimizeChange, libzerocoin::CoinDenomination denomFilter)
{
    // Check available funds
    nStatus = ZTRX_FUNDS_PROBLEMS;
    CAmount nzBalance = GetZerocoinBalance(true);
    if (nValue > nzBalance) {
        receipt.SetStatus(strprintf("You don't have enough Zerocoins in your wallet. Balance: %s", FormatMoney(nzBalance)), nStatus);
        return false;
    }

    std::set<CMintMeta> setMints;
    AvailableZerocoins(setMints);
    if (setMints.empty()) {
        receipt.SetStatus("Failed to find Zerocoins in wallet.dat", nStatus);
        return false;
    }

    // If the input value is not an int, then we want the selection algorithm to round up to the next highest int
    double dValue = static_cast<double>(nValue) / static_cast<double>(COIN);
    bool fWholeNumber = floor(dValue) == dValue;
    CAmount nValueToSelect = nValue;
    if(!fWholeNumber)
        nValueToSelect = static_cast<CAmount>(ceil(dValue) * COIN);

    // Select the z mints to use in this spend
    std::map<libzerocoin::CoinDenomination, CAmount> DenomMap = GetMyZerocoinDistribution().first;
    std::list<CMintMeta> listMints(setMints.begin(), setMints.end());
    if (denomFilter != libzerocoin::CoinDenomination::ZQ_ERROR) {
        //A specific denom was selected to spend with
        listMints.clear();
        for (const auto& mint : setMints) {
            if (mint.denom == denomFilter)
                listMints.emplace_back(mint);
        }

        //Set denom map to 0 values for non-matching denoms
        for (auto mi = DenomMap.begin(); mi != DenomMap.end(); mi++) {
            if (mi->first != denomFilter)
                mi->second = 0;
        }
    }

    // order the list of mints - oldest first
    listMints.sort(oldest_first);

    int nCoinsReturned, nNeededSpends;
    CAmount nValueSelected;
    std::vector<CMintMeta> vMintsToFetch = SelectMintsFromList(nValueToSelect, nValueSelected, Params().Zerocoin_MaxSpendsPerTransaction(),
                                                               fMinimizeChange, nCoinsReturned, listMints, DenomMap, nNeededSpends);

    for (CMintMeta& meta : vMintsToFetch) {
        CZerocoinMint mint;
        if (!GetMint(meta.hashSerial, mint)) {
            receipt.SetStatus(strprintf("%s: failed to fetch hashSerial %s", __func__, meta.hashSerial.GetHex()), nStatus);
            return false;
        }
        vMints.emplace_back(mint);
    }

    return true;
}

bool CWallet::CreateZerocoinSpendTransaction(CAmount nValue, int nSecurityLevel, CWalletTx& wtxNew,
        CZerocoinSpendReceipt& receipt, std::vector<CZerocoinMint>& vSelectedMints,
        std::vector<CDeterministicMint>& vNewMints, bool fMintChange,  bool fMinimizeChange, CTxDestination* address)
{
    int nStatus = ZTRX_FUNDS_PROBLEMS;

    // Check we have selected mints to spend
    if (vSelectedMints.empty()) {
        receipt.SetStatus(strprintf("%s: No mint selected", __func__), nStatus);
        return false;
    }

    // Check that the included mints are at most Zerocoin_MaxSpendsPerTransaction
    if (vSelectedMints.size() > Params().Zerocoin_MaxSpendsPerTransaction()) {
        receipt.SetStatus("Failed to find coin set amongst held coins with less than maxNumber of Spends", nStatus);
        return false;
    }

    // Create transaction
    nStatus = ZTRX_CREATE;
    WalletBatch walletdb(*this->database);

    int nArchived = 0;
    CAmount nValueSelected = 0;
    for (const CZerocoinMint& mint : vSelectedMints) {
        nValueSelected += ZerocoinDenominationToAmount(mint.GetDenomination());
        // see if this serial has already been spent
        int nHeightSpend;
        if (IsSerialInBlockchain(mint.GetSerialNumber(), nHeightSpend)) {
            receipt.SetStatus("Trying to spend an already spent serial #, try again.", nStatus);
            uint256 hashSerial = GetSerialHash(mint.GetSerialNumber());
            if (!zTracker->HasSerialHash(hashSerial))
                return error("%s: tracker does not have serialhash %s", __func__, hashSerial.GetHex());

            CMintMeta meta = zTracker->Get(hashSerial);
            meta.isUsed = true;
            zTracker->UpdateState(meta);

            return false;
        }

        //check that this mint made it into the blockchain
        CTransactionRef txMint;
        uint256 hashBlock;
        bool fArchive = false;
        if (!GetTransaction(mint.GetTxHash(), txMint, Params().GetConsensus(), hashBlock)) {
            receipt.SetStatus("Unable to find transaction containing mint", nStatus);
            fArchive = true;
        } else if (!mapBlockIndex.count(hashBlock) || !chainActive.Contains(mapBlockIndex.at(hashBlock))) {
            receipt.SetStatus("Mint did not make it into blockchain", nStatus);
            fArchive = true;
        }

        // archive this mint as an orphan
        if (fArchive) {
            walletdb.ArchiveMintOrphan(mint);
            nArchived++;
        }
    }
    if (nArchived)
        return false;

    // Create change if needed
    nStatus = ZTRX_CHANGE;
    std::vector<CTempRecipient> vecSend;

    CMutableTransaction txNew;
    CAmount nChange;
    CAmount nChangeRemint;
    CAmount nChangeDust;
    wtxNew.BindWallet(this);
    {
        {
            txNew.vin.clear();
            txNew.vpout.clear();

            //if there is an address to send to then use it, if not generate a new address to send to
            CScript scriptZerocoinSpend;
            CScript scriptChange;
            nChange = nValueSelected - nValue;

            if (nChange < 0) {
                receipt.SetStatus("Selected coins value is less than payment target", nStatus);
                return false;
            }

            if (!address) {
                receipt.SetStatus("No address provided", nStatus);
                return false;
            }

            // Veil: Check whether to basecoin address or to stealth
            bool fStealthOutput = address->type() == typeid(CStealthAddress);

            if (address) {
                if (fStealthOutput) {
                    CTempRecipient r;
                    if (address->type() == typeid(CStealthAddress))
                        r.nType = OUTPUT_CT;
                    else
                        r.nType = OUTPUT_STANDARD;
                    r.SetAmount(nValue);
                    r.fSubtractFeeFromAmount = false;
                    r.address = *address;
                    r.fExemptFeeSub = false;
                    vecSend.emplace_back(r);
                } else {
                    CTempRecipient r;
                    r.nType = OUTPUT_STANDARD;
                    r.fZerocoin = false;
                    r.SetAmount(nValue);
                    r.fSubtractFeeFromAmount = false;
                    r.address = *address;
                    r.fExemptFeeSub = false;
                    vecSend.emplace_back(r);
                }
            }

            //add change output if we are spending too much (only applies to spending multiple at once)
            if (nChange) {
                // Try and mint the change as zerocoin
                if (fMintChange) {
                    //Any change below the smallest zerocoin denom is not remintable and is considered dust to be sent to ct
                    auto nAmountSmallestDenom = libzerocoin::ZerocoinDenominationToAmount(
                            libzerocoin::CoinDenomination::ZQ_TEN);
                    nChangeDust = nChange % nAmountSmallestDenom;
                    nChangeRemint = nChange - nChangeDust;

                    //See how many mints will be created and ensure enough fee is subtracted from the changedust
                    int nMintCount = 0;
                    CAmount nAmountDummy = nChangeRemint;
                    while (nAmountDummy >= nAmountSmallestDenom) {
                        CAmount nRemaining;
                        auto denomdummy = libzerocoin::AmountToClosestDenomination(nAmountDummy, nRemaining);
                        nMintCount++;
                        nAmountDummy -= libzerocoin::ZerocoinDenominationToAmount(denomdummy);
                    }
                    //Subtract any fee from changedust
                    CAmount nFeeRequired = Params().Zerocoin_MintFee() * nMintCount;
                    if (nChangeDust >= nFeeRequired) {
                        nChangeDust -= nFeeRequired;
                    } else if (nMintCount == 1) {
                        //Can't pay the fee and mint the change, do all change as ct
                        nChangeRemint = 0;
                        nChangeDust = nChange;
                    } else {
                        //Reduce the minting amount by the lowest denomination in order to pay fees, add the remainder to ct change
                        nChangeRemint -= nAmountSmallestDenom;
                        nChangeDust += nAmountSmallestDenom;

                        //Sanity
                        if (nChangeRemint < nAmountSmallestDenom) {
                            receipt.SetStatus("Failed to properly calculate change outputs", nStatus);
                            return false;
                        }
                    }
                } else {
                    // Mint the change as ct by default
                    nChangeRemint = 0;
                    nChangeDust = nChange;
                }

                if (nChangeRemint > 0) {
                    LOCK(cs_wallet);
                    //mint change as zerocoins and RingCT
                    CAmount nFeeRet = 0;
                    std::string strFailReason = "";
                    if (!CreateZerocoinMintTransaction(nChangeRemint, txNew, vNewMints, nFeeRet, strFailReason, vecSend,
                            /*inputtype*/OUTPUT_STANDARD, nullptr, true)) {
                        receipt.SetStatus("Failed to create mint", nStatus);
                        return false;
                    }
                }
            }

            txNew.vpout.clear();

            CTransactionRef txRef = std::make_shared<CTransaction>(txNew);

            //turn the finalized transaction into a wallet transaction
            wtxNew = CWalletTx(this, txRef);
            wtxNew.fFromMe = true;
            wtxNew.fTimeReceivedIsTxTime = true;
            wtxNew.nTimeReceived = GetAdjustedTime();

            CTransactionRecord rtx;
            if (!vecSend.empty()) {
                LOCK(cs_wallet);
                CAmount nFeeRet;
                std::string sError;
                CCoinControl coinControl;

                if (0 != pAnonWalletMain->AddStandardInputs(wtxNew, rtx, vecSend, false, nFeeRet, &coinControl, sError, true, nValueSelected)) {
                    receipt.SetStatus("Failed to add standard inputs", nStatus);
                    return error("%s: AddStandardInputs failed: %s", __func__, sError);
                }

                pAnonWalletMain->AddOutputRecordMetaData(rtx, vecSend);
            }

            CMutableTransaction mtx(*wtxNew.tx);

            //hash with only the output info in it to be used in Signature of Knowledge
            uint256 hashTxOut = wtxNew.tx->GetOutputsHash();

            //add all of the mints to the transaction as inputs
            for (CZerocoinMint& mint : vSelectedMints) {
                CTxIn newTxIn;
                if (!MintToTxIn(mint, nSecurityLevel, hashTxOut, newTxIn, receipt, libzerocoin::SpendType::SPEND)) {
                    return error("%s: %s", __func__, receipt.GetStatusMessage());
                }
                mtx.vin.push_back(newTxIn);
            }

            // Limit size
            unsigned int nBytes = ::GetSerializeSize(txNew, SER_NETWORK, PROTOCOL_VERSION);
            if (nBytes >= MAX_ZEROCOIN_TX_SIZE) {
                receipt.SetStatus(strprintf("Maximum size of transaction is too large, Size=%d", nBytes), ZTX_TOO_LARGE);
                return error("%s: %s", __func__, receipt.GetStatusMessage());
            }

            txRef = std::make_shared<CTransaction>(mtx);
            wtxNew.SetTx(txRef);
            receipt.AddTransaction(txRef, rtx);

            //now that all inputs have been added, add full tx hash to zerocoinspend records and write to db
            uint256 txHash = mtx.GetHash();
            for (CZerocoinSpend& spend : receipt.GetSpends_back()) {
                spend.SetTxHash(txHash);
                if (!WalletBatch(*this->database).WriteZerocoinSpendSerialEntry(spend))
                    receipt.SetStatus("Failed to write coin serial number into wallet", nStatus);
            }

            //Change the rtx record to the correct spot
            uint256 txidOld = rtx.GetPartialTxid();
            if (!txidOld.IsNull() && pAnonWalletMain->mapRecords.count(txidOld)) {
                pAnonWalletMain->mapRecords.erase(txidOld);
                rtx.RemovePartialTxid();
            }
            pAnonWalletMain->SaveRecord(txHash, rtx);
        }
    }

    receipt.SetStatus("Transaction Created", ZSPEND_OKAY); // Everything okay
    return true;
}

bool CWallet::GetZerocoinSeed(CKey& keyZerocoinMaster)
{
    if (IsLocked())
        return error("%s: getting zerocoin seed while wallet is locked", __func__);

    //The zerocoin master seed is derived from a specific BIP32 Account of the wallet's masterseed
    BIP32Path vPath;
    vPath.emplace_back(std::make_pair(0, true));
    vPath.emplace_back(std::make_pair(Params().BIP32_Zerocoin_Account(), true));
    CExtKey masterKey = DeriveBIP32Path(vPath);
    keyZerocoinMaster = masterKey.key;

    return true;
}

bool CWallet::GetAnonWalletSeed(CExtKey& keyMaster)
{
    if (IsLocked())
        return error("%s: getting anon wallet seed while wallet is locked", __func__);

    BIP32Path vPath;
    vPath.emplace_back(std::make_pair(0, true));
    vPath.emplace_back(std::make_pair(Params().BIP32_RingCT_Account(), true));
    keyMaster = DeriveBIP32Path(vPath);

    return true;
}

bool CWallet::DatabaseMint(CDeterministicMint& dMint)
{
    WalletBatch walletdb(*this->database);
    zTracker->Add(dMint, true);
    return true;
}

bool CWallet::GetZerocoinKey(const CBigNum& bnSerial, CKey& key)
{
    WalletBatch walletdb(*this->database);
    CZerocoinMint mint;
    if (!GetMint(GetSerialHash(bnSerial), mint))
        return error("%s: could not find serial %s in walletdb!", __func__, bnSerial.GetHex());

    return mint.GetKeyPair(key);
}

boost::thread_group* pthreadGroupAutoSpend;
void LinkAutoSpendThreadGroup(void* pthreadgroup)
{
    pthreadGroupAutoSpend = (boost::thread_group*)pthreadgroup;
}

bool StartAutoSpend()
{
    if (!pthreadGroupAutoSpend) {
        error("%s: pthreadGroupAutoSpend is null! Cannot autospend.", __func__);
        return false;
    }

    // Close any active auto spend threads before starting new threads
    if (pthreadGroupAutoSpend->size() > 0) {
        StopAutoSpend();
    }

    pthreadGroupAutoSpend->create_thread(std::bind(&AutoSpendZeroCoin));
    return true;
}

void StopAutoSpend()
{
    if (!pthreadGroupAutoSpend) {
        error("%s: pthreadGroupAutoSpend is null! Cannot stop autospending.", __func__);
        return;
    }

    if (pthreadGroupAutoSpend->size() > 0) {
        pthreadGroupAutoSpend->interrupt_all();
        pthreadGroupAutoSpend->join_all();
    }

    LogPrintf("AutoSpendZeroCoin stopped\n");
}

static libzerocoin::CoinDenomination denom_to_spend = libzerocoin::CoinDenomination::ZQ_TEN;
static int max_number_to_spend = 1;
static std::string auto_spend_address = "";

void SetAutoSpendParameters(const int& nCount, const int& nDenom, const std::string& strAddress)
{
    if (nCount >= 1 && nCount <= 3)
        max_number_to_spend = nCount;

    libzerocoin::CoinDenomination denom = libzerocoin::IntToZerocoinDenomination(nDenom);

    if (denom != libzerocoin::CoinDenomination::ZQ_ERROR)
        denom_to_spend = denom;

    auto_spend_address = strAddress;
}

void AutoSpendZeroCoin()
{
    LogPrintf("AutoSpendZeroCoin started: spending at most %d of the %d denomination per cycle\n", max_number_to_spend, libzerocoin::ZerocoinDenominationToInt(denom_to_spend));

    static CTxDestination destination;

    boost::this_thread::interruption_point();
    try {
        int64_t nMilliSeconds = 3000;
        int count = 0;

        while (true) {
            // Sleep 5 minutes between spends, but actively check to see if the thread has been interrupted every 3 seconds
            if (count < 100) {
                count++;
                boost::this_thread::interruption_point();
                MilliSleep(nMilliSeconds);
                boost::this_thread::interruption_point();
                continue;
            }
            count = 0;

            boost::this_thread::interruption_point();

            if (ShutdownRequested())
                break;

            if (IsInitialBlockDownload() || !HeadersAndBlocksSynced()) {
                LogPrintf("%s: waiting for sync cannot auto spend\n", __func__);
                continue;
            }

            auto pwallet = GetMainWallet();

            if (!pwallet) {
                LogPrintf("%s: pwallet is null cannot auto spend\n", __func__);
                continue;
            }

            if (pwallet->IsLocked() || pwallet->IsUnlockedForStakingOnly()) {
                LogPrintf("%s: pwallet is locked cannot auto spend\n", __func__);
                continue;
            }

            if (!IsValidDestination(destination)) {
                // Check to see if a new address was passed in via the argument
                std::string address = auto_spend_address;
                if (address.empty()) {
                    // See if the wallet already has an autospend address databased
                    pwallet->GetAutoSpendAddress(address);
                }

                // Create a destination from the address given or databased
                CBitcoinAddress addr(address);
                destination = addr.Get();

                // If the destination isn't valid at this point. Create a new stealth address and database it
                if (!IsValidDestination(destination)) {
                    CStealthAddress stealthAddress;

                    // Check to make sure the anon wallet is available
                    auto anonwallet = pwallet->GetAnonWallet();
                    if (!anonwallet) {
                        LogPrintf("%s: anonwallet is null cannot auto spend\n", __func__);
                        continue;
                    }

                    // Generate new address
                    if (!anonwallet->NewStealthKey(stealthAddress, 0, nullptr)) {
                        LogPrintf("%s: Failed to create new stealth address to use for auto spend\n", __func__);
                        continue;
                    }

                    destination = stealthAddress;
                }

                // Save the destination to database
                if (!pwallet->SetAutoSpendAddress(destination))
                    LogPrintf("%s : Failed to set auto spend address\n", __func__);
            }

            std::vector<CZerocoinMint> vMintsSelected;
            CZerocoinSpendReceipt receipt;

            // Get the list of mints
            std::set<CMintMeta> mints = pwallet->ListMints(true, true, false);

            int found = 0;
            for (const auto& mint : mints) {
                // If you have a mint of the selected denom to spend
                if (mint.denom == denom_to_spend) {
                    found++;
                }

                // If you have found the selected number of mints to spends
                if (found == max_number_to_spend)
                    break;
            }

            // If no mints found to spend, don't try and spend
            if (found == 0) {
                LogPrintf("%s : No more denominations of the selected type (%d) to auto spend\n", __func__, ZerocoinDenominationToInt(denom_to_spend));
                continue;
            }

            TRY_LOCK(cs_main, fLockedMain);
            if (!fLockedMain)
                continue;

            TRY_LOCK(pwallet->cs_wallet, fLockedWallet);
            if (!fLockedWallet)
                continue;

            bool fSuccess = pwallet->SpendZerocoin(found * libzerocoin::ZerocoinDenominationToInt(denom_to_spend) * COIN, 1, receipt, vMintsSelected, false, false, denom_to_spend, &destination);

            if (fSuccess) {
                LogPrintf("Successfully auto spent %d %d denomination(s)\n", found, libzerocoin::ZerocoinDenominationToInt(denom_to_spend));
            } else {
                LogPrintf("Failed to auto spend zerocoin\n");
            }
        }
    } catch (std::exception& e) {
        LogPrintf("AutoSpendZeroCoin() exception\n");
    } catch (boost::thread_interrupted) {
        LogPrintf("AutoSpendZeroCoin() interrupted\n");
    }

    LogPrintf("AutoSpendZeroCoin stopping\n");
}
