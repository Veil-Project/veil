// Copyright (c) 2017-2019 The Particl Core developers
// Copyright (c) 2018-2020 The Veil developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <veil/ringct/anonwallet.h>

#include <crypto/hmac_sha256.h>
#include <crypto/hmac_sha512.h>
#include <crypto/sha256.h>

#include <random.h>
#include <net.h>
#include <validation.h>
#include <consensus/validation.h>
#include <consensus/merkle.h>
#include <utilmoneystr.h>
#include <script/script.h>
#include <script/standard.h>
#include <script/sign.h>
#include <policy/fees.h>
#include <policy/policy.h>
#include <policy/rbf.h>
#include <wallet/coincontrol.h>
#include <veil/invalid.h>
#include <veil/ringct/blind.h>
#include <veil/ringct/anon.h>
#include <veil/zerocoin/denomination_functions.h>
#include <veil/zerocoin/zchain.h>
#include <wallet/deterministicmint.h>
#include <sync.h>
#include <txdb.h>
#include <rpc/server.h>
#include <rpc/util.h>
#include <timedata.h>
#include <wallet/fees.h>
#include <walletinitinterface.h>
#include <wallet/walletutil.h>
#include <wallet/fees.h>

#include <univalue.h>

#include <secp256k1_mlsag.h>

#include <algorithm>
#include <random>

#include <boost/algorithm/string/replace.hpp>
#include <boost/thread.hpp>


int CTransactionRecord::InsertOutput(COutputRecord &r)
{
    for (size_t i = 0; i < vout.size(); ++i) {
        if (vout[i].n == r.n) {
            vout[i] = r;
            return 0; // duplicate
        }

        if (vout[i].n < r.n) {
            continue;
        }

        vout.insert(vout.begin() + i, r);
        return 1;
    }
    vout.push_back(r);
    return 1;
};

bool CTransactionRecord::EraseOutput(uint16_t n)
{
    for (size_t i = 0; i < vout.size(); ++i) {
        if (vout[i].n != n) {
            continue;
        }

        vout.erase(vout.begin() + i);
        return true;
    }
    return false;
};

COutputRecord *CTransactionRecord::GetOutput(int n)
{
    // vout is always in order by asc n
    for (auto &r : vout) {
        if (r.n > n) {
            return nullptr;
        }
        if (r.n == n) {
            return &r;
        }
    }
    return nullptr;
};

const COutputRecord *CTransactionRecord::GetOutput(int n) const
{
    // vout is always in order by asc n
    for (auto &r : vout) {
        if (r.n > n) {
            return nullptr;
        }
        if (r.n == n) {
            return &r;
        }
    }
    return nullptr;
};

const COutputRecord *CTransactionRecord::GetChangeOutput() const
{
    for (auto &r : vout) {
        if (r.nFlags & ORF_CHANGE) {
            return &r;
        }
    }
    return nullptr;
};


int AnonWallet::Finalise()
{
    mapAddressBook.clear();
    return 0;
}

bool AnonWallet::Initialise(CExtKey* pExtMaster)
{
    std::string sError;

    bool fFirstRun = pExtMaster != nullptr;
    AnonWalletDB wdb(*walletDatabase);

    {
        LOCK(pwalletParent->cs_wallet);

        //Load all keys and accounts
        if (!fFirstRun) {
            //No master key passed in, should have masterkeyid in database
            if (!wdb.ReadNamedExtKeyId("master", idMaster))
                return error("%s: failed to read masterkey id from db", __func__);
            if (!wdb.ReadNamedExtKeyId("defaultaccount", idDefaultAccount))
                return error("%s: failed to read default account id from db", __func__);
            if (!wdb.ReadNamedExtKeyId("stealthaccount", idStealthAccount))
                return error("%s: failed to read stealth account id from db", __func__);
            //Load the change account
            idChangeAccount.SetNull();
            if (!wdb.ReadNamedExtKeyId("stealthchange", idChangeAccount)) {
                //If the change account is not created yet, then create it
                if (!IsLocked() && !CreateStealthChangeAccount(&wdb))
                    return error("%s: failed to create stealth change account ", __func__);
            }
            //Load the change address
            if (!idChangeAccount.IsNull() && !wdb.ReadNamedExtKeyId("stealthchangeaddress", idChangeAddress)) {
                auto address = GetStealthChangeAddress();
                idChangeAddress = address.GetID();
                if (!wdb.WriteNamedExtKeyId("stealthchangeaddress", idChangeAddress))
                    return error("%s: Failed to write stealth change address to wallet db", __func__);
            }

            // Load all accounts, keys, stealth addresses from db
            if (!LoadAccountCounters())
                return error("%s: failed to read account counters from db", __func__);
            if (!LoadKeys())
                return error("%s: failed to read keys from db", __func__);
            if (!LoadStealthAddresses())
                return error("%s: failed to read stealth addresses id from db", __func__);
            if (!LoadTxRecords())
                return error("%s: failed to load transaction records from db", __func__);
        } else {
            //First run needs to load up the masterseed and create the default account
            if (!MakeDefaultAccount(*pExtMaster))
                return error("%s: failed to load masterkey and create default account", __func__);
        }
    }

    //Check that database masterkey matches the currently loaded key
    CKeyID idCheck;
    if (!wdb.ReadNamedExtKeyId("master", idCheck))
        return error("%s: Failed loading anon wallet master seed id", __func__);
    if (idMaster != idCheck)
        return error("%s: Failed loading anon wallet master seed id. Expected %s got %s", __func__, idCheck.GetHex(), idDefaultAccount.GetHex());

    return true;
}

void AnonWallet::AddToSpends(const COutPoint& outpoint, const uint256& wtxid)
{
    mapTxSpends.insert(std::make_pair(outpoint, wtxid));

//    setLockedCoins.erase(outpoint);

    std::pair<TxSpends::iterator, TxSpends::iterator> range;
    range = mapTxSpends.equal_range(outpoint);
  //  SyncMetaData(range);
}

void AnonWallet::LoadToWallet(const uint256 &hash, const CTransactionRecord &rtx)
{
    std::pair<MapRecords_t::iterator, bool> ret = mapRecords.insert(std::make_pair(hash, rtx));

    MapRecords_t::iterator mri = ret.first;
    rtxOrdered.insert(std::make_pair(rtx.GetTxTime(), mri));

    // TODO: Spend only owned inputs?

    return;
};

bool AnonWallet::LoadTxRecords()
{
    LOCK(pwalletParent->cs_wallet);

    AnonWalletDB pwdb(*walletDatabase);
    Dbc *pcursor;
    if (!(pcursor = pwdb.GetCursor())) {
        throw std::runtime_error(strprintf("%s: cannot create DB cursor", __func__).c_str());
    }

    CDataStream ssKey(SER_DISK, CLIENT_VERSION);
    CDataStream ssValue(SER_DISK, CLIENT_VERSION);

    std::string sPrefix = "rtx";
    std::string strType;
    uint256 txhash;

    size_t nCount = 0;
    unsigned int fFlags = DB_SET_RANGE;
    ssKey << sPrefix;
    while (pwdb.ReadAtCursor(pcursor, ssKey, ssValue, fFlags) == 0) {
        fFlags = DB_NEXT;
        ssKey >> strType;
        if (strType != sPrefix) {
            break;
        }

        ssKey >> txhash;

        CTransactionRecord data;
        ssValue >> data;
        LoadToWallet(txhash, data);
        nCount++;
    }

    // Must load all records before marking spent.

    {
        MapRecords_t::iterator mri;
        for (const auto &ri : mapRecords) {
            const uint256 &txhash = ri.first;
            const CTransactionRecord &rtx = ri.second;

            for (const auto &prevout : rtx.vin) {
                if (rtx.nFlags & ORF_ANON_IN) {
                    CCmpPubKey ki;
                    memcpy(ki.ncbegin(), prevout.hash.begin(), 32);
                    *(ki.ncbegin()+32) = prevout.n;

                    COutPoint kiPrevout;
                    // TODO: Keep keyimages in memory
                    if (!pwdb.ReadAnonKeyImage(ki, kiPrevout)) {
                        continue;
                    }
                    AddToSpends(kiPrevout, txhash);

                    continue;
                }

                AddToSpends(prevout, txhash);

                if ((mri = mapRecords.find(prevout.hash)) != mapRecords.end()) {
                    CTransactionRecord &prevtx = mri->second;
                    if (prevtx.nIndex == -1 && !prevtx.HashUnset()) {
                        MarkConflicted(prevtx.blockHash, txhash);
                    }
                }
            }
        }
    }

    pcursor->close();


    return true;
};

isminetype AnonWallet::HaveAddress(const CTxDestination &dest) const
{
    LOCK(pwalletParent->cs_wallet);

    if (dest.type() == typeid(CStealthAddress)) {
        CStealthAddress sx = boost::get<CStealthAddress>(dest);
        return HaveStealthAddress(sx);
    }

    if (dest.type() == typeid(CKeyID)) {
        CKeyID id = boost::get<CKeyID>(dest);
        if (mapStealthDestinations.count(id))
            return ISMINE_SPENDABLE;
        if (mapKeyPaths.count(id))
            return ISMINE_SPENDABLE;
    }

    return ISMINE_NO;
};

bool AnonWallet::HaveTransaction(const uint256 &txhash) const
{
    return mapRecords.count(txhash);
};

bool AnonWallet::GetKey(const CKeyID &address, CKey &keyOut) const
{
    LOCK(pwalletParent->cs_wallet);

    if (mapKeyPaths.count(address)) {
        if (!RegenerateKey(address, keyOut))
            return error("%s: failed to regenerate keyid %s", __func__, address.GetHex());
    }

    if (pwalletParent->GetKey(address, keyOut))
        return true;

    // Last check if it is a stealth destination that was stored while the wallet was locked
    if (!mapStealthDestinations.count(address))
        return false;

    AnonWalletDB wdb(*walletDatabase);
    std::vector<uint8_t> vchEphemPK;
    if (!wdb.ReadStealthDestinationMeta(address, vchEphemPK))
        return error("%s: failed to find stealth destination meta data", __func__);

    CKeyID idStealth = mapStealthDestinations.at(address);
    CStealthAddress addr = mapStealthAddresses.at(idStealth);

    CKey sShared;
    ec_point pkExtracted;
    if (StealthSecret(addr.scan_secret, vchEphemPK, addr.spend_pubkey, sShared, pkExtracted) != 0)
        return error("%s: failed to generate stealth secret", __func__);

    return CalculateStealthDestinationKey(addr.GetSpendKeyID(), address, sShared, keyOut);
};

bool AnonWallet::GetPubKey(const CKeyID &address, CPubKey& pkOut) const
{
    LOCK(pwalletParent->cs_wallet);
    if (mapKeyPaths.count(address)) {
        CKey keyTemp;
        if (!RegenerateKey(address, keyTemp))
            return error("%s: FIXME: regenerate key failed, but should not have!", __func__);
        pkOut = keyTemp.GetPubKey();
    }

    return pwalletParent->GetPubKey(address, pkOut);
}

isminetype AnonWallet::HaveStealthAddress(const CStealthAddress &sxAddr) const
{
    AssertLockHeld(pwalletParent->cs_wallet);

    auto si = mapStealthAddresses.find(sxAddr.GetID());
    if (si != mapStealthAddresses.end()) {
        return ISMINE_SPENDABLE;

        //todo, do not currently have a good way of knowing whether address is sendto
//        isminetype imSpend = IsMine(si->second.spend_secret_id);
//        if (imSpend & ISMINE_SPENDABLE) {
//            return imSpend; // Retain ISMINE_HARDWARE_DEVICE flag if present
//        }
//        return ISMINE_WATCH_ONLY;
    }

    return ISMINE_NO;
};

bool AnonWallet::GetStealthAddressScanKey(CStealthAddress &sxAddr) const
{
    auto si = mapStealthAddresses.find(sxAddr.GetID());
    if (si != mapStealthAddresses.end()) {
        sxAddr.scan_secret = si->second.scan_secret;
        return true;
    }

    return false;
};

bool AnonWallet::GetStealthAddressSpendKey(CStealthAddress &sxAddr, CKey &key) const
{
    return RegenerateKey(sxAddr.GetSpendKeyID(), key);
}


bool AnonWallet::GetAddressMeta(const CStealthAddress& address, CKeyID& idAccount, std::string& strPath) const
{
    if (!mapStealthAddresses.count(address.GetID()))
        return false;

    CStealthAddress sxAddr = mapStealthAddresses.at(address.GetID());
    if (mapKeyPaths.count(sxAddr.GetID())) {
        auto accountpair = mapKeyPaths.at(sxAddr.GetID());
        idAccount = accountpair.first;
        std::string strPathToKey = BIP32PathToString(accountpair.second);

        //The path of the account is what provides context
        if (!mapKeyPaths.count(idAccount))
            return false;
        accountpair = mapKeyPaths.at(idAccount);
        strPath = BIP32PathToString(accountpair.second);
        strPath += strPathToKey;
    }

    return true;
}

CStealthAddress GenerateStealthAddressFromIndex(const CExtKey& keyStealthAddress, int nIndex)
{
    //Generate the extkey for spend and scan
    BIP32Path vPathNew;
    vPathNew.emplace_back(nIndex, true);
    CExtKey extKeyScan = DeriveKeyFromPath(keyStealthAddress, vPathNew);
    vPathNew.clear();
    vPathNew.emplace_back(nIndex+1, true);
    CExtKey extKeySpend = DeriveKeyFromPath(keyStealthAddress, vPathNew);
    CKey keyScan = extKeyScan.key;
    CKey keySpend = extKeySpend.key;
    CPubKey pkSpend = keySpend.GetPubKey();

    // Make a stealth address
    CStealthAddress stealthAddress;
    stealthAddress.SetNull();
    stealthAddress.scan_pubkey = keyScan.GetPubKey().Raw();
    stealthAddress.spend_pubkey = pkSpend.Raw();
    stealthAddress.scan_secret.Set(keyScan.begin(), true);
    stealthAddress.spend_secret_id = pkSpend.GetID();

    return stealthAddress;
}

//! Return the last index in this account that has received coins
int AnonWallet::GetLastUsedAddressIndex(const CKeyID& idAccount) const
{
    if (!mapAccountCounter.count(idAccount))
        return -1;

    int nLastGenerated = mapAccountCounter.at(idAccount);
    int n = nLastGenerated - 1;
    while (n > 0) {
        //Regenerate address for this path
        CExtKey keyStealthAccount;
        if (!RegenerateKeyFromIndex(idAccount, n, keyStealthAccount))
            return -1;

        //Regenerate the stealth address and then get full address from 
        CStealthAddress stealthAddress = GenerateStealthAddressFromIndex(keyStealthAccount, 1);
        if (!mapStealthAddresses.count(stealthAddress.GetID()))
            return -2;

        stealthAddress = mapStealthAddresses.at(stealthAddress.GetID());

        //If this address has any stealth destinations, then it is used and is the last used address.
        if (!stealthAddress.setStealthDestinations.empty())
            return n;

        n--;
    }
    return -1;
}

void AnonWallet::ForgetUnusedStealthAddresses(int nBuffer)
{
    int nIndexLastUsed = GetLastUsedAddressIndex(idStealthAccount);
    if (nIndexLastUsed < 0)
        return;

    // Get the last used address (this must be done after rescanning the chain)
    int nLastGenerated = mapAccountCounter.at(idStealthAccount);
    if (nLastGenerated - nIndexLastUsed <= nBuffer)
        return;

    // Remove knowledge of each account beyond the buffer.
    AnonWalletDB wdb(*walletDatabase);
    CExtKey keyStealthAddress;
    int n = nIndexLastUsed + nBuffer;

    while (n <= nLastGenerated && RegenerateKeyFromIndex(idStealthAccount, n, keyStealthAddress)) {
        // Erase stealth address
        auto address = GenerateStealthAddressFromIndex(keyStealthAddress, 0);
        wdb.EraseStealthAddress(address);
        mapStealthAddresses.erase(address.GetID());
        mapKeyPaths.erase(address.GetID());

        // Erase account
        auto idAccount = keyStealthAddress.key.GetPubKey().GetID();
        wdb.EraseExtKey(idAccount);
        mapKeyPaths.erase(idAccount);
        n++;
    }

    // Change account counter back
    mapAccountCounter[idStealthAccount] = nIndexLastUsed + nBuffer;
    wdb.WriteAccountCounter(idStealthAccount, nIndexLastUsed + nBuffer);
}


bool AnonWallet::ImportStealthAddress(const CStealthAddress &sxAddr, const CKey &skSpend)
{
    LOCK(pwalletParent->cs_wallet);

    // Must add before changing spend_secret
    mapStealthAddresses.emplace(sxAddr.GetID(), sxAddr);

    bool fOwned = skSpend.IsValid();

    if (fOwned) {
        // Owned addresses can only be added when wallet is unlocked
        if (IsLocked()) {
            mapStealthAddresses.erase(sxAddr.GetID());
            return werror("%s: Wallet must be unlocked.", __func__);
        }

        CPubKey pk = skSpend.GetPubKey();
        if (!pwalletParent->AddKeyPubKey(skSpend, pk)) {
            mapStealthAddresses.erase(sxAddr.GetID());
            return werror("%s: AddKeyPubKey failed.", __func__);
        }
    }

    if (!AnonWalletDB(*walletDatabase).WriteStealthAddress(sxAddr)) {
        mapStealthAddresses.erase(sxAddr.GetID());
        return werror("%s: WriteStealthAddress failed.", __func__);
    }

    return true;
};

std::map<CTxDestination, CAmount> AnonWallet::GetAddressBalances()
{
    std::map<CTxDestination, CAmount> balances;

    {
        LOCK(pwalletParent->cs_wallet);
        for (const auto &ri : mapRecords)
        {
            const uint256 &txhash = ri.first;
            const CTransactionRecord &rtx = ri.second;

            if (!IsTrusted(txhash, rtx.blockHash, rtx.nIndex))
                continue;

            for (const auto &r : rtx.vout)
            {
                if (r.nType != OUTPUT_STANDARD
                    && r.nType != OUTPUT_CT
                    && r.nType != OUTPUT_RINGCT)
                    continue;

                if (!(r.nFlags & ORF_OWNED))
                    continue;

                CTxDestination addr;
                if (!ExtractDestination(r.scriptPubKey, addr))
                    continue;

                CAmount n =  IsSpent(txhash, r.n) ? 0 : r.GetRawValue();

                std::pair<std::map<CTxDestination, CAmount>::iterator, bool> ret;
                CKeyID keyID;
                keyID.SetNull();
                if (addr.type() != typeid(CKeyID)) continue;
                keyID = boost::get<CKeyID>(addr);
                if (keyID.IsNull()) continue;
                CStealthAddress sx;
                if (!GetStealthAddress(keyID, sx)) continue;
                ret = balances.insert(std::pair<CTxDestination, CAmount>(CTxDestination(sx), n));
                if (!ret.second) // update existing record
                    ret.first->second += n;
            };
        };
    }

    return balances;
}


isminetype AnonWallet::IsMine(const CTxIn& txin) const
{
    if (txin.IsAnonInput()) {
        //todo, this causing issues?
        return ISMINE_NO;
    }

    LOCK(pwalletParent->cs_wallet);

    MapRecords_t::const_iterator mri = mapRecords.find(txin.prevout.hash);
    if (mri != mapRecords.end())
    {
        const COutputRecord *oR = mri->second.GetOutput(txin.prevout.n);

        if (oR)
        {
            if (oR->nFlags & ORF_OWNED)
                return ISMINE_SPENDABLE;
            /* TODO
            if ((filter & ISMINE_WATCH_ONLY)
                && (oR->nFlags & ORF_WATCH_ONLY))
                return ISMINE_WATCH_ONLY;
            */
        };
    };

    return ISMINE_NO;
};

isminetype AnonWallet::IsMine(const CTxOutBase *txout) const
{
    switch (txout->nVersion)
    {
        case OUTPUT_STANDARD:
            return ISMINE_NO;
        case OUTPUT_CT:
        {
            auto* out = (CTxOutCT*)txout;
            CTxDestination dest;
            if (ExtractDestination(*out->GetPScriptPubKey(), dest)) {
                if (!HaveAddress(dest)) {
                    if (dest.which() == 1) //ckeyid could be from cwallet
                        return pwalletParent->IsMine(dest);
                    return ISMINE_NO;
                }
                return ISMINE_SPENDABLE;
            }
            break;
        }
        case OUTPUT_RINGCT:
        {
            CKeyID keyID = ((CTxOutRingCT*) txout)->pk.GetID();
            if (mapKeyPaths.count(keyID))
                return ISMINE_SPENDABLE;
            if (pwalletParent->HaveKey(keyID))
                return ISMINE_SPENDABLE;
            if (HaveAddress(keyID))
                return ISMINE_SPENDABLE;
            if (pwalletParent->IsMine(keyID))
                return ISMINE_SPENDABLE;
        }
        default:
            return ISMINE_NO;
    };

    return ISMINE_NO;
}

bool AnonWallet::IsMine(const CTransaction &tx) const
{
    for (const auto &txout : tx.vpout)
        if (IsMine(txout.get()))
            return true;
    return false;
}

bool AnonWallet::IsFromMe(const CTransaction& tx) const
{
    return (GetDebit(tx, ISMINE_ALL) > 0);
}

// Note that this function doesn't distinguish between a 0-valued input,
// and a not-"is mine" (according to the filter) input.
CAmount AnonWallet::GetDebit(const CTxIn &txin, const isminefilter &filter) const
{
    {
        LOCK(pwalletParent->cs_wallet);
        MapRecords_t::const_iterator mri = mapRecords.find(txin.prevout.hash);
        if (mri != mapRecords.end())
        {
            const COutputRecord *oR = mri->second.GetOutput(txin.prevout.n);

            if (oR)
            {
                if ((filter & ISMINE_SPENDABLE)
                    && (oR->nFlags & ORF_OWNED))
                    return oR->GetAmount();
                /* TODO
                if ((filter & ISMINE_WATCH_ONLY)
                    && (oR->nFlags & ORF_WATCH_ONLY))
                    return oR->nValue;
                */
            }
        };
    } // pwalletParent->cs_wallet
    return 0;
};

CAmount AnonWallet::GetDebit(const CTransaction& tx, const isminefilter& filter) const
{
    CAmount nDebit = 0;
    for (auto &txin : tx.vin) {
        nDebit += GetDebit(txin, filter);
        if (!MoneyRange(nDebit))
            throw std::runtime_error(std::string(__func__) + ": value out of range");
    }

    return nDebit;
}

CAmount AnonWallet::GetDebit(AnonWalletDB *pwdb, const CTransactionRecord &rtx, const isminefilter& filter) const
{
    CAmount nDebit = 0;

    LOCK(pwalletParent->cs_wallet);
    COutPoint kiPrevout;
    for (const auto &prevout : rtx.vin)
    {
        const auto *pPrevout = &prevout;
        if (rtx.nFlags & ORF_ANON_IN)
        {
            CCmpPubKey ki;
            memcpy(ki.ncbegin(), prevout.hash.begin(), 32);
            *(ki.ncbegin()+32) = prevout.n;

            // TODO: Keep keyimages in memory
            if (!pwdb->ReadAnonKeyImage(ki, kiPrevout))
                continue;
            pPrevout = &kiPrevout;
        };
        auto mri = mapRecords.find(pPrevout->hash);
        if (mri != mapRecords.end())
        {
            const COutputRecord *oR = mri->second.GetOutput(pPrevout->n);

            if (oR
                && (filter & ISMINE_SPENDABLE)
                && (oR->nFlags & ORF_OWNED))
                 nDebit += oR->GetAmount();
        };
    };

    return nDebit;
};

bool AnonWallet::IsAllFromMe(const CTransaction& tx, const isminefilter& filter) const
{
    LOCK(pwalletParent->cs_wallet);

    for (const CTxIn& txin : tx.vin)
    {
        auto mri = mapRecords.find(txin.prevout.hash);
        if (mri != mapRecords.end())
        {
            const COutputRecord *oR = mri->second.GetOutput(txin.prevout.n);
            if (!oR)
                return false;
            if ((filter & ISMINE_SPENDABLE)
                && (oR->nFlags & ORF_OWNED))
            {
                continue;
            };
            /* TODO
            if ((filter & ISMINE_WATCH_ONLY)
                && (oR->nFlags & ORF_WATCH_ONLY))
                return oR->nValue;
            */
        };
        return false; // any unknown inputs can't be from us
    };
    return true;
};

CAmount AnonWallet::GetCredit(const COutPoint& outpoint, const isminefilter& filter) const
{
    auto mri = mapRecords.find(outpoint.hash);
    if (mri == mapRecords.end())
        return 0;

    const auto& txRecord = mri->second;
    if (txRecord.vout.size() <= outpoint.n)
        return 0;

    auto outRecord = txRecord.GetOutput(outpoint.n);
    if (!outRecord) {
        //LogPrintf("%s: FIXME: txRecord does not have outpoint %s is!", __func__, outpoint.ToString());
        return 0;
    }
    auto nValue = outRecord->GetAmount();
    if (!MoneyRange(nValue)) {
        //LogPrintf("%s: FIXME: txRecord value %s outpoint %s is too high!", __func__, FormatMoney(nValue), outpoint.ToString());
        return 0;
    }

    return nValue;
}

CAmount AnonWallet::GetCredit(const CTransaction &tx, const isminefilter &filter) const
{
    CAmount nCredit = 0;
    auto hash = tx.GetHash();
    MapRecords_t::const_iterator mri = mapRecords.find(hash);
    if (mri != mapRecords.end()) {
        const CTransactionRecord& rtx = mri->second;
        for (auto& record : rtx.vout) {
            //Credit only comes from receiving. Output Data is a tx_fee.
            if (record.IsReceive() && record.nType != OUTPUT_DATA) {
                nCredit += record.GetAmount();
            }
        }
    }

    return nCredit;
};

void AnonWallet::GetCredit(const CTransaction &tx, CAmount &nSpendable, CAmount &nWatchOnly) const
{
    nSpendable = 0;
    nWatchOnly = 0;
    for (const auto &txout : tx.vpout)
    {
        if (!txout->IsType(OUTPUT_STANDARD))
            continue;

        isminetype ismine = IsMine(txout.get());

        if (ismine & ISMINE_SPENDABLE)
            nSpendable += txout->GetValue();
        if (ismine & ISMINE_WATCH_ONLY)
            nWatchOnly += txout->GetValue();
    };

    if (!MoneyRange(nSpendable))
        throw std::runtime_error(std::string(__func__) + ": value out of range");
    if (!MoneyRange(nWatchOnly))
        throw std::runtime_error(std::string(__func__) + ": value out of range");
    return;
};

bool AnonWallet::GetOutputRecord(const COutPoint& outpoint, COutputRecord& record) const
{
    if (!mapRecords.count(outpoint.hash))
        return false;
    auto& r = mapRecords.at(outpoint.hash);
    auto precord = r.GetOutput(outpoint.n);
    if (!precord)
        return error("%s: Could no locate output record for tx %s n=%d. FIXME", __func__, outpoint.hash.GetHex(), outpoint.n);
    record = *precord;
    return true;
}

CAmount AnonWallet::GetOutputValue(const COutPoint &op, bool fAllowTXIndex)
{
    MapRecords_t::iterator itr;
    if ((itr = mapRecords.find(op.hash)) != mapRecords.end()) {
        const COutputRecord *rec = itr->second.GetOutput(op.n);
        if (rec) {
            return rec->GetAmount();
        }
        CStoredTransaction stx;
        if (!AnonWalletDB(*walletDatabase).ReadStoredTx(op.hash, stx)) { // TODO: cache / use mapTempWallet
            LogPrintf("%s: ReadStoredTx failed for %s.\n", __func__, op.hash.ToString());
            return 0;
        }
        if (stx.tx->GetNumVOuts() > op.n) {
            return stx.tx->vpout[op.n]->GetValue();
        }
        return 0;
    }

    uint256 hashBlock;
    CTransactionRef txOut;
    if (GetTransaction(op.hash, txOut, Params().GetConsensus(), hashBlock, true)) {
        if (txOut->GetNumVOuts() > op.n) {
            return txOut->vpout[op.n]->GetValue();
        }
        return 0;
    }

    return 0;
};

CAmount AnonWallet::GetOwnedOutputValue(const COutPoint &op, isminefilter filter)
{
    MapRecords_t::iterator itr;
    if ((itr = mapRecords.find(op.hash)) != mapRecords.end())
    {
        const COutputRecord *rec = itr->second.GetOutput(op.n);
        if (!rec)
            return 0;
        if ((filter & ISMINE_SPENDABLE && rec->nFlags & ORF_OWNED)
            || (filter & ISMINE_WATCH_ONLY && rec->nFlags & ORF_OWN_WATCH))
            return rec->GetAmount();
        return 0;
    };

    return 0;
};

bool AnonWallet::InMempool(const uint256 &hash) const
{

    LOCK(mempool.cs);
    return mempool.exists(hash);
};

bool hashUnset(const uint256 &hash)
{
    return (hash.IsNull() || hash == ABANDON_HASH);
}

int AnonWallet::GetDepthInMainChain(const uint256 &blockhash, int nIndex) const
{
    if (hashUnset(blockhash))
        return 0;

    AssertLockHeld(cs_main);

    // Find the block it claims to be in
    BlockMap::iterator mi = mapBlockIndex.find(blockhash);
    if (mi == mapBlockIndex.end())
        return 0;
    CBlockIndex *pindex = (*mi).second;
    if (!pindex || !chainActive.Contains(pindex))
        return 0;

    //pindexRet = pindex;
    return ((nIndex == -1) ? (-1) : 1) * (chainActive.Height() - pindex->nHeight + 1);
};

bool AnonWallet::IsTrusted(const uint256 &txhash, const uint256 &blockhash, int nIndex) const
{
    //if (!CheckFinalTx(*this))
    //    return false;
    //if (tx->IsCoinStake() && hashUnset()) // ignore failed stakes
    //    return false;
    int nDepth = GetDepthInMainChain(blockhash, nIndex);
    if (nDepth >= 1)
        return true;
    if (nDepth < 0)
        return false;
    //if (!m_spend_zero_conf_change || !IsFromMe(ISMINE_ALL)) // using wtx's cached debit
    //    return false;

    // Don't trust unconfirmed transactions from us unless they are in the mempool.
    CTransactionRef ptx = mempool.get(txhash);
    if (!ptx)
        return false;

    // Trusted if all inputs are from us and are in the mempool:
    for (const auto &txin : ptx->vin) {
        // Transactions not sent by us: not trusted
        MapRecords_t::const_iterator rit = mapRecords.find(txin.prevout.hash);
        if (rit != mapRecords.end()) {
            const COutputRecord *oR = rit->second.GetOutput(txin.prevout.n);

            if (!oR || !(oR->nFlags & ORF_OWNED))
                return false;

            continue;
        }

        const CWalletTx *parent = pwalletParent->GetWalletTx(txin.prevout.hash);
        if (parent == nullptr)
            return false;

        const CTxOutBase *parentOut = parent->tx->vpout[txin.prevout.n].get();
        if (IsMine(parentOut) != ISMINE_SPENDABLE)
            return false;
    }

    return true;
}


CAmount AnonWallet::GetBalance(const isminefilter& filter, const int min_depth) const
{
    CAmount nBalance = 0;

    LOCK2(cs_main, pwalletParent->cs_wallet);

    for (const auto &ri : mapRecords) {
        const uint256 &txhash = ri.first;
        const CTransactionRecord &rtx = ri.second;
        if (!IsTrusted(txhash, rtx.blockHash, rtx.nIndex) || GetDepthInMainChain(rtx.blockHash, rtx.nIndex) < min_depth)
            continue;

        for (const COutputRecord &r : rtx.vout) {
            if (r.nType == OUTPUT_STANDARD && (((filter & ISMINE_SPENDABLE) && (r.nFlags & ORF_OWNED))
                    || ((filter & ISMINE_WATCH_ONLY) && (r.nFlags & ORF_OWN_WATCH))) && !IsSpent(txhash, r.n)) {
                nBalance += r.GetAmount();
	    }
        }

        if (!MoneyRange(nBalance))
            throw std::runtime_error(std::string(__func__) + ": value out of range");
    }

    return nBalance;
}

CAmount AnonWallet::GetSpendableBalance() const
{
    // Returns a value to be compared against reservebalance, includes stakeable watch-only balance.
    if (m_have_spendable_balance_cached)
        return m_spendable_balance_cached;

    CAmount nBalance = 0;

    LOCK2(cs_main, pwalletParent->cs_wallet);

    for (const auto &ri : mapRecords) {
        const auto &txhash = ri.first;
        const auto &rtx = ri.second;
        if (!IsTrusted(txhash, rtx.blockHash, rtx.nIndex)) {
            continue;
        }

        for (const auto &r : rtx.vout) {
            if (r.nType == OUTPUT_STANDARD && (r.nFlags & ORF_OWNED || r.nFlags) && !IsSpent(txhash, r.n))
                nBalance += r.GetAmount();
        }

        if (!MoneyRange(nBalance)) {
            throw std::runtime_error(std::string(__func__) + ": value out of range");
        }
    }

    m_spendable_balance_cached = nBalance;
    m_have_spendable_balance_cached = true;

    return m_spendable_balance_cached;
};

CAmount AnonWallet::GetUnconfirmedBalance() const
{
    CAmount nBalance = 0;

    LOCK2(cs_main, pwalletParent->cs_wallet);

    for (const auto &ri : mapRecords)
    {
        const auto &txhash = ri.first;
        const auto &rtx = ri.second;

        if (IsTrusted(txhash, rtx.blockHash))
            continue;

        CTransactionRef ptx = mempool.get(txhash);
        if (!ptx)
            continue;

        for (const auto &r : rtx.vout)
        {
            if (r.nFlags & ORF_OWNED && !IsSpent(txhash, r.n))
                nBalance += r.GetAmount();
        };

        if (!MoneyRange(nBalance))
            throw std::runtime_error(std::string(__func__) + ": value out of range");
    };

    return nBalance;
};

CAmount AnonWallet::GetBlindBalance(const int min_depth)
{
    CAmount nBalance = 0;

    LOCK2(cs_main, pwalletParent->cs_wallet);

    for (const auto &ri : mapRecords)
    {
        const auto &txhash = ri.first;
        const auto &rtx = ri.second;
        int nDepth = GetDepthInMainChain(rtx.blockHash, 0);

        if (!IsTrusted(txhash, rtx.blockHash))
            continue;

        if (min_depth > nDepth)
            continue;

        for (const auto &r : rtx.vout)
        {
            if (r.nType == OUTPUT_CT
                && r.nFlags & ORF_OWNED && !IsSpent(txhash, r.n))
                nBalance += r.GetAmount();
        };

        if (!MoneyRange(nBalance))
            throw std::runtime_error(std::string(__func__) + ": value out of range");
    };
    return nBalance;
};

CAmount AnonWallet::GetAnonBalance(const int min_depth)
{
    CAmount nBalance = 0;

    LOCK2(cs_main, pwalletParent->cs_wallet);
    for (const auto &ri : mapRecords)
    {
        const auto &txhash = ri.first;
        const auto &rtx = ri.second;
        int nDepth = GetDepthInMainChain(rtx.blockHash, 0);

        if (!IsTrusted(txhash, rtx.blockHash))
            continue;

        if (min_depth > nDepth)
            continue;

        for (const auto &r : rtx.vout)
        {
            if (r.nType == OUTPUT_RINGCT
                && r.nFlags & ORF_OWNED && !IsSpent(txhash, r.n)) {
                nBalance += r.GetAmount();
            }
        };

        if (!MoneyRange(nBalance))
            throw std::runtime_error(std::string(__func__) + ": value out of range");
    };
    return nBalance;
}

bool AnonWallet::GetBalances(BalanceList &bal)
{
    assert(pwalletParent);
    LOCK2(cs_main, pwalletParent->cs_wallet);

    for (const auto &ri : mapRecords) {
        const auto &txhash = ri.first;
        const auto &rtx = ri.second;

        bool fTrusted = IsTrusted(txhash, rtx.blockHash);
        int nDepth = GetDepthInMainChain(rtx.blockHash, 0);
        bool fConfirmed = nDepth > 11;
        bool fInMempool = false;
        if (!fTrusted) {
            CTransactionRef ptx = mempool.get(txhash);
            fInMempool = !ptx ? false : true;
        }

        for (const auto &r : rtx.vout) {
            if (!(r.nFlags & ORF_OWN_ANY)
                || IsSpent(txhash, r.n)) {
                continue;
            }
            switch (r.nType) {
                case OUTPUT_RINGCT:
                    if (!(r.nFlags & ORF_OWNED) || r.IsSpent())
                        continue;
                    if (fTrusted && !fConfirmed)
                        bal.nRingCTImmature += r.GetAmount();
                    else if (fTrusted && fConfirmed)
                        bal.nRingCT += r.GetAmount();
                    else if (fInMempool)
                        bal.nRingCTUnconf += r.GetAmount();
                    break;
                case OUTPUT_CT:
                    if (!(r.nFlags & ORF_OWNED) || r.IsSpent())
                        continue;
                    if(fTrusted && !fConfirmed)
                        bal.nCTImmature += r.GetAmount();
                    else if (fTrusted && fConfirmed)
                        bal.nCT += r.GetAmount();
                    else if (fInMempool)
                        bal.nCTUnconf += r.GetAmount();
                    break;
                case OUTPUT_STANDARD:
                    break;
                default:
                    break;
            }
        }
    }

    //if (!MoneyRange(nBalance))
    //    throw std::runtime_error(std::string(__func__) + ": value out of range");

    return true;
};

void AnonWallet::Lock()
{
    pkeyMaster.reset();
}

bool AnonWallet::IsLocked() const
{
    return pkeyMaster.get() == nullptr || pwalletParent->IsUnlockedForStakingOnly();
}

CAmount AnonWallet::GetAvailableAnonBalance(const CCoinControl* coinControl) const
{
    LOCK2(cs_main, pwalletParent->cs_wallet);

    CAmount balance = 0;
    std::vector<COutputR> vCoins;
    AvailableAnonCoins(vCoins, true, coinControl);
    for (const COutputR& out : vCoins) {
        if (out.fSpendable) {
            const COutputRecord *oR = out.rtx->second.GetOutput(out.i);
            if (!oR)
                continue;
            balance += oR->GetAmount();
        }
    }
    return balance;
}

CAmount AnonWallet::GetAvailableBlindBalance(const CCoinControl* coinControl) const
{
    LOCK2(cs_main, pwalletParent->cs_wallet);

    CAmount balance = 0;
    std::vector<COutputR> vCoins;
    AvailableBlindedCoins(vCoins, true, coinControl);
    for (const COutputR& out : vCoins) {
        if (out.fSpendable) {
            const COutputRecord *oR = out.rtx->second.GetOutput(out.i);
            if (!oR)
                continue;
            balance += oR->GetAmount();
        }
    }
    return balance;
}

bool AnonWallet::IsChange(const CTxOutBase *txout) const
{
    const CScript *ps = txout->GetPScriptPubKey();
    if (ps)
    {
        auto mine = pwalletParent->IsMine(txout);
        if (!mine)
            return false;
    };
    return false;
}

void AnonWallet::ParseAddressForMetaData(const CTxDestination &addr, COutputRecord &rec)
{
    if (addr.type() == typeid(CStealthAddress))
    {
        CStealthAddress sx = boost::get<CStealthAddress>(addr);
        rec.AddStealthAddress(sx.GetID());
    } else if (addr.type() == typeid(CExtKeyPair))
    {
        CExtKeyPair ek = boost::get<CExtKeyPair>(addr);
        /*
        rec.vPath.resize(21);
        rec.vPath[0] = ORA_EXTKEY;
        CKeyID eid = ek.GetID()();
        memcpy(&rec.vPath[1], eid.begin(), 20)
        */
    } else if (addr.type() == typeid(CKeyID))
    {
        //ORA_STANDARD
    }
    return;
}

void AnonWallet::MarkInputsAsPendingSpend(CTransactionRecord &rtx)
{
    for (const COutPoint& outpointIn : rtx.vin) {
        auto it = mapRecords.find(outpointIn.hash);
        if (it == mapRecords.end())
            continue;
        COutputRecord* outputRecord = it->second.GetOutput(outpointIn.n);
        if (!outputRecord)
            continue;
        outputRecord->MarkPendingSpend(true);
        CTransactionRecord recordUpdated = it->second;
        auto txid = it->first;
        SaveRecord(txid, recordUpdated);
    }
}

void AnonWallet::AddOutputRecordMetaData(CTransactionRecord &rtx, std::vector<CTempRecipient> &vecSend)
{
    for (const auto &r : vecSend) {
        COutputRecord rec;

        rec.n = r.n;
        rec.nType = r.nType;
        rec.SetValue(r.nAmount);
        rec.sNarration = r.sNarration;
        rec.scriptPubKey = r.scriptPubKey;

        if (r.fChange)
            rec.nFlags |= ORF_CHANGE;
        if (r.isMine)
            rec.nFlags |= ORF_OWNED;

        if (!rec.IsBasecoin() && !r.scriptPubKey.empty())
            rec.scriptPubKey = r.scriptPubKey;

        ParseAddressForMetaData(r.address, rec);
        rtx.InsertOutput(rec);
    }
}

bool AnonWallet::ExpandTempRecipients(std::vector<CTempRecipient> &vecSend, std::string &sError)
{
    LOCK(pwalletParent->cs_wallet);
    //uint32_t nChild;
    for (size_t i = 0; i < vecSend.size(); ++i) {
        CTempRecipient &r = vecSend[i];

        if (r.nType == OUTPUT_STANDARD) {
            if (r.address.type() == typeid(CStealthAddress)) {
                CStealthAddress sx = boost::get<CStealthAddress>(r.address);
                r.isMine = mapStealthAddresses.count(sx.GetID()) > 0;
                CKey sShared;
                ec_point pkSendTo;

                int k, nTries = 24;
                for (k = 0; k < nTries; ++k) { // if StealthSecret fails try again with new ephem key
                    r.sEphem.MakeNewKey(true);
                    if (StealthSecret(r.sEphem, sx.scan_pubkey, sx.spend_pubkey, sShared, pkSendTo) == 0) {
                        break;
                    }
                }

                if (k >= nTries) {
                    sError = strprintf("Could not generate receiving public key.");
                    return error("%s: %s", __func__, sError);
                }

                CPubKey pkEphem = r.sEphem.GetPubKey();
                r.pkTo = CPubKey(pkSendTo);
                CKeyID idTo = r.pkTo.GetID();
                r.scriptPubKey = GetScriptForDestination(idTo);

                CTempRecipient rd;
                rd.nType = OUTPUT_DATA;

                if (!MakeStealthData(r.sNarration, sx.prefix, sShared, pkEphem, rd.vData, r.nStealthPrefix, sError)) {
                    // TODO: MakeStealthData currently won't return failure
                    return false;
                }

                if (r.isMine) {
                    if (!RecordOwnedStealthDestination(sShared, sx.GetID(), idTo)) {
                        sError = strprintf("Failed to record stealth destination.");
                        return error("%s: %s", __func__, sError);
                    }
                }

                vecSend.insert(vecSend.begin() + (i+1), rd);
                i++; // skip over inserted output
            } else {
                if (r.address.type() == typeid(CExtKeyPair)) {
                    throw std::runtime_error(strprintf("%s: sending to extkeypair", __func__));
                } else if (r.address.type() == typeid(CKeyID)) {
                    r.scriptPubKey = GetScriptForDestination(r.address);
                } else {
                    if (!r.fScriptSet) {
                        r.scriptPubKey = GetScriptForDestination(r.address);
                        if (r.scriptPubKey.empty()) {
                            sError = strprintf("Unknown address type and no script set.");
                            return error("%s: %s", __func__, sError);
                        }
                    }
                }
            }
        } else if (r.nType == OUTPUT_CT) {
            CKey sEphem = r.sEphem;
            if (!sEphem.IsValid())
                sEphem.MakeNewKey(true);

            if (r.address.type() == typeid(CStealthAddress)) {
                CStealthAddress sx = boost::get<CStealthAddress>(r.address);
                r.isMine = mapStealthAddresses.count(sx.GetID());
                CKey sShared;
                ec_point pkSendTo;

                int k, nTries = 24;
                for (k = 0; k < nTries; ++k) {
                    if (StealthSecret(sEphem, sx.scan_pubkey, sx.spend_pubkey, sShared, pkSendTo) == 0) {
                        if (sShared.IsValid())
                            break;
                    }
                    sEphem.MakeNewKey(true);
                }

                if (k >= nTries) {
                    sError = strprintf("Could not generate receiving public key.");
                    return error("%s: %s", __func__, sError);
                }

                r.pkTo = CPubKey(pkSendTo);
                CKeyID idTo = r.pkTo.GetID();

                r.scriptPubKey = GetScriptForDestination(idTo);
                if (sx.prefix.number_bits > 0) {
                    r.nStealthPrefix = FillStealthPrefix(sx.prefix.number_bits, sx.prefix.bitfield);
                }

                if (r.isMine) {
                    if (!RecordOwnedStealthDestination(sShared, sx.GetID(), idTo)) {
                        sError = strprintf("Failed to record stealth destination.");
                        return error("%s: %s", __func__, sError);
                    }
                }
            } else if (r.address.type() == typeid(CExtKeyPair)) {
                throw std::runtime_error(strprintf("%s: sending to extkeypair", __func__));
            } else if (r.address.type() == typeid(CKeyID)) {
                // Need a matching public key
                CKeyID idTo = boost::get<CKeyID>(r.address);
                r.scriptPubKey = GetScriptForDestination(idTo);
            } else if (!r.fScriptSet) {
                r.scriptPubKey = GetScriptForDestination(r.address);
                if (r.scriptPubKey.size() < 1) {
                    sError = strprintf("Unknown address type and no script set.");
                    return error("%s: %s", __func__, sError);
                }
            }

            r.sEphem = sEphem;
        } else if (r.nType == OUTPUT_RINGCT) {
            CKey sEphem = r.sEphem;
            if (!sEphem.IsValid()) {
                sEphem.MakeNewKey(true);
            }

            if (r.address.type() == typeid(CStealthAddress)) {
                CStealthAddress sx = boost::get<CStealthAddress>(r.address);
                r.isMine = mapStealthAddresses.count(sx.GetID());
                CKey sShared;
                ec_point pkSendTo;
                int k, nTries = 24;
                for (k = 0; k < nTries; ++k) {
                    if (StealthSecret(sEphem, sx.scan_pubkey, sx.spend_pubkey, sShared, pkSendTo) == 0) {
                        break;
                    }
                    sEphem.MakeNewKey(true);
                }
                if (k >= nTries) {
                    sError = strprintf("Could not generate receiving public key.");
                    return error("%s: %s", __func__, sError);
                }

                r.pkTo = CPubKey(pkSendTo);
                CKeyID idTo = r.pkTo.GetID();

                if (sx.prefix.number_bits > 0) {
                    r.nStealthPrefix = FillStealthPrefix(sx.prefix.number_bits, sx.prefix.bitfield);
                }

                if (r.isMine) {
                    if (!RecordOwnedStealthDestination(sShared, sx.GetID(), idTo)) {
                        sError = strprintf("Failed to record stealth destination.");
                        return error("%s: %s", __func__, sError);
                    }
                }
            } else {
                sError = strprintf("Only able to send to stealth address for now."); // TODO: add more types?
                return error("%s: %s", __func__, sError);
            }

            r.sEphem = sEphem;
        }
    }

    return true;
}

bool AnonWallet::CalculateStealthDestinationKey(const CKeyID& idStealthSpend, const CKeyID& idStealthDestination, const CKey& sShared, CKey& keyDestination) const
{
    CKey keySpend;
    if (!RegenerateKey(idStealthSpend, keySpend))
        return error("%s: failed to regenerate spend key that is marked as mine", __func__);

    if (StealthSharedToSecretSpend(sShared, keySpend, keyDestination) != 0)
        return error("%s: StealthSharedToSecretSpend() failed.\n", __func__);

    if (keyDestination.GetPubKey().GetID() != idStealthDestination)
        return error("%s: failed to generate correct shared secret", __func__);

    return true;
}

bool AnonWallet::RecordOwnedStealthDestination(const CKey& sShared, const CKeyID& idStealth, const CKeyID& destStealth)
{
    if (!mapStealthAddresses.count(idStealth))
        return error("%s: do not have record of stealth address %s", __func__, idStealth.GetHex());

    CStealthAddress myStealth = mapStealthAddresses.at(idStealth);

    CKey keySharedSecret;
    if (!CalculateStealthDestinationKey(myStealth.GetSpendKeyID(), destStealth, sShared, keySharedSecret))
        return error("%s: failed to calculated stealth destination key", __func__);

    // Save this stealth destination to our stealth address so we know it is ours.
    if (!AddStealthDestination(idStealth, destStealth))
        return error("%s: failed to save stealth destination", __func__);
    if (!AddKeyToParent(keySharedSecret))
        return error("%s: failed to save ephemeral key", __func__);

    return true;
}

void SetCTOutVData(std::vector<uint8_t> &vData, const CPubKey &pkEphem, uint32_t nStealthPrefix)
{
    vData.resize(nStealthPrefix > 0 ? 38 : 33);

    memcpy(&vData[0], pkEphem.begin(), 33);
    if (nStealthPrefix > 0) {
        vData[33] = DO_STEALTH_PREFIX;
        memcpy(&vData[34], &nStealthPrefix, 4);
    }

    return;
}

void CreateOutputRingCT(OUTPUT_PTR<CTxOutBase> &txbout, const CCmpPubKey& cmpPubKeyTo, const uint32_t& nStealthPrefix, const CPubKey& pkEphem)
{
    txbout = MAKE_OUTPUT<CTxOutRingCT>();
    CTxOutRingCT *txout = (CTxOutRingCT*)txbout.get();
    txout->pk = cmpPubKeyTo;
    SetCTOutVData(txout->vData, pkEphem, nStealthPrefix);
}

int CreateOutput(OUTPUT_PTR<CTxOutBase> &txbout, CTempRecipient &r, std::string &sError)
{
    switch (r.nType) {
        case OUTPUT_DATA:
            txbout = MAKE_OUTPUT<CTxOutData>(r.vData);
            break;
        case OUTPUT_STANDARD:
            txbout = MAKE_OUTPUT<CTxOutStandard>(r.nAmount, r.scriptPubKey);
            break;
        case OUTPUT_CT:
            {
            txbout = MAKE_OUTPUT<CTxOutCT>();
            CTxOutCT *txout = (CTxOutCT*)txbout.get();

            if (r.fNonceSet) {
                if (r.vData.size() < 33) {
                    return errorN(1, sError, __func__, "Missing ephemeral value, vData size %d", r.vData.size());
                }
                txout->vData = r.vData;
            } else {
                CPubKey pkEphem = r.sEphem.GetPubKey();
                SetCTOutVData(txout->vData, pkEphem, r.nStealthPrefix);
            }

            txout->scriptPubKey = r.scriptPubKey;
            }
            break;
        case OUTPUT_RINGCT:
            {
                CreateOutputRingCT(txbout, r.pkTo, r.nStealthPrefix, r.sEphem.GetPubKey());
            }
            break;
        default:
            return errorN(1, sError, __func__, "Unknown output type %d", r.nType);
    }

    return 0;
}

bool AnonWallet::AddCTData(CTxOutBase *txout, CTempRecipient &r, std::string &sError)
{
    secp256k1_pedersen_commitment *pCommitment = txout->GetPCommitment();
    std::vector<uint8_t> *pvRangeproof = txout->GetPRangeproof();

    if (!pCommitment || !pvRangeproof) {
        sError = strprintf("Unable to get CT pointers for output type %d.", txout->GetType());
        return error("%s: %s", __func__, sError);
    }

    uint64_t nValue = r.nAmount;
    if (!secp256k1_pedersen_commit(secp256k1_ctx_blind, pCommitment, (uint8_t*)&r.vBlind[0], nValue, secp256k1_generator_h)) {
        sError = strprintf("Pedersen commit failed.");
        return error("%s: %s", __func__, sError);
    }

    uint256 nonce;
    if (r.fNonceSet) {
        nonce = r.nonce;
    } else {
        if (!r.sEphem.IsValid()) {
            sError = strprintf("Invalid ephemeral key.");
            return error("%s: %s", __func__, sError);
        }
        if (!r.pkTo.IsValid()) {
            sError = strprintf("Invalid recipient public key.");
            return error("%s: %s", __func__, sError);
        }
        nonce = r.sEphem.ECDH(r.pkTo);
        CSHA256().Write(nonce.begin(), 32).Finalize(nonce.begin());
        r.nonce = nonce;
    }

    const char *message = r.sNarration.c_str();
    size_t mlen = strlen(message);

    size_t nRangeProofLen = 5134;
    pvRangeproof->resize(nRangeProofLen);

    uint64_t min_value = 0;
    int ct_exponent = 2;
    int ct_bits = 32;

    if (0 != SelectRangeProofParameters(nValue, min_value, ct_exponent, ct_bits)) {
        sError = strprintf("Failed to select range proof parameters.");
        return error("%s: %s", __func__, sError);
    }

    if (r.fOverwriteRangeProofParams == true) {
        min_value = r.min_value;
        ct_exponent = r.ct_exponent;
        ct_bits = r.ct_bits;
    }

    if (1 != secp256k1_rangeproof_sign(secp256k1_ctx_blind,
                                       &(*pvRangeproof)[0], &nRangeProofLen,
                                       min_value, pCommitment,
                                       &r.vBlind[0], nonce.begin(),
                                       ct_exponent, ct_bits, nValue,
                                       (const unsigned char*) message, mlen,
                                       nullptr, 0, secp256k1_generator_h)) {
        sError = strprintf("Failed to sign range proof.");
        return error("%s: %s", __func__, sError);
    }

    pvRangeproof->resize(nRangeProofLen);

    return true;
}

static bool HaveAnonOutputs(std::vector<CTempRecipient> &vecSend)
{
    for (const auto &r : vecSend)
    if (r.nType == OUTPUT_RINGCT) {
        return true;
    }
    return false;
}

bool CheckOutputValue(const CTempRecipient &r, const CTxOutBase *txbout, CAmount nFeeRet, std::string sError)
{
    if (r.fZerocoinMint)
        return true;
    if ((r.nType == OUTPUT_STANDARD && IsDust(txbout, dustRelayFee)) || (r.nType != OUTPUT_DATA && r.nAmount < 0)) {
        if (r.fSubtractFeeFromAmount && nFeeRet > 0) {
            if (r.nAmount < 0) {
                sError = _("The transaction amount is too small to pay the fee");
            } else {
                sError = _("The transaction amount is too small to send after the fee has been deducted");
            }
        } else {
            sError = _("Transaction amount too small");
            LogPrintf("%s:%s amount=%s\n", __func__, __LINE__, FormatMoney(r.nAmount));
        }
        return error("%s: Failed %s.", __func__, sError);
    }

    return true;
}

void InspectOutputs(std::vector<CTempRecipient> &vecSend, bool fZerocoinInputs, CAmount &nValue,
        size_t &nSubtractFeeFromAmount, bool &fOnlyStandardOutputs, bool& fSkipFee, bool& fSendingOnlyBaseCoin)
{
    nValue = 0;
    nSubtractFeeFromAmount = 0;
    fOnlyStandardOutputs = true;
    fSkipFee = true;
    fSendingOnlyBaseCoin = true;

    for (auto &r : vecSend) {
        nValue += r.nAmount;
        fSkipFee = fSkipFee && (fZerocoinInputs && !r.fZerocoin);
        if (r.nType != OUTPUT_STANDARD && r.nType != OUTPUT_DATA) {
            fOnlyStandardOutputs = false;
            fSendingOnlyBaseCoin = false;
        }

        if (r.nType == OUTPUT_STANDARD && r.fZerocoin)
            fSendingOnlyBaseCoin = false;

        if (r.fSubtractFeeFromAmount) {
            if (r.fSplitBlindOutput && r.nAmount < 0.1) {
                r.fExemptFeeSub = true;
            } else {
                nSubtractFeeFromAmount++;
            }
        }
    }
}

static bool ExpandChangeAddress(AnonWallet *phdw, CTempRecipient &r, std::string &sError)
{
    if (r.address.type() == typeid(CStealthAddress)) {
        CStealthAddress sx = boost::get<CStealthAddress>(r.address);
        r.isMine = true;
        CKey keyShared;
        ec_point pkSendTo;

        int k, nTries = 24;
        for (k = 0; k < nTries; ++k) {
            if (StealthSecret(r.sEphem, sx.scan_pubkey, sx.spend_pubkey, keyShared, pkSendTo) == 0)
                break;
            r.sEphem.MakeNewKey(true);
        }
        if (k >= nTries)
            return error("%s: Could not generate receiving public key.", __func__);

        CPubKey pkEphem = r.sEphem.GetPubKey();
        if (!pkEphem.IsValid())
            return error("%s: Ephemeral pubkey is not valid\n", __func__);
        r.pkTo = CPubKey(pkSendTo);
        CKeyID idTo = r.pkTo.GetID();
        r.scriptPubKey = GetScriptForDestination(idTo);

        if (r.nType == OUTPUT_CT || r.nType == OUTPUT_RINGCT) {
            if (sx.prefix.number_bits > 0) {
                r.nStealthPrefix = FillStealthPrefix(sx.prefix.number_bits, sx.prefix.bitfield);
            }
            if (!phdw->RecordOwnedStealthDestination(keyShared, sx.GetID(), idTo))
                return error("%s: failed to record stealth destination", __func__);
        }
        return true;
    };

    if (r.address.type() == typeid(CExtKeyPair)) {
        return error("%s: trying to send to extkeypair", __func__);
    }

    if (r.address.type() == typeid(CKeyID)) {
        CKeyID idk = boost::get<CKeyID>(r.address);

        if (!phdw->GetPubKey(idk, r.pkTo))
            return error("%s: GetPubKey failed.", __func__);
        r.scriptPubKey = GetScriptForDestination(idk);

        return true;
    }

    if (r.address.type() != typeid(CNoDestination)
        // TODO OUTPUT_CT?
        && r.nType == OUTPUT_STANDARD) {
        r.scriptPubKey = GetScriptForDestination(r.address);
        return r.scriptPubKey.size() > 0;
    }

    return false;
}

bool AnonWallet::SetChangeDest(const CCoinControl *coinControl, CTempRecipient &r, std::string &sError)
{
    if (r.nType == OUTPUT_CT || r.nType == OUTPUT_RINGCT || r.address.type() == typeid(CStealthAddress))
        r.sEphem.MakeNewKey(true);

    //Already have recipient, use this
    if (r.address.type() == typeid(CStealthAddress)) {
        return ExpandChangeAddress(this, r, sError);
    }

    // coin control: send change to custom address
    if (coinControl && !boost::get<CNoDestination>(&coinControl->destChange)) {
        r.address = coinControl->destChange;
        return ExpandChangeAddress(this, r, sError);
    } else if (coinControl && coinControl->scriptChange.size() > 0) {
        if (r.nType == OUTPUT_RINGCT) {
            sError = strprintf("Change script is ringCT output.");
            return error("%s: %s", __func__, sError);
        }

        r.scriptPubKey = coinControl->scriptChange;

        if (r.nType == OUTPUT_CT) {
            if (!ExtractDestination(r.scriptPubKey, r.address)) {
                sError = strprintf("Could not get public key from change script.");
                return error("%s: %s", __func__, sError);
            }

            if (r.address.type() != typeid(CKeyID)) {
                sError = strprintf("Could not get public key from change script.");
                return error("%s: %s", __func__, sError);
            }

            CKeyID idk = boost::get<CKeyID>(r.address);
            if (!GetPubKey(idk, r.pkTo)) {
                sError = strprintf("Could not get public key from change script.");
                return error("%s: %s", __func__, sError);
            }
        }

        return true;
    }

    sError = strprintf("General failure.");
    return error("%s: %s", __func__, sError);
}

static bool InsertChangeAddress(CTempRecipient &r, std::vector<CTempRecipient> &vecSend, int &nChangePosInOut)
{
    if (nChangePosInOut < 0) {
        nChangePosInOut = GetRandInt(vecSend.size()+1);
    } else {
        nChangePosInOut = std::min(nChangePosInOut, (int)vecSend.size());
    }

    if (nChangePosInOut < (int)vecSend.size() && vecSend[nChangePosInOut].nType == OUTPUT_DATA) {
        nChangePosInOut++;
    }

    vecSend.insert(vecSend.begin()+nChangePosInOut, r);

    // Insert data output for stealth address if required
    if (r.vData.size() > 0) {
        CTempRecipient rd;
        rd.nType = OUTPUT_DATA;
        rd.fChange = true;
        rd.vData = r.vData;
        r.vData.clear();
        //vecSend.insert(vecSend.begin()+nChangePosInOut+1, rd);
    }

    return true;
}

int PreAcceptMempoolTx(CWalletTx &wtx, std::string &sError)
{
    // Check if wtx can get into the mempool

    // Limit size
    if (GetTransactionWeight(*wtx.tx) >= MAX_STANDARD_TX_WEIGHT) {
        sError = strprintf("Transaction too large.");
        return error("%s: %s", __func__, sError);
    }

    if (gArgs.GetBoolArg("-walletrejectlongchains", DEFAULT_WALLET_REJECT_LONG_CHAINS)) {
        // Lastly, ensure this tx will pass the mempool's chain limits
        LockPoints lp;
        CTxMemPoolEntry entry(wtx.tx, 0, 0, 0, false, 0, lp);
        CTxMemPool::setEntries setAncestors;
        size_t nLimitAncestors = gArgs.GetArg("-limitancestorcount", DEFAULT_ANCESTOR_LIMIT);
        size_t nLimitAncestorSize = gArgs.GetArg("-limitancestorsize", DEFAULT_ANCESTOR_SIZE_LIMIT)*1000;
        size_t nLimitDescendants = gArgs.GetArg("-limitdescendantcount", DEFAULT_DESCENDANT_LIMIT);
        size_t nLimitDescendantSize = gArgs.GetArg("-limitdescendantsize", DEFAULT_DESCENDANT_SIZE_LIMIT)*1000;
        std::string errString;
        LOCK(::mempool.cs);
        if (!mempool.CalculateMemPoolAncestors(entry, setAncestors, nLimitAncestors, nLimitAncestorSize,
                                               nLimitDescendants, nLimitDescendantSize, errString)) {
            sError = strprintf("Transaction has too long of a mempool chain.");
            return error("%s: %s", __func__, sError);
        }
    }

    return true;
}

bool AnonWallet::SaveRecord(const uint256& txid, const CTransactionRecord& rtx)
{
    AnonWalletDB wdb(*walletDatabase);
    if (!wdb.WriteTxRecord(txid, rtx))
        return error("%s: failed to write tx record\n", __func__);
    mapRecords[txid] = rtx;
    return true;
}

bool GetScriptPubKeyFromOutpoint(const COutPoint& outpoint, CScript& scriptPubKey)
{
    //output record does not have the signing key, find it manually
    CTransactionRef ptx;
    uint256 hashBlock;
    if (!GetTransaction(outpoint.hash, ptx, Params().GetConsensus(), hashBlock, /*allowslow*/true))
        return error("Failed to find input transaction %s", outpoint.hash.GetHex());
    if (ptx->vpout.size() <= outpoint.n)
        return error("Failed to find input output %s:%d does not exist", outpoint.hash.GetHex(), outpoint.n);

    return ptx->vpout[outpoint.n]->GetScriptPubKey(scriptPubKey);
}

int AnonWallet::AddStandardInputs_Inner(CWalletTx &wtx, CTransactionRecord &rtx, std::vector<CTempRecipient> &vecSend,
        bool sign, CAmount &nFeeRet, const CCoinControl *coinControl, std::string &sError, bool fZerocoinInputs, CAmount nInputValue)
{
    assert(coinControl);
    nFeeRet = 0;
    CAmount nValueOutCT;
    size_t nSubtractFeeFromAmount;
    bool fOnlyStandardOutputs, fSkipFee, fSendingOnlyBaseCoin;
    InspectOutputs(vecSend, fZerocoinInputs, nValueOutCT, nSubtractFeeFromAmount, fOnlyStandardOutputs, fSkipFee, fSendingOnlyBaseCoin);

    //Need count of zerocoin mint outputs in order to calculate fee correctly
    int nZerocoinMintOuts = 0;
    CAmount nValueOutZerocoin = 0;
    CAmount nFeeZerocoin = 0;
    for (auto& r : vecSend) {
        if (r.fZerocoinMint) {
            nZerocoinMintOuts++;
            nValueOutZerocoin += r.nAmount;
            nFeeZerocoin += Params().Zerocoin_MintFee();
            //Zerocoin mints have to be exact amount, so not eligible to subtract fee
            r.fExemptFeeSub = true;
        }
    }
    fSkipFee = fZerocoinInputs && nZerocoinMintOuts == 0;
    //If output is going out as zerocoin, then it is being double counted and needs to be subtracted
    nValueOutCT -= nValueOutZerocoin;

    if (!ExpandTempRecipients(vecSend, sError)) {
        return 1; // sError is set
    }

    wtx.fTimeReceivedIsTxTime = true;
    wtx.BindWallet(pwalletParent.get());
    wtx.fFromMe = true;
    CMutableTransaction txNew;
    if (fZerocoinInputs) {
        txNew = CMutableTransaction(*wtx.tx);
    }

    // Discourage fee sniping. See CWallet::CreateTransaction
    txNew.nLockTime = chainActive.Height() - 1;

    // 1/10 chance of random time further back to increase privacy
    if (GetRandInt(10) == 0) {
        txNew.nLockTime = std::max(0, (int)txNew.nLockTime - GetRandInt(100));
    }

    assert(txNew.nLockTime <= (unsigned int)chainActive.Height());
    assert(txNew.nLockTime < LOCKTIME_THRESHOLD);

    coinControl->fHaveAnonOutputs = HaveAnonOutputs(vecSend);
    FeeCalculation feeCalc;
    CAmount nFeeNeeded;
    unsigned int nBytes;
    int nChangePosInOut = -1;
    {
        LOCK2(cs_main, pwalletParent->cs_wallet);

        std::set<CInputCoin> setCoins;
        vector<COutput> vAvailableCoins;
        if (!fZerocoinInputs)
            pwalletParent->AvailableCoins(vAvailableCoins, true, coinControl);
        CoinSelectionParams coin_selection_params; // Parameters for coin selection, init with dummy

        CFeeRate discard_rate = GetDiscardRate(*pwalletParent, ::feeEstimator);

        // Get the fee rate to use effective values in coin selection
        CFeeRate nFeeRateNeeded = GetMinimumFeeRate(*pwalletParent, *coinControl, ::mempool, ::feeEstimator, &feeCalc);

        nFeeRet = 0;
        size_t nSubFeeTries = 100;
        bool pick_new_inputs = true;
        CAmount nValueIn = nInputValue;

        // BnB selector is the only selector used when this is true.
        // That should only happen on the first pass through the loop.
        coin_selection_params.use_bnb = nSubtractFeeFromAmount == 0; // If we are doing subtract fee from recipient, then don't use BnB

        // Start with no fee and loop until there is enough fee
        while (true) {
            if (!fZerocoinInputs) {
                txNew.vin.clear();
            }

            txNew.vpout.clear();
            wtx.fFromMe = true;

            CAmount nValueToSelect = nValueOutCT;
            if (!fZerocoinInputs)
                nValueToSelect += nValueOutZerocoin;
            if (nSubtractFeeFromAmount == 0) {
                nValueToSelect += nFeeRet;
            }

            // Choose coins to use
            bool bnb_used;
            if (!fZerocoinInputs && pick_new_inputs) {
                coin_selection_params.change_spend_size = 40; // TODO
                coin_selection_params.effective_fee = nFeeRateNeeded;
                nValueIn = 0;
                setCoins.clear();
                if (!pwalletParent->SelectCoins(vAvailableCoins, nValueToSelect, setCoins, nValueIn, *coinControl, coin_selection_params, bnb_used)) {
                    // If BnB was used, it was the first pass. No longer the first pass and continue loop with knapsack.
                    if (bnb_used) {
                        coin_selection_params.use_bnb = false;
                        continue;
                    }

                    return wserrorN(1, sError, __func__, _("Insufficient funds.").c_str());
                }
            }

            // Subtract the already minted zerocoin change from the total change if there were zerocoin inputs
            const CAmount nChange = nValueIn - nValueToSelect - (fZerocoinInputs ? nValueOutZerocoin : 0);

            // Remove fee outputs from last round
            for (size_t i = 0; i < vecSend.size(); ++i) {
                if (vecSend[i].fChange) {
                    vecSend.erase(vecSend.begin() + i);
                    i--;
                }
            }

            nChangePosInOut = -1;
            if (nChange > 0) {
                // Fill an output to ourself
                CTempRecipient r;
                r.nType = OUTPUT_CT;
                r.fChange = true;
                r.SetAmount(nChange);
                r.isMine = true;

                //If no change address is set, then use the default change address
                if (!coinControl || (coinControl && coinControl->destChange.type() == typeid(CNoDestination))) {
                    r.address = GetStealthChangeAddress();
                }

                if (!SetChangeDest(coinControl, r, sError)) {
                    return wserrorN(1, sError, __func__, ("SetChangeDest failed: " + sError));
                }

                CTxOutStandard tempOut;
                tempOut.scriptPubKey = r.scriptPubKey;
                tempOut.SetValue(nChange);

                // Never create dust outputs; if we would, just
                // add the dust to the fee.
                if (IsDust(&tempOut, discard_rate)) {
                    nChangePosInOut = -1;
                    nFeeRet += nChange;
                } else {
                    nChangePosInOut = coinControl->nChangePos;
                    InsertChangeAddress(r, vecSend, nChangePosInOut);
                }
            }

            // Fill vin
            //
            // Note how the sequence number is set to non-maxint so that
            // the nLockTime set above actually works.
            //
            // BIP125 defines opt-in RBF as any nSequence < maxint-1, so
            // we use the highest possible value in that range (maxint-2)
            // to avoid conflicting with other possible uses of nSequence,
            // and in the spirit of "smallest possible change from prior
            // behavior."
            const uint32_t nSequence = coinControl->m_signal_bip125_rbf.get_value_or(pwalletParent->m_signal_rbf) ? MAX_BIP125_RBF_SEQUENCE : (CTxIn::SEQUENCE_FINAL - 1);
            if (!fZerocoinInputs) {
                for (const auto &coin : setCoins) {
                    txNew.vin.push_back(CTxIn(coin.outpoint, CScript(), nSequence));
                }
            }

            CAmount nValueOutPlain = 0;

            int nLastBlindedOutput = -1;

            if (!fOnlyStandardOutputs || nZerocoinMintOuts || fSendingOnlyBaseCoin) {
                OUTPUT_PTR<CTxOutData> outFee = MAKE_OUTPUT<CTxOutData>();
                outFee->vData.push_back(DO_FEE);
                outFee->vData.resize(9); // More bytes than varint fee could use
                txNew.vpout.push_back(outFee);
            }

            bool fFirst = true;
            for (size_t i = 0; i < vecSend.size(); ++i) {
                auto &r = vecSend[i];
                if (this->HaveAddress(r.address))
                    r.isMine = true;

                r.ApplySubFee(nFeeRet, nSubtractFeeFromAmount, fFirst);

                OUTPUT_PTR<CTxOutBase> txbout;
                if (0 != CreateOutput(txbout, r, sError)) {
                    return 1; // sError will be set
                }

                if (!CheckOutputValue(r, &*txbout, nFeeRet, sError)) {
                    return 1; // sError set
                }

                if (r.nType == OUTPUT_STANDARD) {
                    nValueOutPlain += r.nAmount;
                    if (r.fChange) {
                        nChangePosInOut = i;
                    }
                }

                if (r.nType == OUTPUT_CT) {
                    nLastBlindedOutput = i;
                }

                r.n = txNew.vpout.size();
                txNew.vpout.push_back(txbout);
            }

            std::vector<uint8_t*> vpBlinds;
            std::vector<uint8_t> vBlindPlain;

            size_t nBlindedInputs = 1;
            secp256k1_pedersen_commitment plainCommitment;
            secp256k1_pedersen_commitment plainInputCommitment;

            vBlindPlain.resize(32);
            memset(&vBlindPlain[0], 0, 32);
            vpBlinds.push_back(&vBlindPlain[0]);
            if (nValueIn > 0
                && !secp256k1_pedersen_commit(secp256k1_ctx_blind, &plainInputCommitment, &vBlindPlain[0], (uint64_t) nValueIn, secp256k1_generator_h)) {
                return wserrorN(1, sError, __func__, "secp256k1_pedersen_commit failed for plain in.");
            }
            nValueOutPlain += nValueOutZerocoin + nFeeRet;
            if (nValueOutPlain > 0) {
                vpBlinds.push_back(&vBlindPlain[0]);

                if (!secp256k1_pedersen_commit(secp256k1_ctx_blind, &plainCommitment, &vBlindPlain[0], (uint64_t) nValueOutPlain, secp256k1_generator_h)) {
                    return wserrorN(1, sError, __func__, "secp256k1_pedersen_commit failed for plain out.");
                }
            }

            for (size_t i = 0; i < vecSend.size(); ++i) {
                auto &r = vecSend[i];

                if (r.nType == OUTPUT_CT || r.nType == OUTPUT_RINGCT) {
                    if ((int)i == nLastBlindedOutput) {
                        r.vBlind.resize(32);
                        // Last to-be-blinded value: compute from all other blinding factors.
                        // sum of output blinding values must equal sum of input blinding values
                        if (!secp256k1_pedersen_blind_sum(secp256k1_ctx_blind, &r.vBlind[0], &vpBlinds[0], vpBlinds.size(), nBlindedInputs)) {
                            return wserrorN(1, sError, __func__, "secp256k1_pedersen_blind_sum failed.");
                        }
                    } else {
                        if (r.vBlind.size() != 32) {
                            r.vBlind.resize(32);
                            GetStrongRandBytes(&r.vBlind[0], 32);
                        }
                        vpBlinds.push_back(&r.vBlind[0]);
                    }

                    assert(r.n < (int)txNew.vpout.size());
                    if (!AddCTData(txNew.vpout[r.n].get(), r, sError)) {
                        return 1; // sError will be set
                    }
                }
            }

            // Fill in dummy signatures for fee calculation.
            int nIn = 0;
            if (!fZerocoinInputs) {
                for (const auto &coin : setCoins) {
                    const CScript &scriptPubKey = coin.txout.scriptPubKey;
                    SignatureData sigdata;

                    std::map<COutPoint, CInputData>::const_iterator it = coinControl->m_inputData.find(coin.outpoint);
                    if (it != coinControl->m_inputData.end()) {
                        sigdata.scriptWitness = it->second.scriptWitness;
                    } else if (!ProduceSignature(*pwalletParent, DUMMY_SIGNATURE_CREATOR, scriptPubKey, sigdata)) {
                        return wserrorN(1, sError, __func__, "Dummy signature failed.");
                    }
                    UpdateInput(txNew.vin[nIn], sigdata);
                    nIn++;
                }
            }

            if (fSkipFee) {
                nFeeNeeded = 0;
                nFeeRet = 0;
                std::vector<uint8_t> &vData = ((CTxOutData*)txNew.vpout[0].get())->vData;
                vData.resize(1);
                if (0 != PutVarInt(vData, 0))
                    return error("%s: failed to add CTFee of 0 to transaction", __func__);
                break;
            }

            nBytes = GetVirtualTransactionSize(txNew);

            // Remove scriptSigs to eliminate the fee calculation dummy signatures
            if (!fZerocoinInputs) {
                for (auto &vin : txNew.vin) {
                    vin.scriptSig = CScript();
                    vin.scriptWitness.SetNull();
                }
            }

            nFeeNeeded = GetMinimumFee(*pwalletParent, nBytes, *coinControl, ::mempool, ::feeEstimator, &feeCalc);
            if (nFeeZerocoin)
                nFeeNeeded = nFeeZerocoin;

            // If we made it here and we aren't even able to meet the relay fee on the next pass, give up
            // because we must be at the maximum allowed fee.
            if (!fSkipFee && nFeeNeeded < ::minRelayTxFee.GetFee(nBytes)) {
                return wserrorN(1, sError, __func__, _("Transaction too large for fee policy."));
            }

            if (!fSkipFee) {
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
                        CKeyID idNull;
                        CScript scriptChange = GetScriptForDestination(idNull);
                        CTxOut change_prototype_txout(0, scriptChange);
                        size_t change_prototype_size = GetSerializeSize(change_prototype_txout, SER_DISK, 0);
                        unsigned int tx_size_with_change = nBytes + change_prototype_size +
                                                           2; // Add 2 as a buffer in case increasing # of outputs changes compact size
                        CAmount fee_needed_with_change = GetMinimumFee(*pwalletParent, tx_size_with_change,
                                                                       *coinControl, ::mempool, ::feeEstimator,
                                                                       nullptr);
                        CAmount minimum_value_for_change = GetDustThreshold(change_prototype_txout, discard_rate);
                        if (nFeeRet >= fee_needed_with_change + minimum_value_for_change) {
                            pick_new_inputs = false;
                            nFeeRet = fee_needed_with_change;
                            continue;
                        }
                    }

                    // If we have change output already, just increase it
                    if (nFeeRet > nFeeNeeded && nChangePosInOut != -1 && nSubtractFeeFromAmount == 0) {
                        auto& r = vecSend[nChangePosInOut];
                        CAmount extraFeePaid = nFeeRet - nFeeNeeded;
                        CTxOutBaseRef c = txNew.vpout[r.n];
                        c->SetValue(c->GetValue() + extraFeePaid);
                        r.nAmount = c->GetValue();

                        nFeeRet -= extraFeePaid;
                    }

                    break; // Done, enough fee included.
                } else if (!pick_new_inputs) {
                    // This shouldn't happen, we should have had enough excess
                    // fee to pay for the new output and still meet nFeeNeeded
                    // Or we should have just subtracted fee from recipients and
                    // nFeeNeeded should not have changed

                    if (!nSubtractFeeFromAmount || !(--nSubFeeTries)) { // rangeproofs can change size per iteration
                        return wserrorN(1, sError, __func__,
                                        _("Transaction fee and change calculation failed.").c_str());
                    }
                }

                // Try to reduce change to include necessary fee
                if (nChangePosInOut != -1 && nSubtractFeeFromAmount == 0) {
                    auto& r = vecSend[nChangePosInOut];
                    CAmount additionalFeeNeeded = nFeeNeeded - nFeeRet;

                    CTxOutBaseRef c = txNew.vpout[r.n];
                    // Only reduce change if remaining amount is still a large enough output.
                    if (c->GetValue() >= MIN_FINAL_CHANGE + additionalFeeNeeded) {
                        c->SetValue(c->GetValue() - additionalFeeNeeded);
                        r.nAmount = c->GetValue();
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
            }
        }

        coinControl->nChangePos = nChangePosInOut;

        if (!fOnlyStandardOutputs || nZerocoinMintOuts || fSendingOnlyBaseCoin) {
            std::vector<uint8_t> &vData = ((CTxOutData*)txNew.vpout[0].get())->vData;
            vData.resize(1);
            if (0 != PutVarInt(vData, nFeeRet)) {
                return werrorN(1, "%s: PutVarInt %d failed\n", __func__, nFeeRet);
            }
        }

        if (!fZerocoinInputs && sign) {
            int nIn = 0;
            for (const auto &coin : setCoins) {
                const CScript& scriptPubKey = coin.txout.scriptPubKey;

                std::vector<uint8_t> vchAmount(8);
                memcpy(vchAmount.data(), &coin.txout.nValue, 8);

                SignatureData sigdata;
                if (!ProduceSignature(*pwalletParent, MutableTransactionSignatureCreator(&txNew, nIn, vchAmount, SIGHASH_ALL), scriptPubKey, sigdata)) {
                    return wserrorN(1, sError, __func__, _("Signing transaction failed"));
                }

                UpdateInput(txNew.vin[nIn], sigdata);
                nIn++;
            }
        }

        rtx.nFee = nFeeRet;
        rtx.nFlags |= ORF_FROM; //Set from me
        rtx.nFlags |= ORF_BASECOIN_IN;
        AddOutputRecordMetaData(rtx, vecSend);
        MarkInputsAsPendingSpend(rtx);

        uint256 txid = txNew.GetHash();
        if (fZerocoinInputs)
            rtx.AddPartialTxid(txid);
        //save record
        SaveRecord(txid, rtx);

        // Embed the constructed transaction data in wtxNew.
        wtx.SetTx(MakeTransactionRef(std::move(txNew)));
    } // cs_main, pwalletParent->cs_wallet

    if (!PreAcceptMempoolTx(wtx, sError)) {
        return 1;
    }

    LogPrintf("Fee Calculation: Fee:%d Bytes:%u Needed:%d Tgt:%d (requested %d) Reason:\"%s\" Decay %.5f: Estimation: (%g - %g) %.2f%% %.1f/(%.1f %d mem %.1f out) Fail: (%g - %g) %.2f%% %.1f/(%.1f %d mem %.1f out)\n",
              nFeeRet, nBytes, nFeeNeeded, feeCalc.returnedTarget, feeCalc.desiredTarget, StringForFeeReason(feeCalc.reason), feeCalc.est.decay,
              feeCalc.est.pass.start, feeCalc.est.pass.end,
              100 * feeCalc.est.pass.withinTarget / (feeCalc.est.pass.totalConfirmed + feeCalc.est.pass.inMempool + feeCalc.est.pass.leftMempool),
              feeCalc.est.pass.withinTarget, feeCalc.est.pass.totalConfirmed, feeCalc.est.pass.inMempool, feeCalc.est.pass.leftMempool,
              feeCalc.est.fail.start, feeCalc.est.fail.end,
              100 * feeCalc.est.fail.withinTarget / (feeCalc.est.fail.totalConfirmed + feeCalc.est.fail.inMempool + feeCalc.est.fail.leftMempool),
              feeCalc.est.fail.withinTarget, feeCalc.est.fail.totalConfirmed, feeCalc.est.fail.inMempool, feeCalc.est.fail.leftMempool);

    return 0;
}

int AnonWallet::AddStandardInputs(CWalletTx &wtx, CTransactionRecord &rtx, std::vector<CTempRecipient> &vecSend,
        bool sign, CAmount &nFeeRet, const CCoinControl *coinControl, std::string &sError, bool fZerocoinInputs, CAmount nInputValue)
{
    if (vecSend.size() < 1) {
        return wserrorN(1, sError, __func__, _("Transaction must have at least one recipient."));
    }

    CAmount nValue = 0;
    bool fCT = false;
    for (const auto &r : vecSend) {
        nValue += r.nAmount;
        if (nValue < 0 || r.nAmount < 0) {
            return wserrorN(1, sError, __func__, _("Transaction amounts must not be negative."));
        }

        if (r.nType == OUTPUT_CT || r.nType == OUTPUT_RINGCT) {
            fCT = true;
        }
    }

    // No point hiding the amount in one output
    // If one of the outputs was always 0 it would be easy to track amounts,
    // the output that gets spent would be = plain input.
    if (fCT && vecSend.size() < 2) {
        CTempRecipient &r0 = vecSend[0];
        CTempRecipient rN;
        rN.nType = r0.nType;
        rN.nAmount = r0.nAmount * ((float)GetRandInt(100) / 100.0);
        rN.nAmountSelected = rN.nAmount;
        rN.address = r0.address;
        rN.fSubtractFeeFromAmount = r0.fSubtractFeeFromAmount;

        r0.nAmount -= rN.nAmount;
        r0.nAmountSelected = r0.nAmount;

        // Tag the smaller amount, might be too small to sub the fee from
        if (r0.nAmount < rN.nAmount) {
            r0.fSplitBlindOutput = true;
        } else {
            rN.fSplitBlindOutput = true;
        }

        vecSend.push_back(rN);
    }

    if (0 != AddStandardInputs_Inner(wtx, rtx, vecSend, sign, nFeeRet, coinControl, sError, fZerocoinInputs, nInputValue)) {
        return 1;
    }

    return 0;
}

bool AnonWallet::MakeSigningKeystore(CBasicKeyStore& keystore, const CScript& scriptPubKey)
{
    CTxDestination dest;
    if (!ExtractDestination(scriptPubKey, dest))
        return error("%s: Failed to extract destination", __func__);

    if (dest.type() != typeid(CKeyID))
        return error("%s: Destination is not type keyid", __func__);

    CKey key;
    CKeyID keyID = boost::get<CKeyID>(dest);
    if (!GetKey(keyID, key))
        return error("%s: Failed to fetch key", __func__);

    keystore.AddKey(key);

    return true;
}

int AnonWallet::AddBlindedInputs_Inner(CWalletTx &wtx, CTransactionRecord &rtx, std::vector<CTempRecipient> &vecSend,
    bool sign, CAmount &nFeeRet, const CCoinControl *coinControl, std::string &sError)
{
    assert(coinControl);
    nFeeRet = 0;
    CAmount nValueOutBlind;
    size_t nSubtractFeeFromAmount;
    bool fOnlyStandardOutputs, fSkipFee, fSendingOnlyBaseCoin;
    InspectOutputs(vecSend, false, nValueOutBlind, nSubtractFeeFromAmount, fOnlyStandardOutputs, fSkipFee, fSendingOnlyBaseCoin);

    //Need count of zerocoin mint outputs in order to calculate fee correctly
    int nZerocoinMintOuts = 0;
    CAmount nValueOutZerocoin = 0;
    CAmount nFeeZerocoin = 0;
    bool fHasCTOut = false;
    for (auto& r : vecSend) {
        if (r.nType == OUTPUT_CT) {
            fHasCTOut = true;
        }
        if (r.fZerocoinMint) {
            nZerocoinMintOuts++;
            nValueOutZerocoin += r.nAmount;
            nFeeZerocoin += Params().Zerocoin_MintFee();
            //Zerocoin mints have to be exact amount, so not eligible to subtract fee
            r.fExemptFeeSub = true;
        }
    }
    //If output is going out as zerocoin, then it is being double counted and needs to be subtracted
    nValueOutBlind -= nValueOutZerocoin;

    if (!ExpandTempRecipients(vecSend, sError))
        return 1; // sError is set

    wtx.fTimeReceivedIsTxTime = true;
    wtx.BindWallet(pwalletParent.get());
    wtx.fFromMe = true;
    CMutableTransaction txNew;

    // Discourage fee sniping. See CWallet::CreateTransaction
    txNew.nLockTime = chainActive.Height() - 1;

    // 1/10 chance of random time further back to increase privacy
    if (GetRandInt(10) == 0) {
        txNew.nLockTime = std::max(0, (int)txNew.nLockTime - GetRandInt(100));
    }

    assert(txNew.nLockTime <= (unsigned int)chainActive.Height());
    assert(txNew.nLockTime < LOCKTIME_THRESHOLD);

    coinControl->fHaveAnonOutputs = HaveAnonOutputs(vecSend);
    FeeCalculation feeCalc;
    CAmount nFeeNeeded;
    unsigned int nBytes;
    {
        LOCK2(cs_main, pwalletParent->cs_wallet);

        std::vector<std::pair<MapRecords_t::const_iterator, unsigned int> > setCoins;
        std::vector<COutputR> vAvailableCoins;
        AvailableBlindedCoins(vAvailableCoins, true, coinControl, /*nMinimumAmount*/2);

        CAmount nValueOutPlain = 0;
        int nChangePosInOut = -1;

        nFeeRet = 0;
        size_t nSubFeeTries = 100;
        bool pick_new_inputs = true;
        CAmount nValueIn = 0;
        // Start with no fee and loop until there is enough fee
        while (true) {
            txNew.vin.clear();
            txNew.vpout.clear();
            wtx.fFromMe = true;

            CAmount nValueToSelect = nValueOutBlind + nValueOutZerocoin;
            if (nSubtractFeeFromAmount == 0) {
                nValueToSelect += nFeeRet;
            }

            // Choose coins to use
            if (pick_new_inputs) {
                nValueIn = 0;
                setCoins.clear();
                if (!SelectBlindedCoins(vAvailableCoins, nValueToSelect, setCoins, nValueIn, coinControl)) {
                    return wserrorN(1, sError, __func__, _("Insufficient funds."));
                }
            }

            const CAmount nChange = nValueIn - nValueToSelect;

            // Remove fee outputs from last round
            for (size_t i = 0; i < vecSend.size(); ++i) {
                if (vecSend[i].fChange) {
                    vecSend.erase(vecSend.begin() + i);
                    i--;
                }
            }

            if (!coinControl->m_addChangeOutput) {
                nFeeRet += nChange;
            } else {
                // Insert a sender-owned 0 value output which becomes the change output if needed
                CTempRecipient r;

                //Default use ringct change, but have to use CT if the destination is sent as CT
                r.nType = OUTPUT_RINGCT;
                if (fHasCTOut)
                    r.nType = OUTPUT_CT;
                r.fChange = true;

                //If no change address is set, then use the default change address
                if (!coinControl || ((coinControl && coinControl->destChange.type() == typeid(CNoDestination)))) {
                    r.address = GetStealthChangeAddress();
                }

                if (!SetChangeDest(coinControl, r, sError)) {
                    return wserrorN(1, sError, __func__, ("SetChangeDest failed: " + sError));
                }

                if (fOnlyStandardOutputs // Need at least 1 blinded output
                    || nChange > ::minRelayTxFee.GetFee(2048)) { // TODO: better output size estimate
                    r.SetAmount(nChange);
                } else {
                    r.SetAmount(0);
                    nFeeRet += nChange;
                }

                nChangePosInOut = coinControl->nChangePos;
                InsertChangeAddress(r, vecSend, nChangePosInOut);
            }

            // Fill vin
            //
            // Note how the sequence number is set to non-maxint so that
            // the nLockTime set above actually works.
            //
            // BIP125 defines opt-in RBF as any nSequence < maxint-1, so
            // we use the highest possible value in that range (maxint-2)
            // to avoid conflicting with other possible uses of nSequence,
            // and in the spirit of "smallest possible change from prior
            // behavior."
            for (const auto &coin : setCoins) {
                const uint256 &txhash = coin.first->first;
                const uint32_t nSequence = coinControl->m_signal_bip125_rbf.get_value_or(pwalletParent->m_signal_rbf) ? MAX_BIP125_RBF_SEQUENCE : (CTxIn::SEQUENCE_FINAL - 1);
                txNew.vin.push_back(CTxIn(txhash, coin.second, CScript(), nSequence));
            }

            nValueOutPlain = 0;
            nChangePosInOut = -1;

            OUTPUT_PTR<CTxOutData> outFee = MAKE_OUTPUT<CTxOutData>();
            outFee->vData.push_back(DO_FEE);
            outFee->vData.resize(9); // More bytes than varint fee could use
            txNew.vpout.push_back(outFee);

            bool fFirst = true;
            for (size_t i = 0; i < vecSend.size(); ++i) {
                auto &r = vecSend[i];

                r.ApplySubFee(nFeeRet, nSubtractFeeFromAmount, fFirst);

                OUTPUT_PTR<CTxOutBase> txbout;
                if (0 != CreateOutput(txbout, r, sError)) {
                    return 1; // sError will be set
                }

                if (!CheckOutputValue(r, &*txbout, nFeeRet, sError)) {
                    return 1; // sError set
                }

                if (r.nType == OUTPUT_STANDARD) {
                    nValueOutPlain += r.nAmount;
                }

                if (r.fChange && r.nType == OUTPUT_CT) {
                    nChangePosInOut = i;
                }

                r.n = txNew.vpout.size();
                txNew.vpout.push_back(txbout);

                if (r.nType == OUTPUT_CT || r.nType == OUTPUT_RINGCT) {
                    // Need to know the fee before calculating the blind sum
                    if (r.vBlind.size() != 32) {
                        r.vBlind.resize(32);
                        GetStrongRandBytes(&r.vBlind[0], 32);
                    }

                    if (!AddCTData(txbout.get(), r, sError)) {
                        return 1; // sError will be set
                    }
                }
            }

            // Fill in dummy signatures for fee calculation.
            int nIn = 0;
            for (const auto &coin : setCoins) {
                const uint256 &txhash = coin.first->first;
                const COutputRecord *outputRecord = coin.first->second.GetOutput(coin.second);
                CScript scriptPubKey = outputRecord->scriptPubKey;

                if (scriptPubKey.empty()) {
                    if (!GetScriptPubKeyFromOutpoint(COutPoint(txhash, outputRecord->n), scriptPubKey))
                        return wserrorN(1, sError, __func__, strprintf("Failed to find input scriptpubkey for tx %s", txhash.GetHex()));
                }

                CBasicKeyStore keystore;
                if (!MakeSigningKeystore(keystore, scriptPubKey))
                    return wserrorN(1, sError, __func__, "Could not locate signing key");

                // Use witness size estimate if set
                COutPoint prevout(txhash, coin.second);
                std::map<COutPoint, CInputData>::const_iterator it = coinControl->m_inputData.find(prevout);
                SignatureData sigdata;
                if (it != coinControl->m_inputData.end()) {
                    sigdata.scriptWitness = it->second.scriptWitness;
                } else if (!ProduceSignature(keystore, DUMMY_SIGNATURE_CREATOR, scriptPubKey, sigdata)) {
                    return wserrorN(1, sError, __func__, "Dummy signature failed.");
                }
                UpdateInput(txNew.vin[nIn], sigdata);
                nIn++;
            }

            nBytes = GetVirtualTransactionSize(txNew);

            nFeeNeeded = GetMinimumFee(*pwalletParent, nBytes, *coinControl, ::mempool, ::feeEstimator, &feeCalc);
            if (nFeeZerocoin != 0)
                nFeeNeeded = nFeeZerocoin;

            // If we made it here and we aren't even able to meet the relay fee on the next pass, give up
            // because we must be at the maximum allowed fee.
            if (nFeeNeeded < ::minRelayTxFee.GetFee(nBytes)) {
                return wserrorN(1, sError, __func__, _("Transaction too large for fee policy."));
            }

            if (nFeeRet >= nFeeNeeded) {
                // Reduce fee to only the needed amount if possible. This
                // prevents potential overpayment in fees if the coins
                // selected to meet nFeeNeeded result in a transaction that
                // requires less fee than the prior iteration.
                if (nFeeRet > nFeeNeeded && nChangePosInOut != -1
                    && nSubtractFeeFromAmount == 0) {
                    auto &r = vecSend[nChangePosInOut];

                    CAmount extraFeePaid = nFeeRet - nFeeNeeded;

                    r.nAmount += extraFeePaid;
                    nFeeRet -= extraFeePaid;
                }

                if (nSubtractFeeFromAmount) {
                    if (nValueOutPlain + nFeeRet == nValueIn) {
                        // blinded input value == plain output value
                        // blinding factor will be 0 for change
                        // an observer could see sum blinded inputs must match plain outputs, avoid by forcing a 1sat change output

                        bool fFound = false;
                        for (auto &r : vecSend) {
                            if (r.nType == OUTPUT_STANDARD
                                && r.nAmountSelected > 0
                                && r.fSubtractFeeFromAmount
                                && !r.fChange) {
                                r.SetAmount(r.nAmountSelected-1);
                                fFound = true;
                                nValueOutBlind -= 1;
                                break;
                            }
                        }

                        if (!fFound || !(--nSubFeeTries)) {
                            return wserrorN(1, sError, __func__, _("Unable to reduce plain output to add blind change."));
                        }

                        pick_new_inputs = false;
                        continue;
                    }
                }

                break; // Done, enough fee included.
            } else if (!pick_new_inputs) {
                // This shouldn't happen, we should have had enough excess
                // fee to pay for the new output and still meet nFeeNeeded
                // Or we should have just subtracted fee from recipients and
                // nFeeNeeded should not have changed

                if (!nSubtractFeeFromAmount || !(--nSubFeeTries)) { // rangeproofs can change size per iteration
                    return wserrorN(1, sError, __func__, _("Transaction fee and change calculation failed."));
                }
            }

            // Try to reduce change to include necessary fee
            if (nChangePosInOut != -1 && nSubtractFeeFromAmount == 0) {
                auto &r = vecSend[nChangePosInOut];

                CAmount additionalFeeNeeded = nFeeNeeded - nFeeRet;

                if (r.nAmount >= MIN_FINAL_CHANGE + additionalFeeNeeded) {
                    r.nAmount -= additionalFeeNeeded;
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
            continue;
        }
        coinControl->nChangePos = nChangePosInOut;


        nValueOutPlain += nFeeRet;

        std::vector<uint8_t> vInputBlinds(32 * setCoins.size());
        std::vector<uint8_t*> vpBlinds;

        int nIn = 0;
        for (const auto &coin : setCoins) {
            auto &txin = txNew.vin[nIn];
            const uint256 &txhash = coin.first->first;

            COutPoint prevout(txhash, coin.second);
            //std::map<COutPoint, CInputData>::const_iterator it = coinControl->m_inputData.find(prevout);
            //if (it != coinControl->m_inputData.end()) {
            //    memcpy(&vInputBlinds[nIn * 32], it->second.blind.begin(), 32);
            //} else {
                CStoredTransaction stx;
                if (!AnonWalletDB(*walletDatabase).ReadStoredTx(txhash, stx)) {
                    return werrorN(1, "%s: ReadStoredTx failed for %s.\n", __func__, txhash.ToString().c_str());
                }

                if (!stx.GetBlind(coin.second, &vInputBlinds[nIn * 32])) {
                    //Try to manually get blinds
                    uint256 blind;
                    if (!GetCTBlindsFromOutput(stx.tx->vpout[coin.second].get(), blind))
                        return werrorN(1, "%s: GetBlind failed for %s, %d.\n", __func__, txhash.ToString().c_str(), coin.second);
                    memcpy(&vInputBlinds[nIn * 32], blind.begin(), 32);
                }
            //}
            vpBlinds.push_back(&vInputBlinds[nIn * 32]);

            // Remove scriptSigs to eliminate the fee calculation dummy signatures
            txin.scriptSig = CScript();
            txin.scriptWitness.SetNull();
            nIn++;
        }


        size_t nBlindedInputs = vpBlinds.size();

        std::vector<uint8_t> vBlindPlain;
        vBlindPlain.resize(32);
        memset(&vBlindPlain[0], 0, 32);

        //secp256k1_pedersen_commitment plainCommitment;
        if (nValueOutPlain > 0) {
            vpBlinds.push_back(&vBlindPlain[0]);
            //if (!secp256k1_pedersen_commit(secp256k1_ctx_blind, &plainCommitment, &vBlindPlain[0], (uint64_t) nValueOutPlain, secp256k1_generator_h))
            //    return errorN(1, sError, __func__, "secp256k1_pedersen_commit failed for plain out.");
        }

        // Update the change output commitment if it exists, else last blinded
        int nLastBlinded = -1;
        for (size_t i = 0; i < vecSend.size(); ++i) {
            auto &r = vecSend[i];

            if (r.nType == OUTPUT_CT || r.nType == OUTPUT_RINGCT) {
                if ((int)i != nChangePosInOut) {
                    vpBlinds.push_back(&r.vBlind[0]);
                }

                nLastBlinded = i;
            }
        }

        if (nChangePosInOut != -1) {
            nLastBlinded = nChangePosInOut; // Use the change output
        } else if (nLastBlinded != -1) {
            vpBlinds.pop_back();
        }

        if (nLastBlinded != -1) {
            auto &r = vecSend[nLastBlinded];
            if (r.nType != OUTPUT_CT && r.nType != OUTPUT_RINGCT) {
                return wserrorN(1, sError, __func__, "nLastBlinded not blind.");
            }

            // Last to-be-blinded value: compute from all other blinding factors.
            // sum of output blinding values must equal sum of input blinding values
            if (!secp256k1_pedersen_blind_sum(secp256k1_ctx_blind, &r.vBlind[0], &vpBlinds[0], vpBlinds.size(), nBlindedInputs)) {
                return wserrorN(1, sError, __func__, "secp256k1_pedersen_blind_sum failed.");
            }

            CTxOutBase *pout = (CTxOutBase*)txNew.vpout[r.n].get();
            if (!AddCTData(pout, r, sError)) {
                return 1; // sError will be set
            }
        }


        std::vector<uint8_t> &vData = ((CTxOutData*)txNew.vpout[0].get())->vData;
        vData.resize(1);
        if (0 != PutVarInt(vData, nFeeRet)) {
            return werrorN(1, "%s: PutVarInt %d failed\n", __func__, nFeeRet);
        }

        if (sign) {
            int nIn = 0;
            for (const auto &coin : setCoins) {
                const uint256 &txhash = coin.first->first;
                const COutputRecord *outputRecord = coin.first->second.GetOutput(coin.second);
                if (!outputRecord) {
                    return werrorN(1, "%s: GetOutput %s failed.\n", __func__, txhash.ToString().c_str());
                }

                CScript scriptPubKey = outputRecord->scriptPubKey;
                if (scriptPubKey.empty()) {
                    if (!GetScriptPubKeyFromOutpoint(COutPoint(txhash, outputRecord->n), scriptPubKey))
                        return wserrorN(1, sError, __func__, strprintf("Failed to find input scriptpubkey in tx %s", txhash.GetHex()));
                }

                CBasicKeyStore keystore;
                if (!MakeSigningKeystore(keystore, scriptPubKey))
                    return wserrorN(1, sError, __func__, "Could not locate signing key");

                CStoredTransaction stx;
                if (!AnonWalletDB(*walletDatabase).ReadStoredTx(txhash, stx)) {
                    return werrorN(1, "%s: ReadStoredTx failed for %s.\n", __func__, txhash.ToString().c_str());
                }
                std::vector<uint8_t> vchAmount;
                stx.tx->vpout[coin.second]->PutValue(vchAmount);

                SignatureData sigdata;

                if (!ProduceSignature(keystore, MutableTransactionSignatureCreator(&txNew, nIn, vchAmount, SIGHASH_ALL), scriptPubKey, sigdata))
                    return wserrorN(1, sError, __func__, _("Signing transaction failed"));
                UpdateInput(txNew.vin[nIn], sigdata);

                nIn++;
            }
        }

        rtx.nFee = nFeeRet;
        rtx.nFlags |= ORF_BLIND_IN;
        AddOutputRecordMetaData(rtx, vecSend);
        for (auto txin : txNew.vin)
            rtx.vin.emplace_back(txin.prevout);
        MarkInputsAsPendingSpend(rtx);

        uint256 txid = txNew.GetHash();
        if (nValueOutZerocoin)
            rtx.AddPartialTxid(txid);
        //save record
        SaveRecord(txid, rtx);

        // Embed the constructed transaction data in wtxNew.
        wtx.SetTx(MakeTransactionRef(std::move(txNew)));

    } // cs_main, pwalletParent->cs_wallet

    if (!PreAcceptMempoolTx(wtx, sError)) {
        return 1;
    }

    LogPrintf("Fee Calculation: Fee:%d Bytes:%u Needed:%d Tgt:%d (requested %d) Reason:\"%s\" Decay %.5f: Estimation: (%g - %g) %.2f%% %.1f/(%.1f %d mem %.1f out) Fail: (%g - %g) %.2f%% %.1f/(%.1f %d mem %.1f out)\n",
              nFeeRet, nBytes, nFeeNeeded, feeCalc.returnedTarget, feeCalc.desiredTarget, StringForFeeReason(feeCalc.reason), feeCalc.est.decay,
              feeCalc.est.pass.start, feeCalc.est.pass.end,
              100 * feeCalc.est.pass.withinTarget / (feeCalc.est.pass.totalConfirmed + feeCalc.est.pass.inMempool + feeCalc.est.pass.leftMempool),
              feeCalc.est.pass.withinTarget, feeCalc.est.pass.totalConfirmed, feeCalc.est.pass.inMempool, feeCalc.est.pass.leftMempool,
              feeCalc.est.fail.start, feeCalc.est.fail.end,
              100 * feeCalc.est.fail.withinTarget / (feeCalc.est.fail.totalConfirmed + feeCalc.est.fail.inMempool + feeCalc.est.fail.leftMempool),
              feeCalc.est.fail.withinTarget, feeCalc.est.fail.totalConfirmed, feeCalc.est.fail.inMempool, feeCalc.est.fail.leftMempool);
    return 0;
}

int AnonWallet::AddBlindedInputs(CWalletTx &wtx, CTransactionRecord &rtx, std::vector<CTempRecipient> &vecSend, bool sign,
        CAmount &nFeeRet, const CCoinControl *coinControl, std::string &sError)
{
    if (vecSend.size() < 1) {
        return wserrorN(1, sError, __func__, _("Transaction must have at least one recipient."));
    }

    CAmount nValue = 0;
    for (const auto &r : vecSend) {
        nValue += r.nAmount;
        if (nValue < 0 || r.nAmount < 0) {
            return wserrorN(1, sError, __func__, _("Transaction amounts must not be negative."));
        }
    }

    if (0 != AddBlindedInputs_Inner(wtx, rtx, vecSend, sign, nFeeRet, coinControl, sError)) {
        return 1;
    }

    return 0;
}

bool AnonWallet::PlaceRealOutputs(std::vector<std::vector<int64_t> > &vMI, size_t &nSecretColumn, size_t nRingSize,
                                  std::set<int64_t> &setHave,
                                  const std::vector<std::pair<MapRecords_t::const_iterator,unsigned int> > &vCoins,
                                  std::vector<uint8_t> &vInputBlinds, std::string &sError)
{
    if (nRingSize < MIN_RINGSIZE || nRingSize > MAX_RINGSIZE) {
        sError = strprintf("Ring size out of range [%d, %d].", MIN_RINGSIZE, MAX_RINGSIZE);
        return error("%s: %s", __func__, sError);
    }

    //GetStrongRandBytes((unsigned char*)&nSecretColumn, sizeof(nSecretColumn));
    //nSecretColumn %= nRingSize;
    nSecretColumn = GetRandInt(nRingSize);

    AnonWalletDB wdb(*walletDatabase);
    vMI.resize(vCoins.size());

    for (size_t k = 0; k < vCoins.size(); ++k) {
        vMI[k].resize(nRingSize);
        for (size_t i = 0; i < nRingSize; ++i) {
            if (i == nSecretColumn) {
                // TODO: Store pubkey on COutputRecord - in scriptPubKey?
                const auto &coin = vCoins[k];
                const uint256 &txhash = coin.first->first;
                CStoredTransaction stx;
                if (!wdb.ReadStoredTx(txhash, stx)) {
                    sError = strprintf("Failed to read stored transaction %s", txhash.ToString().c_str());
                    return error("%s: %s", __func__, sError);
                }

                if (!stx.tx->vpout[coin.second]->IsType(OUTPUT_RINGCT)) {
                    sError = strprintf("Output %d is not a RingCT output: %s", coin.second, txhash.ToString().c_str());
                    return error("%s: %s", __func__, sError);
                }

                CCmpPubKey pk = ((CTxOutRingCT*)stx.tx->vpout[coin.second].get())->pk;

                if (!stx.GetBlind(coin.second, &vInputBlinds[k * 32])) {
                    //Try to manually get blinds
                    CTransactionRef ptx;
                    uint256 hashBlock;
                    if (!GetTransaction(txhash, ptx, Params().GetConsensus(), hashBlock, true)) {
                        sError = strprintf("Failed to get transaction %d: %s", coin.second, txhash.ToString().c_str());
                        return error("%s: %s", __func__, sError);
                    }
                    uint256 blind;
                    if (!GetCTBlindsFromOutput(ptx->vpout[coin.second].get(), blind)) {
                        sError = strprintf("Failed to get CT blinds for output %d: %s", coin.second, txhash.ToString().c_str());
                        return error("%s: %s", __func__, sError);
                    }
                    memcpy(&vInputBlinds[k * 32], blind.begin(), 32);

                    pk = ((CTxOutRingCT*)ptx->vpout[coin.second].get())->pk;
                }

                int64_t index;
                if (!pblocktree->ReadRCTOutputLink(pk, index)) {
                    sError = strprintf("RingCT public key not found in database: %s", HexStr(pk.begin(), pk.end()));
                    return error("%s: %s", __func__, sError);
                }

                if (setHave.count(index)) {
                    sError = strprintf("Duplicate index found: %d.", index);
                    return error("%s: %s", __func__, sError);
                }

                vMI[k][i] = index;
                setHave.insert(index);
            }
        }
    }

    return true;
}

bool AnonWallet::PickHidingOutputs(std::vector<std::vector<int64_t> > &vMI, size_t nSecretColumn, size_t nRingSize, std::set<int64_t> &setHave,
    std::string &sError)
{
    if (nRingSize < MIN_RINGSIZE || nRingSize > MAX_RINGSIZE) {
        sError = strprintf("Ring size out of range [%d, %d].", MIN_RINGSIZE, MAX_RINGSIZE);
        return error("%s: %s", __func__, sError);
    }

    int nBestHeight = chainActive.Tip()->nHeight;
    const Consensus::Params& consensusParams = Params().GetConsensus();
    size_t nInputs = vMI.size();

    int64_t nLastRCTOutIndex = 0;
    {
        AssertLockHeld(cs_main);
        nLastRCTOutIndex = chainActive.Tip()->nAnonOutputs;
    }

    if (nLastRCTOutIndex < (int64_t)(nInputs * nRingSize)) {
        sError = strprintf("Not enough anonymous outputs exist, last: %d, required: %d.",
                           nLastRCTOutIndex, nInputs * nRingSize);
        return error("%s: %s", __func__, sError);
    }

    int nExtraDepth = gArgs.GetBoolArg("-regtest", false) ? -1 : 2; // if not on regtest pick outputs deeper than consensus checks to prevent banning

    // Must add real outputs to setHave before adding the decoys.
    for (size_t k = 0; k < nInputs; ++k)
    for (size_t i = 0; i < nRingSize; ++i) {
        if (i == nSecretColumn) {
            continue;
        }

        int64_t nMinIndex = 1;
        if (GetRandInt(100) < 50) { // 50% chance of selecting from the last 2400
            nMinIndex = std::max((int64_t)1, nLastRCTOutIndex - nRCTOutSelectionGroup1);
        } else if (GetRandInt(100) < 70) { // further 70% chance of selecting from the last 24000
            nMinIndex = std::max((int64_t)1, nLastRCTOutIndex - nRCTOutSelectionGroup2);
        }

        int64_t nLastDepthCheckPassed = 0;
        size_t j = 0;
        const static size_t nMaxTries = 1000;
        for (j = 0; j < nMaxTries; ++j) {
            if (nLastRCTOutIndex <= nMinIndex) {
                sError = strprintf("Not enough anonymous outputs exist, min: last: %d, required: %d.",
                                   nMinIndex, nLastRCTOutIndex, nInputs * nRingSize);
                return error("%s: %s", __func__, sError);
            }

            int64_t nDecoy = nMinIndex + GetRand((nLastRCTOutIndex - nMinIndex) + 1);

            if (setHave.count(nDecoy) > 0) {
                if (nDecoy == nLastRCTOutIndex) {
                    nLastRCTOutIndex--;
                }
                continue;
            }

            CAnonOutput ao;
            if (!pblocktree->ReadRCTOutput(nDecoy, ao)) {
                sError = strprintf("Anonymous output not found in database: %s.", nDecoy);
                return error("%s: %s", __func__, sError);
            }

            //Don't mix in blacklisted decoys
            if (blacklist::ContainsRingCtOutPoint(ao.outpoint))
                continue;

            if (nDecoy > nLastDepthCheckPassed) {
                if (ao.nBlockHeight > nBestHeight - (consensusParams.nMinRCTOutputDepth + nExtraDepth)) {
                    if (nLastRCTOutIndex > nDecoy) {
                        nLastRCTOutIndex = nDecoy-1;
                    }
                    continue;
                }

                nLastDepthCheckPassed = nDecoy;
            }

            vMI[k][i] = nDecoy;
            setHave.insert(nDecoy);
            break;
        }

        if (j >= nMaxTries) {
            sError = strprintf("Exceeded maximum tries for picking hiding outputs (%d, %d).", k, i);
            return error("%s: %s", __func__, sError);
        }
    }

    return true;
}

/**Compute whether an anon input's key images belongs to us**/
bool AnonWallet::IsMyAnonInput(const CTxIn& txin, COutPoint& myOutpoint)
{
    uint32_t nInputs, nRingSize;
    txin.GetAnonInfo(nInputs, nRingSize);

    size_t nCols = nRingSize;
    size_t nRows = nInputs + 1;

    if (txin.scriptData.stack.size() != 1)
        return false;

    if (txin.scriptWitness.stack.size() != 2)
        return false;

    const std::vector<uint8_t> vKeyImages = txin.scriptData.stack[0];
    const std::vector<uint8_t> vMI = txin.scriptWitness.stack[0];
    const std::vector<uint8_t> vDL = txin.scriptWitness.stack[1];

    if (vKeyImages.size() != nInputs * 33)
        return false;

    std::vector<uint8_t> vM(nCols * nRows * 33);
    std::set<CKey> setKeyCandidates;

    size_t ofs = 0, nB = 0;
    for (size_t k = 0; k < nInputs; ++k) {
        auto image = *((CCmpPubKey*)&vKeyImages[k*33]);
        for (size_t i = 0; i < nCols; ++i) {
            int64_t nIndex = 0;

            if (0 != GetVarInt(vMI, ofs, (uint64_t &) nIndex, nB))
                return false;
            ofs += nB;

            CAnonOutput ao;
            if (!pblocktree->ReadRCTOutput(nIndex, ao))
                return false;

            CKeyID stealthDest = ao.pubkey.GetID();
            CKey key;
            if (GetKey(stealthDest, key)) {
                std::vector<uint8_t> vchKeyImage(33);
                if (0 != secp256k1_get_keyimage(secp256k1_ctx_blind, vchKeyImage.data(), ao.pubkey.begin(), key.begin()))
                    continue;

                if (vchKeyImage == image) {
                    myOutpoint = ao.outpoint;
                    return true;
                }

            }
        }
    }

    return false;
}

// Returns bool
bool AnonWallet::AddAnonInputs_Inner(CWalletTx &wtx, CTransactionRecord &rtx, std::vector<CTempRecipient> &vecSend,
        bool sign, size_t nRingSize, size_t nInputsPerSig, CAmount &nFeeRet, const CCoinControl *coinControl,
        std::string &sError, bool fZerocoinInputs, CAmount nInputValue)
{
    assert(coinControl);
    if (nRingSize < MIN_RINGSIZE || nRingSize > MAX_RINGSIZE) {
        sError = strprintf("Ring size out of range.");
        return error("%s: %s", __func__, sError);
    }

    if (nInputsPerSig < 1 || nInputsPerSig > MAX_ANON_INPUTS) {
        sError = strprintf("Num inputs per signature out of range.");
        return error("%s: %s", __func__, sError);
    }

    nFeeRet = 0;
    CAmount nValueOutAnon;
    size_t nSubtractFeeFromAmount;
    bool fOnlyStandardOutputs, fSkipFee, fSendingOnlyBaseCoin;
    InspectOutputs(vecSend, fZerocoinInputs, nValueOutAnon, nSubtractFeeFromAmount, fOnlyStandardOutputs, fSkipFee, fSendingOnlyBaseCoin);

    //Need count of zerocoin mint outputs in order to calculate fee correctly
    int nZerocoinMintOuts = 0;
    CAmount nValueOutZerocoin = 0;
    CAmount nFeeZerocoin = 0;
    bool fCTOut = false;
    for (auto& r : vecSend) {
        if (r.nType == OUTPUT_CT)
            fCTOut = true;
        if (r.fZerocoinMint) {
            nZerocoinMintOuts++;
            nValueOutZerocoin += r.nAmount;
            nFeeZerocoin += Params().Zerocoin_MintFee();
            //Zerocoin mints have to be exact amount, so not eligible to subtract fee
            r.fExemptFeeSub = true;
        }
    }
    fSkipFee = fZerocoinInputs && nZerocoinMintOuts == 0;
    //If output is going out as zerocoin, then it is being double counted and needs to be subtracted
    nValueOutAnon -= nValueOutZerocoin;

    if (!ExpandTempRecipients(vecSend, sError))
        return false;

    wtx.fTimeReceivedIsTxTime = true;
    wtx.BindWallet(pwalletParent.get());
    wtx.fFromMe = true;
    CMutableTransaction txNew;
    if (fZerocoinInputs) {
        txNew = CMutableTransaction(*wtx.tx);
    }

    txNew.nLockTime = 0;

    coinControl->fHaveAnonOutputs = true;
    FeeCalculation feeCalc;
    CAmount nFeeNeeded;
    bool fAlreadyHaveInputs = fZerocoinInputs;
    unsigned int nBytes;
    {
        LOCK2(cs_main, pwalletParent->cs_wallet);
        std::vector<std::pair<MapRecords_t::const_iterator, unsigned int> > setCoins;
        std::vector<COutputR> vAvailableCoins;
        if (!fAlreadyHaveInputs)
            AvailableAnonCoins(vAvailableCoins, true, coinControl);

        CAmount nValueOutPlain = 0;
        int nChangePosInOut = -1;

        std::vector<std::vector<std::vector<int64_t> > > vMI;
        std::vector<std::vector<uint8_t> > vInputBlinds;
        std::vector<size_t> vSecretColumns;

        size_t nSubFeeTries = 100;
        bool pick_new_inputs = true;
        CAmount nValueIn = nInputValue;

        // Start with no fee and loop until there is enough fee
        int nIterations = 0;
        while (true) {
            if (nIterations > 20) {
                sError = strprintf("Unable to properly calculate transaction fee with ringct inputs.");
                return error("%s: %s", __func__, sError);
            }
            nIterations++;

            if (!fAlreadyHaveInputs)
                txNew.vin.clear();
            txNew.vpout.clear();
            wtx.fFromMe = true;

            CAmount nValueToSelect = nValueOutAnon + nValueOutZerocoin;
            if (nSubtractFeeFromAmount == 0) {
                nValueToSelect += nFeeRet;
            }

            // Choose coins to use
            if (!fAlreadyHaveInputs && pick_new_inputs) {
                nValueIn = 0;
                setCoins.clear();
                if (!SelectBlindedCoins(vAvailableCoins, nValueToSelect, setCoins, nValueIn, coinControl)) {
                    sError = strprintf("Insufficient funds.");
                    return error("%s: %s", __func__, sError);
                }
            }

            const CAmount nChange = nValueIn - nValueToSelect;

            // Remove fee outputs from last round
            for (size_t i = 0; i < vecSend.size(); ++i) {
                if (vecSend[i].fChange) {
                    vecSend.erase(vecSend.begin() + i);
                    i--;
                }
            }

            // Insert a sender-owned 0 value output that becomes the change output if needed
            {
                // Fill an output to ourself
                CTempRecipient recipient;
                recipient.nType = fCTOut ? OUTPUT_CT : OUTPUT_RINGCT;
                recipient.fChange = true;

                //If no change address is set, then use the default change address
                if (!coinControl || ((coinControl && coinControl->destChange.type() == typeid(CNoDestination)))) {
                    recipient.address = GetStealthChangeAddress();
                }

                if (!SetChangeDest(coinControl, recipient, sError)) {
                    return false;
                }

                if ((fSkipFee && nChange) || nChange > ::minRelayTxFee.GetFee(2048)) { // TODO: better output size estimate
                    recipient.SetAmount(nChange);
                } else {
                    recipient.SetAmount(0);
                    nFeeRet += nChange;
                }

                nChangePosInOut = coinControl->nChangePos;
                InsertChangeAddress(recipient, vecSend, nChangePosInOut);
            }

            int nRemainder = setCoins.size() % nInputsPerSig;
            int nTxRingSigs = setCoins.size() / nInputsPerSig + (nRemainder == 0 ? 0 : 1);

            size_t nRemainingInputs = setCoins.size();

            //Add blank anon inputs as anon inputs
            if (!fAlreadyHaveInputs) {
                for (int k = 0; k < nTxRingSigs; ++k) {
                    size_t nInputs = (k == nTxRingSigs - 1 ? nRemainingInputs : nInputsPerSig);
                    CTxIn txin;
                    txin.nSequence = CTxIn::SEQUENCE_FINAL;
                    txin.prevout.n = COutPoint::ANON_MARKER;
                    txin.SetAnonInfo(nInputs, nRingSize);
                    txNew.vin.emplace_back(txin);

                    nRemainingInputs -= nInputs;
                }
            }

            vMI.clear();
            vInputBlinds.clear();
            vSecretColumns.clear();
            vMI.resize(nTxRingSigs);
            vInputBlinds.resize(nTxRingSigs);
            vSecretColumns.resize(nTxRingSigs);
            nValueOutPlain = 0;
            nChangePosInOut = -1;

            OUTPUT_PTR<CTxOutData> outFee = MAKE_OUTPUT<CTxOutData>();
            outFee->vData.push_back(DO_FEE);
            outFee->vData.resize(9); // More bytes than varint fee could use
            txNew.vpout.push_back(outFee);

            bool fFirst = true;
            for (size_t i = 0; i < vecSend.size(); ++i) {
                auto &recipient = vecSend[i];

                if (!fSkipFee)
                    recipient.ApplySubFee(nFeeRet, nSubtractFeeFromAmount, fFirst);

                OUTPUT_PTR<CTxOutBase> txbout;
                CreateOutput(txbout, recipient, sError);

                if (recipient.nType == OUTPUT_STANDARD) {
                    nValueOutPlain += recipient.nAmount;
                }

                if (recipient.fChange && recipient.nType == OUTPUT_RINGCT) {
                    nChangePosInOut = i;
                }

                recipient.n = txNew.vpout.size();
                txNew.vpout.push_back(txbout);
                if (recipient.nType == OUTPUT_CT || recipient.nType == OUTPUT_RINGCT) {
                    if (recipient.vBlind.size() != 32) {
                        recipient.vBlind.resize(32);
                        GetStrongRandBytes(&recipient.vBlind[0], 32);
                    }

                    if (!AddCTData(txbout.get(), recipient, sError))
                        return false;
                }
            }

            if (!fAlreadyHaveInputs) {
                std::set<int64_t> setHave; // Anon prev-outputs can only be used once per transaction.
                size_t nTotalInputs = 0;
                for (size_t l = 0; l < txNew.vin.size(); ++l) { // Must add real outputs to setHave before picking decoys
                    auto &txin = txNew.vin[l];
                    uint32_t nSigInputs, nSigRingSize;
                    txin.GetAnonInfo(nSigInputs, nSigRingSize);

                    vInputBlinds[l].resize(32 * nSigInputs);
                    std::vector<std::pair<MapRecords_t::const_iterator, unsigned int> >
                            vCoins(setCoins.begin() + nTotalInputs, setCoins.begin() + nTotalInputs + nSigInputs);
                    nTotalInputs += nSigInputs;

                    if (!PlaceRealOutputs(vMI[l], vSecretColumns[l], nSigRingSize, setHave, vCoins, vInputBlinds[l], sError)) {
                        return false;
                    }
                }

                // Fill in dummy signatures for fee calculation.
                for (size_t l = 0; l < txNew.vin.size(); ++l) {
                    auto &txin = txNew.vin[l];
                    uint32_t nSigInputs, nSigRingSize;
                    txin.GetAnonInfo(nSigInputs, nSigRingSize);

                    if (!PickHidingOutputs(vMI[l], vSecretColumns[l], nSigRingSize, setHave, sError))
                        return false;

                    std::vector<uint8_t> vPubkeyMatrixIndices;

                    for (size_t k = 0; k < nSigInputs; ++k)
                        for (size_t i = 0; i < nSigRingSize; ++i) {
                            PutVarInt(vPubkeyMatrixIndices, vMI[l][k][i]);
                        }

                    std::vector<uint8_t> vKeyImages(33 * nSigInputs);
                    txin.scriptData.stack.emplace_back(vKeyImages);

                    txin.scriptWitness.stack.emplace_back(vPubkeyMatrixIndices);

                    std::vector<uint8_t> vDL((1 + (nSigInputs + 1) * nSigRingSize) *
                                             32 // extra element for C, extra row for commitment row
                                             + (txNew.vin.size() > 1 ? 33
                                                                     : 0)); // extra commitment for split value if multiple sigs
                    txin.scriptWitness.stack.emplace_back(vDL);
                }
            }

            if (fSkipFee) {
                nFeeNeeded = 0;
                nFeeRet = 0;
                std::vector<uint8_t> &vData = ((CTxOutData*)txNew.vpout[0].get())->vData;
                vData.resize(1);
                if (0 != PutVarInt(vData, 0)) {
                    sError = strprintf("Failed to add skipped fee to transaction.");
                    return error("%s: %s", __func__, sError);
                }
                break;
            }

            nBytes = GetVirtualTransactionSize(txNew);

            nFeeNeeded = GetMinimumFee(*pwalletParent, nBytes, *coinControl, ::mempool, ::feeEstimator, &feeCalc);
            if (nFeeZerocoin)
                nFeeNeeded = nFeeZerocoin;

            // If we made it here and we aren't even able to meet the relay fee on the next pass, give up
            // because we must be at the maximum allowed fee.
            if (!fSkipFee && nFeeNeeded < ::minRelayTxFee.GetFee(nBytes)) {
                sError = strprintf("Transaction too large for fee policy.");
                return error("%s: %s", __func__, sError);
            }
            if (!fSkipFee) {
                if (nFeeRet >= nFeeNeeded) {
                    // Reduce fee to only the needed amount if possible. This
                    // prevents potential overpayment in fees if the coins
                    // selected to meet nFeeNeeded result in a transaction that
                    // requires less fee than the prior iteration.
                    if (nFeeRet > nFeeNeeded && nChangePosInOut != -1 && nSubtractFeeFromAmount == 0) {
                        auto &r = vecSend[nChangePosInOut];

                        CAmount extraFeePaid = nFeeRet - nFeeNeeded;

                        r.nAmount += extraFeePaid;
                        nFeeRet -= extraFeePaid;
                    }
                    break; // Done, enough fee included.
                } else if (!pick_new_inputs) {
                    // This shouldn't happen, we should have had enough excess
                    // fee to pay for the new output and still meet nFeeNeeded
                    // Or we should have just subtracted fee from recipients and
                    // nFeeNeeded should not have changed

                    if (!nSubtractFeeFromAmount || !(--nSubFeeTries)) {
                        sError = strprintf("Transaction fee and change calculation failed.");
                        return error("%s: %s", __func__, sError);
                    }
                }

                // Try to reduce change to include necessary fee
                if (nChangePosInOut != -1 && nSubtractFeeFromAmount == 0) {
                    auto &r = vecSend[nChangePosInOut];
                    CAmount additionalFeeNeeded = nFeeNeeded - nFeeRet;
                    if (r.nAmount >= MIN_FINAL_CHANGE + additionalFeeNeeded) {
                        r.nAmount -= additionalFeeNeeded;
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
            }
            if (fZerocoinInputs) {
                sError = strprintf("Not able to calculate fee.");
                return error("%s: %s", __func__, sError);
            }
        }

        coinControl->nChangePos = nChangePosInOut;
        nValueOutPlain += nFeeRet;

        // Remove scriptSigs to eliminate the fee calculation dummy signatures
        if (!fZerocoinInputs) {
            for (auto &txin : txNew.vin) {
                txin.scriptData.stack[0].resize(0);
                txin.scriptWitness.stack[1].resize(0);
            }
        }

        std::vector<const uint8_t*> vpOutCommits;
        std::vector<const uint8_t*> vpOutBlinds;
        std::vector<uint8_t> vBlindPlain;
        secp256k1_pedersen_commitment plainCommitment;
        vBlindPlain.resize(32);
        memset(&vBlindPlain[0], 0, 32);

        if (nValueOutPlain > 0) {
            if (!secp256k1_pedersen_commit(secp256k1_ctx_blind, &plainCommitment, &vBlindPlain[0],
                                          (uint64_t) nValueOutPlain, secp256k1_generator_h)) {
                sError = strprintf("Pedersen Commit failed for plain out.");
                return error("%s: %s", __func__, sError);
            }

            vpOutCommits.push_back(plainCommitment.data);
            vpOutBlinds.push_back(&vBlindPlain[0]);
        }

        // Update the change output commitment
        for (size_t i = 0; i < vecSend.size(); ++i) {
            auto &r = vecSend[i];

            if ((int)i == nChangePosInOut) {
                // Change amount may have changed

                if (r.nType != OUTPUT_RINGCT) {
                    sError = strprintf("Change output is not RingCT type.");
                    return error("%s: %s", __func__, sError);
                }

                if (r.vBlind.size() != 32) {
                    r.vBlind.resize(32);
                    GetStrongRandBytes(&r.vBlind[0], 32);
                }

                if (!AddCTData(txNew.vpout[r.n].get(), r, sError))
                    return false;
            }

            if (r.nType == OUTPUT_CT || r.nType == OUTPUT_RINGCT) {
                vpOutCommits.push_back(txNew.vpout[r.n]->GetPCommitment()->data);
                vpOutBlinds.push_back(&r.vBlind[0]);
            }
        }

        //Add actual fee to CT Fee output
        std::vector<uint8_t> &vData = ((CTxOutData*)txNew.vpout[0].get())->vData;
        vData.resize(1);
        if (0 != PutVarInt(vData, nFeeRet)) {
            sError = strprintf("Failed to add fee to transaction.");
            return error("%s: %s", __func__, sError);
        }

        if (!fZerocoinInputs && sign) {
            std::vector<CKey> vSplitCommitBlindingKeys(txNew.vin.size()); // input amount commitment when > 1 mlsag
            int rv;
            size_t nTotalInputs = 0;

            for (size_t l = 0; l < txNew.vin.size(); ++l) {
                auto &txin = txNew.vin[l];

                uint32_t nSigInputs, nSigRingSize;
                txin.GetAnonInfo(nSigInputs, nSigRingSize);

                std::vector<uint8_t> &vKeyImages = txin.scriptData.stack[0];
                vKeyImages.resize(33 * nSigInputs);

                for (size_t k = 0; k < nSigInputs; ++k) {
                    size_t i = vSecretColumns[l];
                    int64_t nIndex = vMI[l][k][i];

                    CAnonOutput anonOutput;
                    if (!pblocktree->ReadRCTOutput(nIndex, anonOutput)) {
                        sError = strprintf("Output %d not found in database.", nIndex);
                        return error("%s: %s", __func__, sError);
                    }

                    CKeyID idk = anonOutput.pubkey.GetID();
                    CKey key;
                    if (!GetKey(idk, key)) {
                        sError = strprintf("No key for output: %s", HexStr(anonOutput.pubkey.begin(), anonOutput.pubkey.end()));
                        return error("%s: %s", __func__, sError);
                    }

                    // Keyimage is required for the tx hash
                    rv = secp256k1_get_keyimage(secp256k1_ctx_blind, &vKeyImages[k * 33], anonOutput.pubkey.begin(), key.begin());
                    if (0 != rv) {
                        sError = strprintf("Failed to get keyimage.");
                        return error("%s: %s", __func__, sError);
                    }

                    // Double check key image is not used... todo, this should not be done here and is result of bad state
                    uint256 txhashKI;
                    auto ki = *((CCmpPubKey*)&vKeyImages[k*33]);
                    if (pblocktree->ReadRCTKeyImage(ki, txhashKI)) {
                        AnonWalletDB wdb(*walletDatabase);
                        COutPoint out;
                        bool fErased = false;
                        if (wdb.ReadAnonKeyImage(ki, out)) {
                            MarkOutputSpent(out, true);
                            fErased = true;
                        }
                        sError = strprintf("Bad wallet state trying to spend already spent input, outpoint=%s erased=%s.",
                                            out.ToString(), fErased ? "true" : "false");
                        return error("%s: %s", __func__, sError);
                    }
                }
            }

            for (size_t l = 0; l < txNew.vin.size(); ++l) {
                auto &txin = txNew.vin[l];

                uint32_t nSigInputs, nSigRingSize;
                txin.GetAnonInfo(nSigInputs, nSigRingSize);

                size_t nCols = nSigRingSize;
                size_t nRows = nSigInputs + 1;

                uint8_t randSeed[32];
                GetStrongRandBytes(randSeed, 32);

                std::vector<CKey> vsk(nSigInputs);
                std::vector<const uint8_t*> vpsk(nRows);

                std::vector<uint8_t> vm(nCols * nRows * 33);
                std::vector<secp256k1_pedersen_commitment> vCommitments;
                vCommitments.reserve(nCols * nSigInputs);
                std::vector<const uint8_t*> vpInCommits(nCols * nSigInputs);
                std::vector<const uint8_t*> vpBlinds;

                std::vector<uint8_t> &vKeyImages = txin.scriptData.stack[0];

                for (size_t k = 0; k < nSigInputs; ++k) {
                    for (size_t i = 0; i < nCols; ++i) {
                        int64_t nIndex = vMI[l][k][i];

                        CAnonOutput ao;
                        if (!pblocktree->ReadRCTOutput(nIndex, ao)) {
                            sError = strprintf("Output %d not found in database.", nIndex);
                            return error("%s: %s", __func__, sError);
                        }

                        memcpy(&vm[(i + k * nCols) * 33], ao.pubkey.begin(), 33);
                        vCommitments.push_back(ao.commitment);
                        vpInCommits[i + k * nCols] = vCommitments.back().data;

                        if (i == vSecretColumns[l]) {
                            CKeyID idk = ao.pubkey.GetID();
                            if (!GetKey(idk, vsk[k])) {
                                sError = strprintf("No key for output: %s", HexStr(ao.pubkey.begin(), ao.pubkey.end()));
                                return error("%s: %s", __func__, sError);
                            }
                            vpsk[k] = vsk[k].begin();

                            vpBlinds.push_back(&vInputBlinds[l][k * 32]);
                            /*
                            // Keyimage is required for the tx hash
                            rv = secp256k1_get_keyimage(secp256k1_ctx_blind, &vKeyImages[k * 33], &vm[(i+k*nCols)*33], vpsk[k]);
                            if (0 != rv) {
                                sError = strprintf("Failed to get keyimage.");
                                return error("%s: %s", __func__, sError);
                            }
                            */
                        }
                    }
                }

                uint8_t blindSum[32];
                memset(blindSum, 0, 32);
                vpsk[nRows-1] = blindSum;

                std::vector<uint8_t> &vDL = txin.scriptWitness.stack[1];

                if (txNew.vin.size() == 1) {
                    vDL.resize((1 + (nSigInputs+1) * nSigRingSize) * 32); // extra element for C, extra row for commitment row
                    vpBlinds.insert(vpBlinds.end(), vpOutBlinds.begin(), vpOutBlinds.end());

                    if (0 != (rv = secp256k1_prepare_mlsag(&vm[0], blindSum,
                                   vpOutCommits.size(), vpOutCommits.size(), nCols, nRows,
                                   &vpInCommits[0], &vpOutCommits[0], &vpBlinds[0]))) {
                        sError = strprintf("Failed to prepare mlsag with %d.", rv);
                        return error("%s: %s", __func__, sError);
                    }
                } else {
                    vDL.resize((1 + (nSigInputs+1) * nSigRingSize) * 32 + 33); // extra element for C extra, extra row for commitment row, split input commitment

                    if (l == txNew.vin.size()-1) {
                        std::vector<const uint8_t*> vpAllBlinds = vpOutBlinds;

                        for (size_t k = 0; k < l; ++k) {
                            vpAllBlinds.push_back(vSplitCommitBlindingKeys[k].begin());
                        }

                        if (!secp256k1_pedersen_blind_sum(secp256k1_ctx_blind,
                                                          vSplitCommitBlindingKeys[l].begin_nc(),
                                                          &vpAllBlinds[0], vpAllBlinds.size(),
                                                          vpOutBlinds.size())) {
                            sError = strprintf("Pedersen blind sum failed.");
                            return error("%s: %s", __func__, sError);
                        }
                    } else {
                        vSplitCommitBlindingKeys[l].MakeNewKey(true);
                    }


                    CAmount nCommitValue = 0;
                    for (size_t k = 0; k < nSigInputs; ++k) {
                        const auto &coin = setCoins[nTotalInputs+k];
                        const COutputRecord *oR = coin.first->second.GetOutput(coin.second);
                        nCommitValue += oR->GetAmount();
                    }

                    nTotalInputs += nSigInputs;

                    secp256k1_pedersen_commitment splitInputCommit;
                    if (!secp256k1_pedersen_commit(secp256k1_ctx_blind, &splitInputCommit,
                                                   (uint8_t*)vSplitCommitBlindingKeys[l].begin(),
                                                   nCommitValue, secp256k1_generator_h)) {
                        sError = strprintf("Pedersen commit failed.");
                        return error("%s: %s", __func__, sError);
                    }

                    memcpy(&vDL[(1 + (nSigInputs+1) * nSigRingSize) * 32], splitInputCommit.data, 33);

                    vpBlinds.emplace_back(vSplitCommitBlindingKeys[l].begin());
                    const uint8_t *pSplitCommit = splitInputCommit.data;
                    if (0 != (rv = secp256k1_prepare_mlsag(&vm[0], blindSum, 1, 1, nCols, nRows,
                                                           &vpInCommits[0], &pSplitCommit, &vpBlinds[0]))) {
                        sError = strprintf("Failed to prepare mlsag with %d.", rv);
                        return error("%s: %s", __func__, sError);
                    }

                    vpBlinds.pop_back();
                };

                uint256 hashOutputs = txNew.GetOutputsHash();
                if (0 != (rv = secp256k1_generate_mlsag(secp256k1_ctx_blind, &vKeyImages[0], &vDL[0], &vDL[32],
                                                        randSeed, hashOutputs.begin(), nCols, nRows, vSecretColumns[l],
                                                        &vpsk[0], &vm[0]))) {
                    sError = strprintf("Failed to generate mlsag with %d.", rv);
                    return error("%s: %s", __func__, sError);
                }

                // Validate the mlsag
                if (0 != (rv = secp256k1_verify_mlsag(secp256k1_ctx_blind, hashOutputs.begin(), nCols,
                                                      nRows, &vm[0], &vKeyImages[0], &vDL[0], &vDL[32]))) {
                    sError = strprintf("Failed to generate mlsag on initial generation %d.", rv);
                    return error("%s: %s", __func__, sError);
                }
            }
        }

        rtx.nFee = nFeeRet;
        rtx.nFlags |= ORF_ANON_IN;
        AddOutputRecordMetaData(rtx, vecSend);

        for (auto txin : txNew.vin)
            rtx.vin.emplace_back(txin.prevout);
        MarkInputsAsPendingSpend(rtx);

        uint256 txid = txNew.GetHash();
        if (nValueOutZerocoin)
            rtx.AddPartialTxid(txid);
        //save record
        SaveRecord(txid, rtx);

        // Embed the constructed transaction data in wtxNew.
        wtx.SetTx(MakeTransactionRef(std::move(txNew)));

    } // cs_main, pwalletParent->cs_wallet

    if (!PreAcceptMempoolTx(wtx, sError)) {
        return false;
    }

    LogPrintf("Fee Calculation: Fee:%d Bytes:%u Needed:%d Tgt:%d (requested %d) Reason:\"%s\" Decay %.5f: Estimation: (%g - %g) %.2f%% %.1f/(%.1f %d mem %.1f out) Fail: (%g - %g) %.2f%% %.1f/(%.1f %d mem %.1f out)\n",
              nFeeRet, nBytes, nFeeNeeded, feeCalc.returnedTarget, feeCalc.desiredTarget, StringForFeeReason(feeCalc.reason), feeCalc.est.decay,
              feeCalc.est.pass.start, feeCalc.est.pass.end,
              100 * feeCalc.est.pass.withinTarget / (feeCalc.est.pass.totalConfirmed + feeCalc.est.pass.inMempool + feeCalc.est.pass.leftMempool),
              feeCalc.est.pass.withinTarget, feeCalc.est.pass.totalConfirmed, feeCalc.est.pass.inMempool, feeCalc.est.pass.leftMempool,
              feeCalc.est.fail.start, feeCalc.est.fail.end,
              100 * feeCalc.est.fail.withinTarget / (feeCalc.est.fail.totalConfirmed + feeCalc.est.fail.inMempool + feeCalc.est.fail.leftMempool),
              feeCalc.est.fail.withinTarget, feeCalc.est.fail.totalConfirmed, feeCalc.est.fail.inMempool, feeCalc.est.fail.leftMempool);
    return true;
}

bool AnonWallet::AddAnonInputs(CWalletTx &wtx, CTransactionRecord &rtx, std::vector<CTempRecipient> &vecSend, bool sign,
        size_t nRingSize, size_t nSigs, CAmount &nFeeRet, const CCoinControl *coinControl, std::string &sError, bool fZerocoinInputs,
        CAmount nInputValue)
{
    if (vecSend.size() < 1) {
        sError = strprintf("Transaction must have at least one recipient.");
        return error("%s: %s", __func__, sError);
    }

    CAmount nValue = 0;
    for (const auto &r : vecSend) {
        nValue += r.nAmount;
        if (nValue < 0 || r.nAmount < 0) {
            sError = strprintf("Transaction amounts must not be negative.");
            return error("%s: %s", __func__, sError);
        }
    }

    if (!AddAnonInputs_Inner(wtx, rtx, vecSend, sign, nRingSize, nSigs, nFeeRet, coinControl, sError, fZerocoinInputs, nInputValue)) {
        return false;
    }

    return true;
}

bool AnonWallet::CreateStealthChangeAccount(AnonWalletDB* wdb)
{
    /** Derive a stealth address that is specifically for change **/
    BIP32Path vPathStealthChange;
    vPathStealthChange.emplace_back(3, true);
    CExtKey keyStealthChange = DeriveKeyFromPath(*pkeyMaster, vPathStealthChange);
    idChangeAccount = keyStealthChange.key.GetPubKey().GetID();
    mapKeyPaths.emplace(idChangeAccount, std::make_pair(idMaster, vPathStealthChange));
    mapAccountCounter.emplace(idChangeAccount, 1);
    if (!wdb->WriteExtKey(idMaster, idChangeAccount, vPathStealthChange))
        return false;
    if (!wdb->WriteAccountCounter(idChangeAccount, (uint32_t)1))
        return false;
    if (!wdb->WriteNamedExtKeyId("stealthchange", idChangeAccount))
        return false;

    //Make the change address
    GetStealthChangeAddress();

    if (!wdb->WriteNamedExtKeyId("stealthchangeaddress", idChangeAddress))
        return false;

    return true;
}

CStealthAddress AnonWallet::GetStealthChangeAddress()
{
    if (!mapStealthAddresses.count(idChangeAddress)) {
        //Make sure idChangeAccount is loaded
        if (!mapAccountCounter.count(idChangeAddress))
            mapAccountCounter.emplace(idChangeAddress, 0);

        CStealthAddress address;
        NewStealthKey(address, 0 , nullptr, &idChangeAccount);
        idChangeAddress = address.GetID();
    }
    return mapStealthAddresses.at(idChangeAddress);
}

bool AnonWallet::MakeDefaultAccount(const CExtKey& extKeyMaster)
{
    LogPrintf("%s: Generating new default account.\n", __func__);
    LOCK(pwalletParent->cs_wallet);
    AnonWalletDB wdb(*walletDatabase, "r+");
    if (!wdb.TxnBegin())
        return error("%s: TxnBegin failed.");

    if (!SetMasterKey(extKeyMaster))
        return error("%s: failed to set master key", __func__);

    /** Derive the default account**/
    BIP32Path vPathDefaultAccount;
    vPathDefaultAccount.emplace_back(1, true);
    CExtKey keyDefaultAccount = DeriveKeyFromPath(*pkeyMaster, vPathDefaultAccount);

    //Add default account to anon wallet state, start at 1
    idDefaultAccount = keyDefaultAccount.key.GetPubKey().GetID();
    mapAccountCounter.emplace(idDefaultAccount, 1);

    mapKeyPaths.emplace(idDefaultAccount, std::make_pair(idMaster, vPathDefaultAccount));
    wdb.WriteExtKey(idMaster, idDefaultAccount, vPathDefaultAccount);
    wdb.WriteAccountCounter(idDefaultAccount, (uint32_t)1);
    wdb.WriteNamedExtKeyId("defaultaccount", idDefaultAccount);

    /** Derive the stealth account**/
    BIP32Path vPathStealthAccount;
    vPathStealthAccount.emplace_back(2, true);
    CExtKey keyStealthAccount = DeriveKeyFromPath(*pkeyMaster, vPathStealthAccount);
    //Add stealth account to anon wallet state, start at 1
    idStealthAccount = keyStealthAccount.key.GetPubKey().GetID();
    mapAccountCounter.emplace(idStealthAccount, 1);

    mapKeyPaths.emplace(idStealthAccount, std::make_pair(idMaster, vPathStealthAccount));
    wdb.WriteExtKey(idMaster, idStealthAccount, vPathStealthAccount);
    wdb.WriteAccountCounter(idStealthAccount, (uint32_t)1);
    wdb.WriteNamedExtKeyId("stealthaccount", idStealthAccount);

    if (!wdb.TxnCommit())
        return error("%s: TxnCommit failed.");

    /** Derive a stealth address that is specifically for change **/
    CreateStealthChangeAccount(&wdb);

    LogPrintf("%s: Default account %s\n", __func__, idDefaultAccount.GetHex());
    LogPrintf("%s: Stealth account %s\n", __func__, idStealthAccount.GetHex());
    LogPrintf("%s: Stealth Change account %s\n", __func__, idChangeAccount.GetHex());
    LogPrintf("%s: Master account %s\n", __func__, idMaster.GetHex());

    return true;
}

bool AnonWallet::UnlockWallet(const CExtKey& keyMasterIn)
{
    if (!SetMasterKey(keyMasterIn))
        return false;

    // Create stealth change account if it does not exist
    AnonWalletDB wdb(*walletDatabase);
    if (!wdb.ReadNamedExtKeyId("stealthchange", idChangeAccount)) {
        //If the change account is not created yet, then create it
        if (!CreateStealthChangeAccount(&wdb))
            return error("%s: failed to create stealth change account ", __func__);
    }

    return true;
}

bool AnonWallet::SetMasterKey(const CExtKey& keyMasterIn)
{
    std::unique_ptr<CExtKey> ptr(new CExtKey());
    pkeyMaster = std::move(ptr);
    *pkeyMaster = keyMasterIn;
    idMaster = pkeyMaster->key.GetPubKey().GetID();

    AnonWalletDB wdb(*walletDatabase);
    return wdb.WriteNamedExtKeyId("master", idMaster);
}

CKeyID AnonWallet::GetSeedHash() const
{
    return idMaster;
}

bool AnonWallet::LoadKeys()
{
    LOCK(pwalletParent->cs_wallet);

    AnonWalletDB wdb(*walletDatabase);

    Dbc *pcursor;
    if (!(pcursor = wdb.GetCursor()))
        return error("%s: cannot create DB cursor", __func__);

    CDataStream ssKey(SER_DISK, CLIENT_VERSION);
    CDataStream ssValue(SER_DISK, CLIENT_VERSION);

    unsigned int fFlags = DB_SET_RANGE;
    ssKey << std::string("ek32_n");
    while (wdb.ReadAtCursor(pcursor, ssKey, ssValue, fFlags) == 0) {
        fFlags = DB_NEXT;

        std::string strType;
        ssKey >> strType;
        if (strType != "ek32_n") {
            break;
        }

        CKeyID idAccount;
        ssKey >> idAccount;

        std::pair<CKeyID, BIP32Path> pLocation;
        ssValue >> pLocation;

        if (!mapAccountCounter.count(pLocation.first) && pLocation.first != idMaster)
            return error("%s: account %s has no counter", __func__, pLocation.first.GetHex());
        mapKeyPaths.emplace(idAccount, pLocation);
    }

    pcursor->close();

    return true;
}

bool AnonWallet::LoadAccountCounters()
{
    LOCK(pwalletParent->cs_wallet);

    AnonWalletDB wdb(*walletDatabase);

    Dbc *pcursor;
    if (!(pcursor = wdb.GetCursor()))
        return error("%s: cannot create DB cursor", __func__);

    CDataStream ssKey(SER_DISK, CLIENT_VERSION);
    CDataStream ssValue(SER_DISK, CLIENT_VERSION);

    unsigned int fFlags = DB_SET_RANGE;
    ssKey << std::string("acct_c");
    while (wdb.ReadAtCursor(pcursor, ssKey, ssValue, fFlags) == 0) {
        fFlags = DB_NEXT;

        std::string strType;
        ssKey >> strType;
        if (strType != "acct_c") {
            break;
        }

        CKeyID idAccount;
        ssKey >> idAccount;

        uint32_t nCount;
        ssValue >> nCount;

        mapAccountCounter.emplace(idAccount, nCount);
    }

    pcursor->close();

    return true;
}

int AnonWallet::GetStealthAccountCount() const
{
    if (!mapAccountCounter.count(idStealthAccount))
        return 0;
    return mapAccountCounter.at(idStealthAccount);
}

bool AnonWallet::RegenerateKey(const CKeyID& idKey, CKey& key) const
{
    CExtKey keyTemp;
    if (!RegenerateExtKey(idKey, keyTemp))
        return false;
    key = keyTemp.key;
    return true;
}

bool AnonWallet::RegenerateKeyFromIndex(const CKeyID& idAccount, int nIndex, CExtKey& keyDerive) const
{
    CExtKey keyAccount;
    if (!RegenerateAccountExtKey(idAccount, keyAccount))
        return error("%s: failed to regenerate account key", __func__);

    BIP32Path vPathNew;
    vPathNew.emplace_back(nIndex, true);
    keyDerive = DeriveKeyFromPath(keyAccount, vPathNew);

    return true;
}

bool AnonWallet::RegenerateAccountExtKey(const CKeyID& idAccount, CExtKey& keyAccount) const
{
    if (IsLocked() || pwalletParent->IsUnlockedForStakingOnly())
        return error("%s Wallet must be unlocked to derive hardened keys.", __func__);

    if (!mapKeyPaths.count(idAccount))
        return error("%s: cannot locate keyid %s", __func__, idAccount.GetHex());

    CKeyID idAccountParent = mapKeyPaths.at(idAccount).first;
    bool fMasterAccount = idAccountParent == idMaster;
    if (!mapKeyPaths.count(idAccountParent) && !fMasterAccount)
        return error("%s: Cannot find keypath for account %s", __func__, idAccountParent.GetHex());
    BIP32Path vPathAccount = mapKeyPaths.at(idAccount).second;

    if (!fMasterAccount) {
        CExtKey keyParent;
        if (!RegenerateAccountExtKey(idAccountParent, keyParent))
            return error("%s: FIXME, could not regenerate account key", __func__);
        keyAccount = DeriveKeyFromPath(keyParent, vPathAccount);
    } else {
        keyAccount = DeriveKeyFromPath(*pkeyMaster, vPathAccount);
    }
    return keyAccount.key.GetPubKey().GetID() == idAccount;
}

bool AnonWallet::RegenerateExtKey(const CKeyID& idKey, CExtKey& extkey) const
{
    if (IsLocked())
        return error("%s Wallet must be unlocked to derive hardened keys.", __func__);

    if (!mapKeyPaths.count(idKey))
        return error("%s: cannot locate keyid %s", __func__, idKey.GetHex());

    CKeyID idAccountFrom = mapKeyPaths.at(idKey).first;
    if (idAccountFrom == idKey)
        return error("%s: keyid:%s matches accountid:%s", __func__, idKey.GetHex(), idAccountFrom.GetHex());

    CExtKey keyAccount;
    if (!RegenerateAccountExtKey(idAccountFrom, keyAccount))
        return error("%s: failed to regenerate account %s", __func__, idAccountFrom.GetHex());

    // Regenerate child key
    BIP32Path vChildPath = mapKeyPaths.at(idKey).second;
    extkey = DeriveKeyFromPath(keyAccount, vChildPath);
    return idKey == extkey.key.GetPubKey().GetID();
}

bool AnonWallet::NewKeyFromAccount(const CKeyID &idAccount, CKey& key)
{
    CExtKey keyTemp;
    return NewExtKeyFromAccount(idAccount, keyTemp, key);
}

bool AnonWallet::NewExtKeyFromAccount(const CKeyID& idAccount, CExtKey& keyDerive, CKey& key)
{
    if (IsLocked())
        return error("%s Wallet must be unlocked to derive hardened keys.", __func__);

    // Double check the account is being tracked
    if (!mapAccountCounter.count(idAccount))
        return error("%s Account id not in map of account counters.", __func__);

    CExtKey keyAccount;
    if (!RegenerateAccountExtKey(idAccount, keyAccount))
        return error("%s: failed to regenerate account ext key. ID=%s", __func__, idAccount.GetHex());

    BIP32Path vPathNew;
    auto count = mapAccountCounter.at(idAccount)++;
    vPathNew.emplace_back(count, true);
    keyDerive = DeriveKeyFromPath(keyAccount, vPathNew);

    LOCK(pwalletParent->cs_wallet);
    AnonWalletDB wdb(*walletDatabase, "r+");
    key = keyDerive.key;
    CKeyID idNew = key.GetPubKey().GetID();

    //Record the new key meta data to disk
    mapKeyPaths.emplace(idNew, std::make_pair(idAccount, vPathNew));

    //Flush the new key meta data to disk
    if (!wdb.WriteExtKey(idAccount, idNew, vPathNew))
        return error("%s Failed to write new key to disk", __func__);
    if (!wdb.WriteAccountCounter(idAccount, count))
        return error("%s Failed to write account counter to disk", __func__);

    return true;
}

bool AnonWallet::CreateAccountWithKey(const CExtKey& key)
{
    CKeyID idAccount = key.key.GetPubKey().GetID();
    if (!mapKeyPaths.count(idAccount))
        return error("%s: keyid is not in mapkeypaths but should be before creating an account", __func__);
    mapAccountCounter.emplace(idAccount, 1);

    LOCK(pwalletParent->cs_wallet);
    AnonWalletDB wdb(*walletDatabase, "r+");
    wdb.WriteAccountCounter(idAccount, (uint32_t)1);

    return true;
}

bool AnonWallet::NewStealthKey(CStealthAddress& stealthAddress, uint32_t nPrefixBits,
        const char *pPrefix, CKeyID* paccount)
{
    // Scan secrets must be stored uncrypted - always derive hardened keys
    if (IsLocked())
        return error("%s Wallet must be unlocked to derive hardened keys.", __func__);

    // Generate a new account that is only used to generate the two keys for this stealth address
    CExtKey keyStealthAddress;
    CKey key;
    CKeyID idAccount = idStealthAccount;
    if (paccount)
        idAccount = *paccount;
    if (!NewExtKeyFromAccount(idAccount, keyStealthAddress, key))
        return error("%s: failed to generate stealth account key", __func__);
    if (!CreateAccountWithKey(keyStealthAddress))
        return error("%s: failed to create account for stealthkey", __func__);

    CKeyID idKey = keyStealthAddress.key.GetPubKey().GetID();
    CKey keyScan, keySpend;
    if (!NewKeyFromAccount(idKey, keyScan))
        return error("%s: failed to generate key1", __func__);
    if (!NewKeyFromAccount(idKey, keySpend))
        return error("%s: failed to generate key2", __func__);

    uint32_t nPrefix = 0;
    if (pPrefix) {
        if (!ExtractStealthPrefix(pPrefix, nPrefix))
            return error("%s ExtractStealthPrefix.", __func__);
    } else if (nPrefixBits > 0) {
        // If pPrefix is null, set nPrefix from the hash of kSpend
        uint8_t tmp32[32];
        CSHA256().Write(keySpend.begin(), 32).Finalize(tmp32);
        memcpy(&nPrefix, tmp32, 4);
    }

    uint32_t nMask = SetStealthMask(nPrefixBits);
    nPrefix = nPrefix & nMask;
    CPubKey pkSpend = keySpend.GetPubKey();

    // Make a stealth address
    stealthAddress.SetNull();
    stealthAddress.prefix.number_bits = nPrefixBits;
    stealthAddress.prefix.bitfield = nPrefix;
    stealthAddress.scan_pubkey = keyScan.GetPubKey().Raw();
    stealthAddress.spend_pubkey = pkSpend.Raw();
    stealthAddress.scan_secret.Set(keyScan.begin(), true);
    stealthAddress.spend_secret_id = pkSpend.GetID();

    //Record stealth address to db
    if (!AnonWalletDB(*walletDatabase).WriteStealthAddress(stealthAddress))
        return error("%s: failed to write stealth address to db", __func__);
    mapStealthAddresses.emplace(stealthAddress.GetID(), stealthAddress);

    return true;
}

bool AnonWallet::RestoreAddresses(int nCount)
{
    for (int i = 0; i < nCount; i++) {
        CStealthAddress stealthAddress;
        if (!NewStealthKey(stealthAddress, 0, nullptr))
            return false;
    }
    return true;
}

int AnonWallet::LoadStealthAddresses()
{
    LOCK(pwalletParent->cs_wallet);

    AnonWalletDB wdb(*walletDatabase);

    Dbc *pcursor;
    if (!(pcursor = wdb.GetCursor())) {
        return werrorN(1, "%s: cannot create DB cursor", __func__);
    }

    CDataStream ssKey(SER_DISK, CLIENT_VERSION);
    CDataStream ssValue(SER_DISK, CLIENT_VERSION);

    CBitcoinAddress addr;
    std::string strType;

    unsigned int fFlags = DB_SET_RANGE;
    ssKey << std::string("sxad");
    while (wdb.ReadAtCursor(pcursor, ssKey, ssValue, fFlags) == 0) {
        fFlags = DB_NEXT;

        ssKey >> strType;
        if (strType != "sxad") {
            break;
        }

        //Stealth Address
        CStealthAddress stealthAddress;
        ssValue >> stealthAddress;
        auto idStealth = stealthAddress.GetID();
        mapStealthAddresses.emplace(idStealth, stealthAddress);

        //If the stealth address has stealth destinations load them too
        if (stealthAddress.setStealthDestinations.empty())
            continue;
        for (auto stealthDest : stealthAddress.setStealthDestinations)
            mapStealthDestinations.emplace(stealthDest, idStealth);
    }
    pcursor->close();

    return true;
}

bool AnonWallet::GetStealthLinked(const CKeyID &stealthDest, CStealthAddress &sx) const
{
    LOCK(pwalletParent->cs_wallet);

    //Match an already indexed stealth destination to its source stealth address
    if (!mapStealthDestinations.count(stealthDest))
        return false;

    auto idStealth = mapStealthDestinations.at(stealthDest);
    if (!mapStealthAddresses.count(idStealth))
        return false;

    sx = mapStealthAddresses.at(idStealth);

    return true;
}

bool AnonWallet::GetStealthAddress(const CKeyID& idStealth, CStealthAddress& stealthAddress)
{
    if (!mapStealthAddresses.count(idStealth))
        return false;
    stealthAddress = mapStealthAddresses.at(idStealth);
    return true;
}

bool AnonWallet::ProcessLockedStealthOutputs()
{
    LOCK(pwalletParent->cs_wallet);

    AnonWalletDB wdb(*walletDatabase);

    if (!wdb.TxnBegin()) {
        return werror("%s: TxnBegin failed.", __func__);
    }

    Dbc *pcursor;
    if (!(pcursor = wdb.GetTxnCursor())) {
        return werror("%s: Cannot create DB cursor.", __func__);
    }

    CDataStream ssKey(SER_DISK, CLIENT_VERSION);
    CDataStream ssValue(SER_DISK, CLIENT_VERSION);

    CPubKey pk;

    std::string strType;
    CKeyID idk;
    CStealthKeyMetadata sxKeyMeta;

    size_t nProcessed = 0; // incl any failed attempts
    size_t nExpanded = 0;
    unsigned int fFlags = DB_SET_RANGE;
    ssKey << std::string("sxkm");
    while (wdb.ReadAtCursor(pcursor, ssKey, ssValue, fFlags) == 0) {
        fFlags = DB_NEXT;
        ssKey >> strType;
        if (strType != "sxkm") {
            break;
        }

        nProcessed++;

        ssKey >> idk;
        ssValue >> sxKeyMeta;

        if (!GetPubKey(idk, pk)) {
            LogPrintf("%s Error: GetPubKey failed %s.\n", __func__, CBitcoinAddress(idk).ToString());
            continue;
        }

        CStealthAddress sxFind;
        sxFind.SetScanPubKey(sxKeyMeta.pkScan);

        CKeyID idScan = sxKeyMeta.pkScan.GetID();
        if (!mapStealthAddresses.count(idScan)) {
            LogPrintf("%s Error: No stealth key found to add secret for %s.\n", __func__, CBitcoinAddress(idk).ToString());
            continue;
        }

        CStealthAddress* si = &mapStealthAddresses.at(idScan);

        CKey sSpendR, sSpend;

        if (!RegenerateKey(si->GetSpendKeyID(), sSpend)) {
            LogPrintf("%s Error: Stealth address has no spend_secret_id key for %s\n", __func__, CBitcoinAddress(idk).ToString());
            continue;
        }

        if (si->scan_secret.size() != EC_SECRET_SIZE) {
            LogPrintf("%s Error: Stealth address has no scan_secret key for %s\n", __func__, CBitcoinAddress(idk).ToString());
            continue;
        }

        if (sxKeyMeta.pkEphem.size() != EC_COMPRESSED_SIZE) {
            LogPrintf("%s Error: Incorrect Ephemeral point size (%d) for %s\n", __func__, sxKeyMeta.pkEphem.size(), CBitcoinAddress(idk).ToString());
            continue;
        }

        ec_point pkEphem;;
        pkEphem.resize(EC_COMPRESSED_SIZE);
        memcpy(&pkEphem[0], sxKeyMeta.pkEphem.begin(), sxKeyMeta.pkEphem.size());

        if (StealthSecretSpend(si->scan_secret, pkEphem, sSpend, sSpendR) != 0) {
            LogPrintf("%s Error: StealthSecretSpend() failed for %s\n", __func__, CBitcoinAddress(idk).ToString());
            continue;
        }

        if (!sSpendR.IsValid()) {
            LogPrintf("%s Error: Reconstructed key is invalid for %s\n", __func__, CBitcoinAddress(idk).ToString());
            continue;
        }

        CPubKey cpkT = sSpendR.GetPubKey();
        if (cpkT != pk) {
            LogPrintf("%s: Error: Generated secret does not match.\n", __func__);
            continue;
        }

        if (!pwalletParent->AddKeyPubKey(sSpendR, pk)) {
            LogPrintf("%s: Error: AddKeyPubKey failed.\n", __func__);
            continue;
        }

        nExpanded++;

        int rv = pcursor->del(0);
        if (rv != 0) {
            LogPrintf("%s: Error: EraseStealthKeyMeta failed for %s, %d\n", __func__, CBitcoinAddress(idk).ToString(), rv);
        }
    }

    pcursor->close();

    wdb.TxnCommit();

    return true;
}

bool AnonWallet::ProcessLockedBlindedOutputs()
{
    AssertLockHeld(pwalletParent->cs_wallet);

    size_t nProcessed = 0; // incl any failed attempts
    size_t nExpanded = 0;
    std::set<uint256> setChanged;

    {
    AnonWalletDB wdb(*walletDatabase);

    if (!wdb.TxnBegin()) {
        return werror("%s: TxnBegin failed.", __func__);
    }

    Dbc *pcursor;
    if (!(pcursor = wdb.GetTxnCursor())) {
        return werror("%s: Cannot create DB cursor.", __func__);
    }

    CDataStream ssKey(SER_DISK, CLIENT_VERSION);

    COutPoint op;
    std::string strType;

    CStoredTransaction stx;
    unsigned int fFlags = DB_SET_RANGE;
    ssKey << std::string("lao");
    while (wdb.ReadKeyAtCursor(pcursor, ssKey, fFlags) == 0) {
        fFlags = DB_NEXT;
        ssKey >> strType;
        if (strType != "lao") {
            break;
        }

        nProcessed++;

        ssKey >> op;

        int rv = pcursor->del(0);
        if (rv != 0) {
            LogPrintf("%s: Error: pcursor->del failed for %s, %d.\n", __func__, op.ToString(), rv);
        }

        MapRecords_t::iterator mir;

        mir = mapRecords.find(op.hash);
        if (mir == mapRecords.end()
            || !wdb.ReadStoredTx(op.hash, stx)) {
            LogPrintf("%s: Error: mapRecord not found for %s.\n", __func__, op.ToString());
            continue;
        }
        CTransactionRecord &rtx = mir->second;

        if (stx.tx->vpout.size() < op.n) {
            LogPrintf("%s: Error: Outpoint doesn't exist %s.\n", __func__, op.ToString());
            continue;
        }

        const auto &txout = stx.tx->vpout[op.n];

        COutputRecord rout;
        COutputRecord *pout = rtx.GetOutput(op.n);

        bool fHave = false;
        if (pout) { // Have output recorded already, still need to check if owned
            fHave = true;
        } else {
            pout = &rout;
        }

        bool fUpdated = false;
        pout->n = op.n;
        switch (txout->nVersion) {
            case OUTPUT_CT:
                if (OwnBlindOut(&wdb, op.hash, (CTxOutCT*)txout.get(), *pout, stx, fUpdated)
                    && !fHave) {
                    fUpdated = true;
                    rtx.InsertOutput(*pout);
                }
                break;
            case OUTPUT_RINGCT:
                if (OwnAnonOut(&wdb, op.hash, (CTxOutRingCT*)txout.get(), *pout, stx, fUpdated)
                    && !fHave) {
                    fUpdated = true;
                    rtx.InsertOutput(*pout);
                }
                break;
            default:
                LogPrintf("%s: Error: Output is unexpected type %d %s.\n", __func__, txout->nVersion, op.ToString());
                continue;
        }

        if (fUpdated) {
            // If txn has change, it must have been sent by this wallet
            if (rtx.HaveChange()) {
                ProcessPlaceholder(&wdb, *stx.tx.get(), rtx);
            }

            if (!wdb.WriteTxRecord(op.hash, rtx)
                || !wdb.WriteStoredTx(op.hash, stx)) {
                return false;
            }

            setChanged.insert(op.hash);
        }

        nExpanded++;
    }

    pcursor->close();

    wdb.TxnCommit();
    }
    //todo:
//    // Notify UI of updated transaction
//    for (const auto &hash : setChanged) {
//        NotifyTransactionChanged(this, hash, CT_REPLACE);
//    }

    return true;
}

inline bool MatchPrefix(uint32_t nAddrBits, uint32_t addrPrefix, uint32_t outputPrefix, bool fHavePrefix)
{
    if (nAddrBits < 1) { // addresses without prefixes scan all incoming stealth outputs
        return true;
    }
    if (!fHavePrefix) { // don't check when address has a prefix and no prefix on output
        return false;
    }

    uint32_t mask = SetStealthMask(nAddrBits);

    return (addrPrefix & mask) == (outputPrefix & mask);
}

//Veil
bool AnonWallet::AddStealthDestination(const CKeyID& idStealthAddress, const CKeyID& idStealthDestination)
{
    if (idStealthAddress == CKeyID())
        return error("%s: Trying to save a null key id", __func__);

    if (!mapStealthAddresses.count(idStealthAddress))
        return error("%s: Do not have stealth address with hash %s\n", __func__, idStealthAddress.GetHex());

    auto* address = &mapStealthAddresses.at(idStealthAddress);
    address->AddStealthDestination(idStealthDestination);
    mapStealthDestinations.emplace(idStealthDestination, idStealthAddress);

    //Update database
    AnonWalletDB db(*walletDatabase);
    if (!db.WriteStealthAddress(*address))
        return error("%s: failed writing stealth address to database", __func__);
    return true;
}

bool AnonWallet::AddStealthDestinationMeta(const CKeyID& idStealth, const CKeyID& idStealthDestination, std::vector<uint8_t> &vchEphemPK)
{
    AnonWalletDB db(*walletDatabase);
    return db.WriteStealthDestinationMeta(idStealthDestination, vchEphemPK);
}

bool AnonWallet::AddKeyToParent(const CKey& keySharedSecret)
{
    LOCK(pwalletParent->cs_wallet);
    //Since the new key is not derived through Ext, for now keep it in CWallet keystore
    bool fSuccess = true;
    if (!pwalletParent->AddKeyPubKey(keySharedSecret, keySharedSecret.GetPubKey())) {
        fSuccess = pwalletParent->HaveKey(keySharedSecret.GetPubKey().GetID());
    }
    return fSuccess;
}

bool AnonWallet::ProcessStealthOutput(const CTxDestination &address, std::vector<uint8_t> &vchEphemPK, uint32_t prefix,
        bool fHavePrefix, CKey &sShared, bool fNeedShared)
{
    LOCK(pwalletParent->cs_wallet);
    ec_point pkExtracted;
    CKey keySpend;

    CKeyID idStealthDestination = boost::get<CKeyID>(address);
    if (mapStealthDestinations.count(idStealthDestination)) {
        CStealthAddress sx;
        if (fNeedShared && GetStealthLinked(idStealthDestination, sx) && GetStealthAddressScanKey(sx)) {
            if (StealthShared(sx.scan_secret, vchEphemPK, sShared) != 0) {
                LogPrintf("%s: StealthShared failed.\n", __func__);
                //continue;
            }
        }

        return true;
    }

    // Iterate through owned stealth addresses to see if this was sent to one of them (note: the address sent to is
    // extracted from the stealth address in a deterministic way, so the owned addresses are calculate the changes to
    // see if there is a match, if so the key belongs to us
    for (auto mi = mapStealthAddresses.begin(); mi != mapStealthAddresses.end(); ++mi) {
        auto* addr = &mi->second;
        if (!MatchPrefix(addr->prefix.number_bits, addr->prefix.bitfield, prefix, fHavePrefix)) {
            continue;
        }

        if (!addr->scan_secret.IsValid()) {
            continue; // stealth address is not owned
        }

        if (StealthSecret(addr->scan_secret, vchEphemPK, addr->spend_pubkey, sShared, pkExtracted) != 0) {
            LogPrintf("%s: StealthSecret failed.\n", __func__);
            continue;
        }

        CPubKey pubKeyStealthSecret(pkExtracted);
        if (!pubKeyStealthSecret.IsValid()) {
            continue;
        }

        CKeyID idExtracted = pubKeyStealthSecret.GetID();
        if (idStealthDestination != idExtracted) {
            continue;
        }

        // Found a matching stealth address that is owned by this wallet, record a link to the stealth destination
        assert(AddStealthDestination(addr->GetID(), idStealthDestination));

        if (IsLocked()) {
            // Save ephemeral pubkey
            if (!AddStealthDestinationMeta(addr->GetID(), idStealthDestination, vchEphemPK))
                return error("%s: Failed to add metadata for stealth destination", __func__);

            nFoundStealth++;
            return true;
        }

        if (!RegenerateKey(addr->GetSpendKeyID(), keySpend)) {
            LogPrintf("%s: Failed for keyid %d\n", __func__, addr->GetSpendKeyID().GetHex());
            continue;
        }

        CKey keySharedSecret;
        if (StealthSharedToSecretSpend(sShared, keySpend, keySharedSecret) != 0) {
            LogPrintf("%s: StealthSharedToSecretSpend() failed.\n", __func__);
            continue;
        }

        //Verify pubkey regenerated matches
        CPubKey pkT = keySharedSecret.GetPubKey();
        if (!pkT.IsValid()) {
            LogPrintf("%s: pkT is invalid.\n", __func__);
            continue;
        }
        CKeyID keyID = pkT.GetID();
        if (keyID != idStealthDestination) {
            LogPrintf("%s: Spend key mismatch!\n", __func__);
            continue;
        }

        //Since the new key is not derived through Ext, for now keep it in CWallet keystore
        bool fSuccess = true;
        if (!pwalletParent->AddKeyPubKey(keySharedSecret, pkT)) {
            fSuccess = pwalletParent->HaveKey(pkT.GetID());
        }

        nFoundStealth++;
        return fSuccess;
    }

    return false;
}

int AnonWallet::CheckForStealthAndNarration(const CTxOutBase *pb, const CTxOutData *pdata, std::string &sNarr)
{
    // returns: -1 error, 0 nothing found, 1 narration, 2 stealth

    CKey sShared;
    std::vector<uint8_t> vchEphemPK, vchENarr;
    const std::vector<uint8_t> &vData = pdata->vData;

    if (vData.size() < 1) {
        return -1;
    }

    if (vData[0] == DO_NARR_PLAIN) {
        if (vData.size() < 2) {
            return -1; // error
        }

        sNarr = std::string(vData.begin()+1, vData.end());
        return 1;
    }

    if (vData[0] == DO_STEALTH) {
        if (vData.size() < 34
            || !pb->IsStandardOutput()) {
            return -1; // error
        }

        vchEphemPK.resize(33);
        memcpy(&vchEphemPK[0], &vData[1], 33);

        uint32_t prefix = 0;
        bool fHavePrefix = false;

        if (vData.size() >= 34 + 5
            && vData[34] == DO_STEALTH_PREFIX) {
            fHavePrefix = true;
            memcpy(&prefix, &vData[35], 4);
        }

        const CTxOutStandard *so = (CTxOutStandard*)pb;
        CTxDestination address;
        if (!ExtractDestination(so->scriptPubKey, address)
            || address.type() != typeid(CKeyID)) {
            LogPrintf("%s: ExtractDestination failed.\n",  __func__);
            return -1;
        }

        if (!ProcessStealthOutput(address, vchEphemPK, prefix, fHavePrefix, sShared, true)) {
            // TODO: check all other outputs?
            return 0;
        }

        return 2;
    }

    LogPrintf("%s: Unknown data output type %d.\n",  __func__, vData[0]);
    return -1;
}

bool AnonWallet::FindStealthTransactions(const CTransaction &tx, mapValue_t &mapNarr)
{
    mapNarr.clear();

    LOCK(pwalletParent->cs_wallet);

    // A data output always applies to the preceding output
    int32_t nOutputId = -1;
    for (const auto &txout : tx.vpout) {
        nOutputId++;
        if (txout->nVersion != OUTPUT_DATA) {
            continue;
        }

        CTxOutData *txd = (CTxOutData*) txout.get();

        if (nOutputId < 1) {
            continue;
        }

        std::string sNarr;
        if (CheckForStealthAndNarration(tx.vpout[nOutputId-1].get(), txd, sNarr) < 0) {
            LogPrintf("%s: txn %s, malformed data output %d.\n",  __func__, tx.GetHash().ToString(), nOutputId);
        }

        if (sNarr.length() > 0) {
            std::string sKey = strprintf("n%d", nOutputId-1);
            mapNarr[sKey] = sNarr;
        }
    }

    return true;
}

bool AnonWallet::ScanForOwnedOutputs(const CTransaction &tx, size_t &nCT, size_t &nRingCT, mapValue_t &mapNarr)
{
    AssertLockHeld(pwalletParent->cs_wallet);

    bool fIsMine = false;
    mapNarr.clear();

    int32_t nOutputId = -1;
    for (const auto &txout : tx.vpout) {
        nOutputId++;
        if (txout->IsType(OUTPUT_CT)) {
            nCT++;
            const CTxOutCT *ctout = (CTxOutCT*) txout.get();

            CTxDestination address;
            if (!ExtractDestination(ctout->scriptPubKey, address)
                || address.type() != typeid(CKeyID)) {
                LogPrintf("%s: ExtractDestination failed.\n", __func__);
                continue;
            }

            // Uncover stealth
            uint32_t prefix = 0;
            bool fHavePrefix = false;
            if (ctout->vData.size() != 33) {
                if (ctout->vData.size() == 38 // Have prefix
                    && ctout->vData[33] == DO_STEALTH_PREFIX) {
                    fHavePrefix = true;
                    memcpy(&prefix, &ctout->vData[34], 4);
                } else {
                    continue;
                }
            }

            CKey sShared;
            std::vector<uint8_t> vchEphemPK;
            vchEphemPK.resize(33);
            memcpy(&vchEphemPK[0], &ctout->vData[0], 33);

            if (ProcessStealthOutput(address, vchEphemPK, prefix, fHavePrefix, sShared)) {
                fIsMine = true;
            }

            continue;
        } else if (txout->IsType(OUTPUT_RINGCT)) {
            nRingCT++;
            const CTxOutRingCT *rctout = (CTxOutRingCT*) txout.get();

            CKeyID idk = rctout->pk.GetID();

            // Uncover stealth
            uint32_t prefix = 0;
            bool fHavePrefix = false;
            if (rctout->vData.size() != 33) {
                if (rctout->vData.size() == 38 // Have prefix
                    && rctout->vData[33] == DO_STEALTH_PREFIX) {
                    fHavePrefix = true;
                    memcpy(&prefix, &rctout->vData[34], 4);
                } else {
                    continue;
                }
            }

            CKey sShared;
            std::vector<uint8_t> vchEphemPK;
            vchEphemPK.resize(33);
            memcpy(&vchEphemPK[0], &rctout->vData[0], 33);

            if (ProcessStealthOutput(idk, vchEphemPK, prefix, fHavePrefix, sShared)) {
                fIsMine = true;
            }
            continue;
        } else
        if (txout->IsType(OUTPUT_STANDARD)) {
            if (nOutputId < (int)tx.vpout.size()-1
                && tx.vpout[nOutputId+1]->IsType(OUTPUT_DATA)) {
                CTxOutData *txd = (CTxOutData*) tx.vpout[nOutputId+1].get();

                std::string sNarr;
                if (CheckForStealthAndNarration(txout.get(), txd, sNarr) < 0) {
                    LogPrintf("%s: txn %s, malformed data output %d.\n",  __func__, tx.GetHash().ToString(), nOutputId);
                }

                if (sNarr.length() > 0) {
                    std::string sKey = strprintf("n%d", nOutputId);
                    mapNarr[sKey] = sNarr;
                }
            }

            if (IsMine(txout.get())) {
                fIsMine = true;
            }
        }
    }

    return fIsMine;
}

void AnonWallet::MarkOutputSpent(const COutPoint& outpoint, bool isSpent)
{
    auto it = mapRecords.find(outpoint.hash);
    if (it == mapRecords.end())
        return;

    //Overwrite record with updated info
    auto record = it->second;
    COutputRecord* outputRecord = record.GetOutput(outpoint.n);
    if (outputRecord) {
        outputRecord->MarkSpent(isSpent);
    }
    SaveRecord(outpoint.hash, record);
}

bool KeyIdFromScriptPubKey(const CScript& script, CKeyID& id)
{
    CTxDestination dest;
    if (!ExtractDestination(script, dest))
        return false;
    if (dest.which() != 1)
        return false;
    id = boost::get<CKeyID>(dest);
    return true;
}

void AnonWallet::RescanWallet()
{
    AssertLockHeld(pwalletParent->cs_wallet);
    AssertLockHeld(cs_main);
    AnonWalletDB wdb(*walletDatabase);

    CCoinsView dummy;
    CCoinsViewCache view(&dummy);

    LOCK(mempool.cs);
    CCoinsViewMemPool viewMemPool(pcoinsTip.get(), mempool);
    view.SetBackend(viewMemPool);

    std::set<uint256> setErase;
    for (auto mi = mapRecords.begin(); mi != mapRecords.end(); mi++) {
        uint256 txid = mi->first;
        CTransactionRecord* txrecord = &mi->second;

        int nHeightTx = 0;
        if (!IsTransactionInChain(txid, nHeightTx, Params().GetConsensus())) {
            //This particular transaction never made it into the chain. If it is a certain amount of time old, delete it.
            if (GetTime() - txrecord->nTimeReceived > 60*20) {
                //20 minutes too old?
                for (auto input : txrecord->vin) {
                    //If the input is marked as spent because of this tx, then unmark
                    auto mi_2 = mapRecords.find(input.hash);
                    if (mi_2 != mapRecords.end()) {
                        CTransactionRecord* txrecord_input = &mi_2->second;
                        COutputRecord* outrecord = txrecord_input->GetOutput(input.n);

                        //not sure why this would ever happen
                        if (!outrecord)
                            continue;

                        //Assume the spentness is from this, should do a full chain rescan after? //todo
                        outrecord->MarkSpent(false);
                        outrecord->MarkPendingSpend(false);

                        wdb.WriteTxRecord(input.hash, *txrecord_input);
                        LogPrintf("%s: Marking %s as unspent\n", __func__, input.ToString());
                    }
                }
                setErase.emplace(txid);
            }
        } else {
            // Check outputs for inconsistencies
            CTransactionRef txRef;
            uint256 hashBlock;
            if (!GetTransaction(txid, txRef, Params().GetConsensus(), hashBlock, true))
                continue;

            for (auto it = txrecord->vout.begin(); it != txrecord->vout.end(); it++) {
                bool fUpdated = false;
                if (txRef->vpout.size() < static_cast<unsigned int>(it->n + 1))
                    continue;

                auto pout = txRef->vpout[it->n];
                if (it->scriptPubKey.empty()) {
                    CStoredTransaction stx;
                    if (it->nType == OUTPUT_CT) {
                        OwnBlindOut(&wdb, txid, (CTxOutCT*)pout.get(), *(it), stx, fUpdated);
                    } else if (it->nType == OUTPUT_RINGCT) {
                        OwnAnonOut(&wdb, txid, (CTxOutRingCT*)pout.get(), *(it), stx, fUpdated);
                    }
                    if (fUpdated)
                        LogPrintf("%s: Updating scriptpubkey for %s\n", __func__, COutPoint(txid, it->n).ToString());
                }

                // Check that the record's type is correct
                if (it->nType != pout->GetType()) {
                    it->nType = pout->GetType();
                    fUpdated = true;
                    LogPrintf("%s: Updated txout type for %s\n", __func__, COutPoint(txid, it->n).ToString());

                    if (it->IsBasecoin()) {
                        // clear scriptpubkey info on basecoin. Don't need redundant storing.
                        it->scriptPubKey.clear();
                    }
                }

                // If value is marked as 0, check blind to make sure it is actually 0
                if ((it->nFlags & ORF_OWNED) && it->GetAmount() == 0) {
                    int64_t nValue = 0;
                    uint256 blind;
                    bool fBlindsSuccess = true;
                    if (it->nType == OUTPUT_CT) {
                        auto pout = (CTxOutCT*) txRef->vpout[it->n].get();
                        CKeyID idKey;
                        if (!KeyIdFromScriptPubKey(pout->scriptPubKey, idKey))
                            continue;
                        if (GetCTBlinds(idKey, pout->vData, &pout->commitment, pout->vRangeproof, blind, nValue)) {
                            if (nValue != 0) {
                                fUpdated = true;
                                LogPrintf("%s: Recovered %s that was marked as 0 \n", __func__, FormatMoney(nValue));
                            }
                            fBlindsSuccess = true;
                        }
                    } else if (it->nType == OUTPUT_RINGCT) {
                        auto pout = (CTxOutRingCT*) txRef->vpout[it->n].get();
                        if (GetCTBlinds(pout->pk.GetID(), pout->vData, &pout->commitment, pout->vRangeproof, blind, nValue)) {
                            if (nValue != 0) {
                                fUpdated = true;
                                LogPrintf("%s: Recovered %s that was marked as 0 \n", __func__, FormatMoney(nValue));
                            }
                            fBlindsSuccess = true;
                        }
                    }
                    //Failed to decrypt blinds. This could happen if a 0 value output is added. Double check.
                    if (!fBlindsSuccess) {
                        auto nValueIn = txrecord->GetOwnedValueIn();
                        if (nValueIn == 0) {
                            //Maybe not correctly marked as 0 in, update this.
                            for (auto& in : txrecord->vin) {
                                auto mi = mapRecords.find(in.hash);
                                if (mi != mapRecords.end()) {
                                    auto prevout = mi->second.GetOutput(in.n);
                                    if (!prevout)
                                        continue;
                                    nValueIn += prevout->GetAmount();
                                }
                            }

                            if (nValueIn > 0) {
                                txrecord->SetOwnedValueIn(nValueIn);
                                LogPrintf("%s: Updated owned value in for %s\n", __func__, txid.GetHex());
                                fUpdated = true;
                            }
                        }
                        auto nValueOut = txrecord->GetValueSent(/*fExternalOnly*/false);
                        auto nFee = txrecord->nFee;
                        if (nValueIn - nFee - nValueOut > 0)
                            LogPrintf("%s: Failed to get blinds for output %s %s %s valuein:%s valueout:%s fee=%s\n",
                                      __func__, txid.GetHex(), COutPoint(txid, it->n).ToString(), it->ToString(),
                                      FormatMoney(nValueIn), FormatMoney(nValueOut), FormatMoney(nFee));
                    }

                    it->SetValue(nValue);
                }

                // Check if it has the correct is_spent status //todo ringct outputs
                if ((it->nFlags & ORF_OWNED)) {
                    if (it->nType == OUTPUT_CT) {
                        bool isSpentOnChain = !view.HaveCoin(COutPoint(txid, it->n));
                        if (isSpentOnChain != it->IsSpent()) {
                            it->MarkSpent(isSpentOnChain);
                            fUpdated = true;
                        }
                    } else if (it->nType == OUTPUT_RINGCT) {
                        auto txout = (CTxOutRingCT*)pout.get();
                        CKeyID idk = txout->pk.GetID();
                        CKey key;
                        if (GetKey(idk, key)) {
                            // Keyimage is required for the tx hash
                            CCmpPubKey ki;
                            if (secp256k1_get_keyimage(secp256k1_ctx_blind, ki.ncbegin(), txout->pk.begin(), key.begin()) == 0) {
                                // Double check key image is not used...
                                uint256 txhashKI;
                                if (pblocktree->ReadRCTKeyImage(ki, txhashKI)) {
                                    COutPoint out;
                                    if (wdb.ReadAnonKeyImage(ki, out)) {
                                        MarkOutputSpent(out, true);
                                        LogPrintf("%s: marking ringct output %s:%d spent\n", __func__, txid.GetHex(), it->n);
                                    }
                                }
                            }
                        }
                    }
                }

                if (fUpdated) {
                    wdb.WriteTxRecord(txid, *txrecord);
                }
            }
        }
    }

    for (const uint256& txid : setErase) {
        mapRecords.erase(txid);
        wdb.EraseTxRecord(txid);
    }
}

bool AnonWallet::AddToWalletIfInvolvingMe(const CTransactionRef& ptx, const CBlockIndex* pIndex, int posInBlock, bool fUpdate)
{
    const CTransaction& tx = *ptx;

    {
        AssertLockHeld(pwalletParent->cs_wallet);

        mapValue_t mapNarr;
        size_t nCT = 0, nRingCT = 0;
        bool fIsMine = ScanForOwnedOutputs(tx, nCT, nRingCT, mapNarr);
        bool fIsFromMe = false;
        MapRecords_t::const_iterator mir;
        for (const auto &txin : tx.vin) {
            if (txin.IsAnonInput()) {
                nRingCT++;

                AnonWalletDB wdb(*walletDatabase, "r");
                uint32_t nInputs, nRingSize;
                txin.GetAnonInfo(nInputs, nRingSize);

                const std::vector<uint8_t> &vKeyImages = txin.scriptData.stack[0];
                if (vKeyImages.size() != nInputs * 33) {
                    LogPrintf("Error: %s - Malformed anon txin, %s.\n", __func__, tx.GetHash().ToString());
                    continue;
                }

                for (size_t k = 0; k < nInputs; ++k) {
                    const CCmpPubKey &ki = *((CCmpPubKey*)&vKeyImages[k*33]);
                    COutPoint prevout;

                    // TODO: Keep keyimages in memory
                    if (!wdb.ReadAnonKeyImage(ki, prevout))
                        continue;

                    MarkOutputSpent(prevout, true);

                    fIsFromMe = true;
                }

                if (fIsFromMe)
                    break; // only need one match)
            }

            mir = mapRecords.find(txin.prevout.hash);
            if (mir != mapRecords.end()) {
                const COutputRecord *r = mir->second.GetOutput(txin.prevout.n);
                if (r && r->nFlags & ORF_OWN_ANY) {
                    fIsFromMe = true;
                }
                MarkOutputSpent(txin.prevout, true);
            }

            //Double check for standard outputs from us
            if (!fIsFromMe && !txin.IsAnonInput()) {
                if (pwalletParent->IsMine(txin, /*fCheckZerocoin*/true, /*fCheckAnon*/false))
                    fIsFromMe = true;
            }
        }

        if (fIsFromMe || fIsMine) {
            bool fExisted = mapRecords.count(tx.GetHash()) != 0;
            if (fExisted && !fUpdate) return false;
            if (fExisted || fIsMine || fIsFromMe) {
                CTransactionRecord rtx;
                bool rv = AddToRecord(rtx, tx, pIndex, posInBlock, false);
                return rv;
            }

            return false;
        }
    }
    return false;
}

int AnonWallet::InsertTempTxn(const uint256 &txid, const CTransactionRecord *rtx) const
{
    LOCK(pwalletParent->cs_wallet);

    CTransactionRef tx_new;
    CWalletTx wtx(pwalletParent.get(), std::move(tx_new));
    CStoredTransaction stx;

    /*
    uint256 hashBlock;
    CTransactionRef txRef;
    if (!GetTransaction(txid, txRef, Params().GetConsensus(), hashBlock, false))
        return errorN(1, "%s: GetTransaction failed, %s.\n", __func__, txid.ToString());
    */
    if (!AnonWalletDB(*walletDatabase).ReadStoredTx(txid, stx)) {
        return werrorN(1, "%s: ReadStoredTx failed for %s.\n", __func__, txid.ToString().c_str());
    }

    wtx.BindWallet(std::remove_const<CWallet*>::type(pwalletParent.get()));
    wtx.tx = stx.tx;

    if (rtx) {
        wtx.hashBlock = rtx->blockHash;
        wtx.nIndex = rtx->nIndex;
        wtx.nTimeSmart = rtx->GetTxTime();
        wtx.nTimeReceived = rtx->nTimeReceived;
    }

    std::pair<MapWallet_t::iterator, bool> ret = mapTempWallet.insert(std::make_pair(txid, wtx));
    if (!ret.second) // silence compiler warning
        LogPrintf("%s: insert failed for %s.\n", __func__, txid.ToString());

    return 0;
}

bool AnonWallet::OwnBlindOut(AnonWalletDB *pwdb, const uint256 &txhash, const CTxOutCT *pout, COutputRecord &rout,
        CStoredTransaction &stx, bool &fUpdated)
{
    isminetype mine = IsMine(pout);
    if (!(mine & ISMINE_ALL)) {
        return 0;
    }

    rout.nType = OUTPUT_CT;

    if (mine & ISMINE_SPENDABLE) {
        rout.nFlags |= ORF_OWNED;
    } else {
        rout.nFlags |= ORF_WATCHONLY;
    }

    if (IsLocked()) {
        COutPoint op(txhash, rout.n);
        if ((rout.nFlags & ORF_LOCKED)
            && !pwdb->HaveLockedAnonOut(op)) {
            rout.SetValue(0);
            fUpdated = true;
            if (!pwdb->WriteLockedAnonOut(op)) {
                LogPrintf("Error: %s - WriteLockedAnonOut failed.\n", __func__);
            }
        }
        return true;
    }

    CScript scriptPubKey;
    if (!pout->GetScriptPubKey(scriptPubKey))
        return error("%s: GetScriptPubKey failed.", __func__);

    CTxDestination dest;
    if (!ExtractDestination(scriptPubKey, dest))
        return error("%s: ExtractDestination failed", __func__);

    if (dest.which() != 1)
        return error("%s: destination is not key id", __func__);
    CKeyID idKey = boost::get<CKeyID>(dest);

    CKey key;
    if (!GetKey(idKey, key))
        return error("%s: GetKey failed for out.n=%d id=%s", __func__, rout.n, idKey.GetHex());

    if (pout->vData.size() < 33)
        return error("%s: vData.size() < 33.", __func__);

    CPubKey pkEphem;
    pkEphem.Set(pout->vData.begin(), pout->vData.begin() + 33);

    // Regenerate nonce
    uint256 nonce = key.ECDH(pkEphem);
    CSHA256().Write(nonce.begin(), 32).Finalize(nonce.begin());

    uint64_t min_value, max_value;
    uint8_t blindOut[32];
    unsigned char msg[256]; // Currently narration is capped at 32 bytes
    size_t mlen = sizeof(msg);
    memset(msg, 0, mlen);
    uint64_t amountOut;
    if (1 != secp256k1_rangeproof_rewind(secp256k1_ctx_blind,
        blindOut, &amountOut, msg, &mlen, nonce.begin(),
        &min_value, &max_value,
        &pout->commitment, pout->vRangeproof.data(), pout->vRangeproof.size(),
        nullptr, 0,
        secp256k1_generator_h)) {
        return error("%s: secp256k1_rangeproof_rewind failed.", __func__);
    }

    msg[mlen-1] = '\0';

    size_t nNarr = strlen((const char*)msg);
    if (nNarr > 0) {
        rout.sNarration.assign((const char*)msg, nNarr);
    }

    rout.SetValue(amountOut);
    rout.scriptPubKey = pout->scriptPubKey;
    rout.nFlags &= ~ORF_LOCKED;

    stx.InsertBlind(rout.n, blindOut);
    fUpdated = true;

    return true;
}

bool AnonWallet::GetCTBlinds(CKeyID idKey, std::vector<uint8_t>& vData,
        secp256k1_pedersen_commitment* commitment, std::vector<uint8_t>& vRangeproof, uint256 &blind, int64_t& nValue) const
{
    CKey key;
    if (!GetKey(idKey, key))
        return error("%s: could not locate key for id %s\n", __func__, idKey.GetHex());

    if (vData.size() < 33)
        return error("%s: vData.size() < 33.", __func__);

    CPubKey pkEphem;
    pkEphem.Set(vData.begin(), vData.begin() + 33);

    // Regenerate nonce
    uint256 nonce = key.ECDH(pkEphem);
    CSHA256().Write(nonce.begin(), 32).Finalize(nonce.begin());

    uint64_t min_value, max_value;
    uint8_t blindOut[32];
    unsigned char msg[256]; // Currently narration is capped at 32 bytes
    size_t mlen = sizeof(msg);
    memset(msg, 0, mlen);
    uint64_t amountOut;
    if (1 != secp256k1_rangeproof_rewind(secp256k1_ctx_blind,
                                         blindOut, &amountOut, msg, &mlen, nonce.begin(),
                                         &min_value, &max_value,
                                         commitment, vRangeproof.data(), vRangeproof.size(),
                                         nullptr, 0,
                                         secp256k1_generator_h)) {
        return error("%s: secp256k1_rangeproof_rewind failed.", __func__);
    }

    blind = uint256();
    memcpy(blind.begin(), blindOut, 32);
    nValue = amountOut;

    return true;
}

bool AnonWallet::GetCTBlindsFromOutput(const CTxOutBase *pout, uint256& blind) const
{
    int64_t nValue = 0;
    if (pout->GetType() == OUTPUT_CT) {
        auto txout = (CTxOutCT*)pout;
        CKeyID id;
        if (!KeyIdFromScriptPubKey(txout->scriptPubKey, id))
            return false;
        return GetCTBlinds(id, txout->vData, &txout->commitment, txout->vRangeproof, blind, nValue);
    } else if (pout->GetType() == OUTPUT_RINGCT) {
        auto txout = (CTxOutRingCT*)pout;
        return GetCTBlinds(txout->pk.GetID(), txout->vData, &txout->commitment, txout->vRangeproof, blind, nValue);
    }

    return false;
}

bool AnonWallet::HaveKeyID(const CKeyID& id)
{
    //Anything in the anon wallet should be in the map of key paths
    if (mapKeyPaths.count(id))
        return true;

    //Check the plain cwallet keystore
    return pwalletParent->HaveKey(id);
}

int AnonWallet::OwnAnonOut(AnonWalletDB *pwdb, const uint256 &txhash, const CTxOutRingCT *pout, COutputRecord &rout, CStoredTransaction &stx, bool &fUpdated)
{
    rout.nType = OUTPUT_RINGCT;
    CKeyID idStealthDestination = pout->pk.GetID();
    CKey key;
    if (IsLocked()) {
        if (!HaveKeyID(idStealthDestination))
            return 0;

        rout.nFlags |= ORF_OWNED;

        fUpdated = true;
        return 1;
    }

    if (!GetKey(idStealthDestination, key))
        return 0;

    if (pout->vData.size() < 33)
        return werrorN(0, "%s: vData.size() < 33.", __func__);

    CPubKey pkEphem;
    pkEphem.Set(pout->vData.begin(), pout->vData.begin() + 33);

    // Regenerate nonce
    uint256 nonce = key.ECDH(pkEphem);
    CSHA256().Write(nonce.begin(), 32).Finalize(nonce.begin());

    uint64_t min_value, max_value;
    uint8_t blindOut[32];
    unsigned char msg[256]; // Currently narration is capped at 32 bytes
    size_t mlen = sizeof(msg);
    memset(msg, 0, mlen);
    uint64_t amountOut;
    if (1 != secp256k1_rangeproof_rewind(secp256k1_ctx_blind,
        blindOut, &amountOut, msg, &mlen, nonce.begin(),
        &min_value, &max_value,
        &pout->commitment, pout->vRangeproof.data(), pout->vRangeproof.size(),
        nullptr, 0,
        secp256k1_generator_h)) {
        return werrorN(0, "%s: secp256k1_rangeproof_rewind failed.", __func__);
    }

    msg[mlen-1] = '\0';
    size_t nNarr = strlen((const char*)msg);
    if (nNarr > 0) {
        rout.sNarration.assign((const char*)msg, nNarr);
    }

    rout.nFlags |= ORF_OWNED;
    rout.SetValue(amountOut);

    CStealthAddress stealthAddress;
    if (GetStealthLinked(idStealthDestination, stealthAddress)) {
        auto idStealth = stealthAddress.GetID();
        rout.AddStealthAddress(idStealth);
        rout.scriptPubKey = GetScriptForDestination(idStealth);
    }

    COutPoint op(txhash, rout.n);
    CCmpPubKey ki;

    if (0 != secp256k1_get_keyimage(secp256k1_ctx_blind, ki.ncbegin(), pout->pk.begin(), key.begin())) {
        LogPrintf("Error: %s - secp256k1_get_keyimage failed.\n", __func__);
    } else
    if (!pwdb->WriteAnonKeyImage(ki, op)) {
        LogPrintf("Error: %s - WriteAnonKeyImage failed.\n", __func__);
    }

    rout.nFlags &= ~ORF_LOCKED;
    stx.InsertBlind(rout.n, blindOut);
    fUpdated = true;

    return 1;
}

bool AnonWallet::AddTxinToSpends(const CTxIn &txin, const uint256 &txhash)
{
    AssertLockHeld(pwalletParent->cs_wallet);
    if (txin.IsAnonInput()) {
        AnonWalletDB wdb(*walletDatabase, "r");
        uint32_t nInputs, nRingSize;
        txin.GetAnonInfo(nInputs, nRingSize);

        const std::vector<uint8_t> &vKeyImages = txin.scriptData.stack[0];
        if (vKeyImages.size() != nInputs * 33) {
            return werror("%s: Malformed anon txin, %s.", __func__, txhash.ToString());
        }

        for (size_t k = 0; k < nInputs; ++k) {
            const CCmpPubKey &ki = *((CCmpPubKey*)&vKeyImages[k*33]);
            COutPoint prevout;
            // TODO: Keep keyimages in memory
            if (!wdb.ReadAnonKeyImage(ki, prevout)) {
                continue;
            }
            AddToSpends(prevout, txhash);
        }

        return true;
    }

    AddToSpends(txin.prevout, txhash);
    return true;
}

bool AnonWallet::ProcessPlaceholder(AnonWalletDB *pwdb, const CTransaction &tx, CTransactionRecord &rtx)
{
    rtx.EraseOutput(OR_PLACEHOLDER_N);

    CAmount nDebit = GetDebit(pwdb, rtx, ISMINE_ALL);
    CAmount nCredit = rtx.TotalOutput() + rtx.nFee;
    if (nDebit > 0 && nDebit != nCredit) {
        const COutputRecord *pROutChange = rtx.GetChangeOutput();

        int nType = OUTPUT_STANDARD;
        for (size_t i = 0; i < tx.vpout.size(); ++i) {
            const auto &txout = tx.vpout[i];
            if (!(txout->IsType(OUTPUT_CT) || txout->IsType(OUTPUT_RINGCT))) {
                continue;
            }
            if (pROutChange && pROutChange->n == i) {
                continue;
            }
            nType = txout->GetType();
            break;
        }

        COutputRecord rout;
        rout.n = OR_PLACEHOLDER_N;
        rout.nType = nType;
        rout.nFlags |= ORF_FROM;
        rout.SetValue(nDebit - nCredit);

        rtx.InsertOutput(rout);
    }
    return true;
}

bool AnonWallet::AddToRecord(CTransactionRecord &rtxIn, const CTransaction &tx,
    const CBlockIndex *pIndex, int posInBlock, bool fFlushOnClose)
{
    AssertLockHeld(pwalletParent->cs_wallet);
    AnonWalletDB wdb(*walletDatabase, "r+", fFlushOnClose);

    uint256 txhash = tx.GetHash();

    // Inserts only if not exists, returns tx inserted or tx found
    std::pair<MapRecords_t::iterator, bool> ret = mapRecords.insert(std::make_pair(txhash, rtxIn));
    CTransactionRecord &rtx = ret.first->second;

    bool fUpdated = false;
    if (pIndex) {
        if (rtx.blockHash != pIndex->GetBlockHash()
            || rtx.nIndex != posInBlock) {
            fUpdated = true;
        }
        rtx.blockHash = pIndex->GetBlockHash();
        rtx.nIndex = posInBlock;
        rtx.nBlockTime = pIndex->nTime;
    }

    CAmount nValueInOwned = 0;
    bool fInsertedNew = ret.second;
    if (fInsertedNew) {
        if (pIndex)
            rtx.nTimeReceived = pIndex->GetBlockTime();
        else
            rtx.nTimeReceived = GetAdjustedTime();

        MapRecords_t::iterator mri = ret.first;
        rtxOrdered.insert(std::make_pair(rtx.nTimeReceived, mri));

        for (auto &txin : tx.vin) {
            if (txin.IsAnonInput()) {
                rtx.nFlags |= ORF_ANON_IN;
            } else {
                if (pwalletParent->IsMine(txin)) {
                    rtx.nFlags |= ORF_FROM;
                    rtx.nFlags |= ORF_BASECOIN_IN;
                }
            }
            AddTxinToSpends(txin, txhash);
        }

        if (rtx.nFlags & ORF_ANON_IN) {
            COutPoint outpoint;
            for (auto &txin : tx.vin) {
                if (!txin.IsAnonInput()) {
                    continue;
                }

                uint32_t nInputs, nRingSize;
                txin.GetAnonInfo(nInputs, nRingSize);
                const std::vector<uint8_t> &vKeyImages = txin.scriptData.stack[0];

                if (vKeyImages.size() != nInputs * 33)
                    return error("%s: FIXME keyimage wrong size\n  size=%d\n  inputs=%d\n", __func__, vKeyImages.size(), nInputs);

                for (size_t k = 0; k < nInputs; ++k) {
                    const CCmpPubKey &keyImage = *((CCmpPubKey*)&vKeyImages[k*33]);
                    outpoint.n = 0;
                    memcpy(outpoint.hash.begin(), keyImage.begin(), 32);
                    outpoint.n = *(keyImage.begin()+32);

                    rtx.vin.push_back(outpoint);

                    //Look up value of own inputs
                    COutputRecord outRecord;
                    if (mapRecords.count(outpoint.hash)) {
                        if (GetOutputRecord(outpoint, outRecord)) {
                            if (outRecord.nFlags & ORF_OWN_ANY)
                                nValueInOwned += outRecord.GetAmount();
                        }
                    }
                }
            }
        } else if (!tx.IsZerocoinSpend()) {
            rtx.vin.resize(tx.vin.size());
            for (size_t k = 0; k < tx.vin.size(); ++k) {
                rtx.vin[k] = tx.vin[k].prevout;
            }

            if (!tx.vin[0].IsZerocoinSpend()) {
                // Lookup 1st input to set type
                Coin coin;
                const auto &prevout0 = tx.vin[0].prevout;
                if (pcoinsdbview->GetCoin(prevout0, coin)) {
                    if (coin.nType == OUTPUT_CT) {
                        rtx.nFlags |= ORF_BLIND_IN;
                    }
                } else {
                    uint256 hashBlock;
                    CTransactionRef txPrev;
                    if (GetTransaction(prevout0.hash, txPrev, Params().GetConsensus(), hashBlock, true)) {
                        if (txPrev->vpout.size() > prevout0.n && txPrev->vpout[prevout0.n]->IsType(OUTPUT_CT)) {
                            rtx.nFlags |= ORF_BLIND_IN;
                        }
                    }
                }
            }
        } else if (tx.IsZerocoinSpend()) {
            // Add value of zerocoin spends
            std::set<uint256> setSerialHashes;
            TxToSerialHashSet(&tx, setSerialHashes);

            bool fIsMine = false;
            for (const uint256& hash : setSerialHashes) {
                if (pwalletParent->IsMyZerocoinSpend(hash)) {
                    fIsMine = true;
                    break;
                }
            }
            if (fIsMine) {
                nValueInOwned += tx.GetZerocoinSpent();
            }
        }
    }
    rtx.SetOwnedValueIn(nValueInOwned);

    CStoredTransaction stx;
    if (!wdb.ReadStoredTx(txhash, stx)) {
        //LogPrintf("%s:%s no stored tx\n", __func__, __LINE__);
        stx.vBlinds.clear();
    }

    for (size_t i = 0; i < tx.vpout.size(); ++i) {
        const auto &txout = tx.vpout[i];

        COutputRecord rout;
        COutputRecord *pout = rtx.GetOutput(i);

        bool fHave = false;
        if (pout) { // Have output recorded already, still need to check if owned
            fHave = true;
        } else {
            pout = &rout;
            //pout->nFlags |= ORF_LOCKED; // mark new output as locked
        }

        //If there were any spending flags, remove
        auto nFlagsPrev = pout->nFlags;
        pout->nFlags &= ~ORF_SPENT;
        pout->nFlags &= ~ORF_PENDING_SPEND;
        if (!fUpdated)
            fUpdated = nFlagsPrev != pout->nFlags;

        pout->n = i;
        pout->nType = txout->nVersion;

        switch (txout->nVersion) {
            case OUTPUT_STANDARD:
                {
                    //Veil: todo: add more logic here for better tracking, also maybe not necessary to add if not ismine
                    rtx.InsertOutput(*pout);
                }
                break;
            case OUTPUT_CT:
                if (OwnBlindOut(&wdb, txhash, (CTxOutCT*)txout.get(), *pout, stx, fUpdated)
                    && !fHave) {
                    fUpdated = true;
                } else {
                    //Just set as from me.
                    if (rtx.nFlags & ORF_FROM) {
                        pout->nFlags |= ORF_FROM;
                    }

                    //todo: this could also be sent from someone else, to multiple recipients, in which this wallet is one of many sent to
                }
                rtx.InsertOutput(*pout);
                break;
            case OUTPUT_RINGCT:
                if (OwnAnonOut(&wdb, txhash, (CTxOutRingCT*)txout.get(), *pout, stx, fUpdated)
                    && !fHave) {
                    fUpdated = true;
                } else {
                    //Just set as from me.
                    if (rtx.nFlags & ORF_FROM) {
                        pout->nFlags |= ORF_FROM;
                    }
                }
                rtx.InsertOutput(*pout);
                break;
        }
        fUpdated = true;
    }

    if (fInsertedNew || fUpdated) {
        // Plain to plain will always be a wtx, revisit if adding p2p to rtx
        if (!tx.GetCTFee(rtx.nFee))
            LogPrintf("%s: ERROR - GetCTFee failed %s.\n", __func__, txhash.ToString());

        // If txn has change, it must have been sent by this wallet
        if (rtx.HaveChange()) {
            for (auto &r : rtx.vout) {
                if (!(r.nFlags & ORF_CHANGE)) {
                    r.nFlags |= ORF_FROM;
                }
            }

            ProcessPlaceholder(&wdb, tx, rtx);
        }
        stx.tx = MakeTransactionRef(tx);
        if (!wdb.WriteTxRecord(txhash, rtx)
            || !wdb.WriteStoredTx(txhash, stx)) {
            return false;
        }
    }

    return true;
}

std::vector<uint256> AnonWallet::ResendRecordTransactionsBefore(int64_t nTime, CConnman *connman)
{
    std::vector<uint256> result;

    LOCK(pwalletParent->cs_wallet);

    for (RtxOrdered_t::iterator it = rtxOrdered.begin(); it != rtxOrdered.end(); ++it) {
        if (it->first > nTime) {
            continue;
        }

        const uint256 &txhash = it->second->first;
        CTransactionRecord &rtx = it->second->second;

        if (rtx.IsAbandoned()) {
            continue;
        }
        if (GetDepthInMainChain(rtx.blockHash, rtx.nIndex) != 0) {
            continue;
        }

        MapWallet_t::iterator twi = mapTempWallet.find(txhash);

        if (twi == mapTempWallet.end()) {
            if (0 != InsertTempTxn(txhash, &rtx)
                || (twi = mapTempWallet.find(txhash)) == mapTempWallet.end()) {
                LogPrintf("ERROR: %s - InsertTempTxn failed %s.\n", __func__, txhash.ToString());
            }
        }

        if (twi != mapTempWallet.end()) {
            if (twi->second.RelayWalletTransaction(connman)) {
                result.push_back(txhash);
            }
        }
    }

    return result;
}

void AnonWallet::AvailableBlindedCoins(std::vector<COutputR>& vCoins, bool fOnlySafe, const CCoinControl *coinControl, const CAmount& nMinimumAmount, const CAmount& nMaximumAmount, const CAmount& nMinimumSumAmount, const uint64_t& nMaximumCount, const int& nMinDepth, const int& nMaxDepth, bool fIncludeImmature) const
{
    AssertLockHeld(cs_main);
    AssertLockHeld(pwalletParent->cs_wallet);

    vCoins.clear();

    CCoinsView dummy;
    CCoinsViewCache view(&dummy);

    LOCK(mempool.cs);
    CCoinsViewMemPool viewMemPool(pcoinsTip.get(), mempool);
    view.SetBackend(viewMemPool);

//    if (coinControl && coinControl->HasSelected()) {
//        // Add specified coins which may not be in the chain
//        for (MapRecords_t::const_iterator it = mapTempRecords.begin(); it !=  mapTempRecords.end(); ++it) {
//            const uint256 &txid = it->first;
//            const CTransactionRecord &rtx = it->second;
//            for (const auto &r : rtx.vout) {
//                if (r.IsSpent() || !view.HaveCoin(COutPoint(txid, r.n)))
//                    continue;
//
//                if (coinControl->IsSelected(COutPoint(txid, r.n))) {
//                    int nDepth = 0;
//                    bool fSpendable = true;
//                    bool fSolvable = true;
//                    bool safeTx = true;
//                    bool fMature = false;
//                    bool fNeedHardwareKey = false;
//                    vCoins.emplace_back(txid, it, r.n, nDepth, fSpendable, fSolvable, safeTx, fMature, fNeedHardwareKey);
//                }
//            }
//        }
//    }

    CAmount nTotal = 0;

    for (MapRecords_t::const_iterator it = mapRecords.begin(); it != mapRecords.end(); ++it) {
        const uint256 &txid = it->first;
        const CTransactionRecord &rtx = it->second;

        // TODO: implement when moving coinbase and coinstake txns to mapRecords
        //if (pcoin->GetBlocksToMaturity() > 0)
        //    continue;

        int nDepth = GetDepthInMainChain(rtx.blockHash, rtx.nIndex);
        if (nDepth < 0)
            continue;

        if (nDepth < nMinDepth || nDepth > nMaxDepth)
            continue;

        // We should not consider coins which aren't at least in our mempool
        // It's possible for these to be conflicted via ancestors which we may never be able to detect
        if (nDepth == 0 && !InMempool(txid))
            continue;

        bool safeTx = IsTrusted(txid, rtx.blockHash);
        if (nDepth == 0 && rtx.mapValue.count(RTXVT_REPLACES_TXID)) {
            safeTx = false;
        }

        if (nDepth == 0 && rtx.mapValue.count(RTXVT_REPLACED_BY_TXID)) {
            safeTx = false;
        }

        if (fOnlySafe && !safeTx) {
            continue;
        }

        for (const auto &r : rtx.vout) {
            if (coinControl && coinControl->HasSelected()) {
                if (!coinControl->IsSelected(COutPoint(txid, r.n)))
                    continue;
            }

            if (r.nType != OUTPUT_CT)
                continue;

            if (!(r.nFlags & ORF_OWN_ANY))
                continue;

            if (r.IsSpent() || IsSpent(txid, r.n) || !view.HaveCoin(COutPoint(txid, r.n)))
                continue;

            if (r.GetAmount() < nMinimumAmount || r.GetAmount() > nMaximumAmount)
                continue;

            if (coinControl && coinControl->HasSelected() && !coinControl->fAllowOtherInputs && !coinControl->IsSelected(COutPoint(txid, r.n)))
                continue;

            if (!coinControl/* || !coinControl->fAllowLocked)
                && IsLockedCoin(txid, r.n)*/)
                continue;

            bool fMature = true;
            bool fSpendable = (coinControl && !coinControl->fAllowWatchOnly && !(r.nFlags & ORF_OWNED)) ? false : true;
            bool fSolvable = true;
            //bool fNeedHardwareKey = (r.nFlags & ORF_HARDWARE_DEVICE);

            vCoins.emplace_back(txid, it, r.n, nDepth, fSpendable, fSolvable, safeTx, fMature, /*fNeedHardwareKey*/false);

            if (nMinimumSumAmount != MAX_MONEY) {
                nTotal += r.GetAmount();

                if (nTotal >= nMinimumSumAmount) {
                    return;
                }
            }

            // Checks the maximum number of UTXO's.
            if (nMaximumCount > 0 && vCoins.size() >= nMaximumCount) {
                return;
            }
        }
    }
}

bool AnonWallet::SelectBlindedCoins(const std::vector<COutputR> &vAvailableCoins, const CAmount &nTargetValue,
        std::vector<std::pair<MapRecords_t::const_iterator,unsigned int> > &setCoinsRet, CAmount &nValueRet, const CCoinControl *coinControl) const
{
    std::vector<COutputR> vCoins(vAvailableCoins);

    // calculate value from preset inputs and store them
    std::vector<std::pair<MapRecords_t::const_iterator,unsigned int> > vPresetCoins;
    CAmount nValueFromPresetInputs = 0;

    std::vector<COutPoint> vPresetInputs;
    if (coinControl) {
        coinControl->ListSelected(vPresetInputs);
    }

    for (auto &outpoint : vPresetInputs) {
        MapRecords_t::const_iterator it;
        if ((it = mapTempRecords.find(outpoint.hash)) != mapTempRecords.end() // Must check mapTempRecords first, mapRecords may contain the same tx without the relevant output.
            || (it = mapRecords.find(outpoint.hash)) != mapRecords.end()) { // Allows non-wallet inputs
            const CTransactionRecord &rtx = it->second;
            const COutputRecord *oR = rtx.GetOutput(outpoint.n);
            if (!oR) {
                return error("%s: Can't find output %s\n", __func__, outpoint.ToString());
            }

            nValueFromPresetInputs += oR->GetAmount();
            vPresetCoins.push_back(std::make_pair(it, outpoint.n));
        } else {
            return error("%s: Can't find output %s\n", __func__, outpoint.ToString());
        }
    }

    // coin control -> return all selected outputs (we want all selected to go into the transaction for sure)
    if (coinControl && coinControl->HasSelected() && !coinControl->fAllowOtherInputs) {
        nValueRet = nValueFromPresetInputs;
        setCoinsRet.insert(setCoinsRet.end(), vPresetCoins.begin(), vPresetCoins.end());
        return (nValueRet >= nTargetValue);
    }

    // Remove preset inputs from vCoins
    if (vPresetCoins.size() > 0) {
        for (std::vector<COutputR>::iterator it = vCoins.begin(); it != vCoins.end();) {
            std::vector<std::pair<MapRecords_t::const_iterator,unsigned int> >::const_iterator it2;
            bool fFound = false;
            for (it2 = vPresetCoins.begin(); it2 != vPresetCoins.end(); it2++) {
                if (it2->first->first == it->txhash && it2->second == (uint32_t) it->i) {
                    fFound = true;
                    break;
                }
            }
            //if (std::find(vPresetCoins.begin(), vPresetCoins.end(), std::make_pair(it->rtx, it->i)) != vPresetCoins.end())
            if (fFound) {
                it = vCoins.erase(it);
            } else {
                ++it;
            }
        }
    }

    size_t max_ancestors = (size_t)std::max<int64_t>(1, gArgs.GetArg("-limitancestorcount", DEFAULT_ANCESTOR_LIMIT));
    size_t max_descendants = (size_t)std::max<int64_t>(1, gArgs.GetArg("-limitdescendantcount", DEFAULT_DESCENDANT_LIMIT));
    bool fRejectLongChains = gArgs.GetBoolArg("-walletrejectlongchains", DEFAULT_WALLET_REJECT_LONG_CHAINS);

    auto m_spend_zero_conf_change = pwalletParent->m_spend_zero_conf_change;

    bool res = nTargetValue <= nValueFromPresetInputs ||
        SelectCoinsMinConf(nTargetValue - nValueFromPresetInputs, CoinEligibilityFilter(1, 6, 0), vCoins, setCoinsRet, nValueRet) ||
        SelectCoinsMinConf(nTargetValue - nValueFromPresetInputs, CoinEligibilityFilter(1, 1, 0), vCoins, setCoinsRet, nValueRet) ||
        (m_spend_zero_conf_change && SelectCoinsMinConf(nTargetValue - nValueFromPresetInputs, CoinEligibilityFilter(0, 1, 2), vCoins, setCoinsRet, nValueRet)) ||
        (m_spend_zero_conf_change && SelectCoinsMinConf(nTargetValue - nValueFromPresetInputs, CoinEligibilityFilter(0, 1, std::min((size_t)4, max_ancestors/3), std::min((size_t)4, max_descendants/3)), vCoins, setCoinsRet, nValueRet)) ||
        (m_spend_zero_conf_change && SelectCoinsMinConf(nTargetValue - nValueFromPresetInputs, CoinEligibilityFilter(0, 1, max_ancestors/2, max_descendants/2), vCoins, setCoinsRet, nValueRet)) ||
        (m_spend_zero_conf_change && SelectCoinsMinConf(nTargetValue - nValueFromPresetInputs, CoinEligibilityFilter(0, 1, max_ancestors-1, max_descendants-1), vCoins, setCoinsRet, nValueRet)) ||
        (m_spend_zero_conf_change && !fRejectLongChains && SelectCoinsMinConf(nTargetValue - nValueFromPresetInputs, CoinEligibilityFilter(0, 1, std::numeric_limits<uint64_t>::max()), vCoins, setCoinsRet, nValueRet));


    // because SelectCoinsMinConf clears the setCoinsRet, we now add the possible inputs to the coinset
    setCoinsRet.insert(setCoinsRet.end(), vPresetCoins.begin(), vPresetCoins.end());

    // add preset inputs to the total value selected
    nValueRet += nValueFromPresetInputs;

    std::random_shuffle(setCoinsRet.begin(), setCoinsRet.end(), GetRandInt);

    return res;
}

void AnonWallet::AvailableAnonCoins(std::vector<COutputR> &vCoins, bool fOnlySafe, const CCoinControl *coinControl,
        const CAmount& nMinimumAmount, const CAmount& nMaximumAmount, const CAmount& nMinimumSumAmount,
        const uint64_t& nMaximumCount, const int& nMinDepth, const int& nMaxDepth, bool fIncludeImmature) const
{
    AssertLockHeld(cs_main);
    AssertLockHeld(pwalletParent->cs_wallet);

    vCoins.clear();
    CAmount nTotal = 0;

    const Consensus::Params& consensusParams = Params().GetConsensus();
    for (MapRecords_t::const_iterator it = mapRecords.begin(); it != mapRecords.end(); ++it) {
        const uint256 &txid = it->first;
        const CTransactionRecord &rtx = it->second;

        // TODO: implement when moving coinbase and coinstake txns to mapRecords
        //if (pcoin->GetBlocksToMaturity() > 0)
        //    continue;

        int nDepth = GetDepthInMainChain(rtx.blockHash, rtx.nIndex);
        bool fMature = nDepth >= consensusParams.nMinRCTOutputDepth;
        if (!fIncludeImmature && !fMature) {
            continue;
        }

        // Coins at depth 0 will never be available, no need to check depth0 cases

        if (nDepth < nMinDepth || nDepth > nMaxDepth) {
            continue;
        }

        bool safeTx = IsTrusted(txid, rtx.blockHash);

        if (fOnlySafe && !safeTx) {
            continue;
        }

        for (const auto &r : rtx.vout) {
            if (r.nType != OUTPUT_RINGCT) {
                continue;
            }

            if (!(r.nFlags & ORF_OWNED)) {
                continue;
            }

            if (IsSpent(txid, r.n) || r.IsSpent()) {
                continue;
            }

            if (r.GetRawValue() < nMinimumAmount || r.GetRawValue() > nMaximumAmount) {
                continue;
            }

            if (coinControl && coinControl->HasSelected() && !coinControl->fAllowOtherInputs && !coinControl->IsSelected(COutPoint(txid, r.n))) {
                continue;
            }

            if (!coinControl/* || !coinControl->fAllowLocked) && IsLockedCoin(txid, r.n)*/) {
                continue;
            }

            bool fMature = true;
            bool fSpendable = (coinControl && !coinControl->fAllowWatchOnly && !(r.nFlags & ORF_OWNED)) ? false : true;
            bool fSolvable = true;
            //bool fNeedHardwareKey = (r.nFlags & ORF_HARDWARE_DEVICE);

            vCoins.emplace_back(txid, it, r.n, nDepth, fSpendable, fSolvable, safeTx, fMature, /*fNeedHardwareKey*/false);

            if (nMinimumSumAmount != MAX_MONEY) {
                nTotal += r.GetRawValue();

                if (nTotal >= nMinimumSumAmount) {
                    return;
                }
            }

            // Checks the maximum number of UTXO's.
            if (nMaximumCount > 0 && vCoins.size() >= nMaximumCount) {
                return;
            }
        }
    }

    random_shuffle(vCoins.begin(), vCoins.end(), GetRandInt);
    return;
}

bool GetAddress(const AnonWallet *pw, const COutputRecord *pout, CTxDestination &address)
{
    if (ExtractDestination(pout->scriptPubKey, address)) {
        return true;
    }
    if (pout->IsStealth()) {
        CKeyID idStealth;
        if (!pout->GetStealthID(idStealth))
            return error("%s: Warning, malformed vPath.\n", __func__);

        CStealthAddress sx;
        if (pw->GetStealthLinked(idStealth, sx))
            return true;
    }
    return false;
}

struct CompareValueOnly
{
    bool operator()(const std::pair<CAmount, std::pair<MapRecords_t::const_iterator, unsigned int> >& t1,
                    const std::pair<CAmount, std::pair<MapRecords_t::const_iterator, unsigned int> >& t2) const
    {
        return t1.first < t2.first;
    }
};

static void ApproximateBestSubset(std::vector<std::pair<CAmount, std::pair<MapRecords_t::const_iterator,unsigned int> > >vValue, const CAmount& nTotalLower, const CAmount& nTargetValue,
                                  std::vector<char>& vfBest, CAmount& nBest, int iterations = 1000)
{
    std::vector<char> vfIncluded;

    vfBest.assign(vValue.size(), true);
    nBest = nTotalLower;

    FastRandomContext insecure_rand;

    for (int nRep = 0; nRep < iterations && nBest != nTargetValue; nRep++)
    {
        vfIncluded.assign(vValue.size(), false);
        CAmount nTotal = 0;
        bool fReachedTarget = false;
        for (int nPass = 0; nPass < 2 && !fReachedTarget; nPass++)
        {
            for (unsigned int i = 0; i < vValue.size(); i++)
            {
                //The solver here uses a randomized algorithm,
                //the randomness serves no real security purpose but is just
                //needed to prevent degenerate behavior and it is important
                //that the rng is fast. We do not use a constant random sequence,
                //because there may be some privacy improvement by making
                //the selection random.
                if (nPass == 0 ? insecure_rand.rand32()&1 : !vfIncluded[i])
                {
                    nTotal += vValue[i].first;
                    vfIncluded[i] = true;
                    if (nTotal >= nTargetValue)
                    {
                        fReachedTarget = true;
                        if (nTotal < nBest)
                        {
                            nBest = nTotal;
                            vfBest = vfIncluded;
                        }
                        nTotal -= vValue[i].first;
                        vfIncluded[i] = false;
                    }
                }
            }
        }
    }
    return;
}

bool AnonWallet::SelectCoinsMinConf(const CAmount& nTargetValue, const CoinEligibilityFilter& eligibility_filter,
    std::vector<COutputR> vCoins, std::vector<std::pair<MapRecords_t::const_iterator,unsigned int> >& setCoinsRet, CAmount& nValueRet) const
{
    setCoinsRet.clear();
    nValueRet = 0;

    // List of values less than target
    std::pair<CAmount, std::pair<MapRecords_t::const_iterator,unsigned int> > coinLowestLarger;
    coinLowestLarger.first = std::numeric_limits<CAmount>::max();
    coinLowestLarger.second.first = mapRecords.end();
    std::vector<std::pair<CAmount, std::pair<MapRecords_t::const_iterator,unsigned int> > > vValue;
    CAmount nTotalLower = 0;

    random_shuffle(vCoins.begin(), vCoins.end(), GetRandInt);

    for (const auto &r : vCoins) {
        //if (!r.fSpendable)
        //    continue;
        MapRecords_t::const_iterator rtxi = r.rtx;
        const CTransactionRecord *rtx = &rtxi->second;

        const CWalletTx *pcoin = pwalletParent->GetWalletTx(r.txhash);
        if (!pcoin) {
            if (0 != InsertTempTxn(r.txhash, rtx)
                || !(pcoin = pwalletParent->GetWalletTx(r.txhash)))
                return werror("%s: InsertTempTxn failed.\n", __func__);
        }


        if (r.nDepth < (pcoin->IsFromMe(ISMINE_ALL) ? eligibility_filter.conf_mine : eligibility_filter.conf_theirs))
            continue;

        size_t ancestors, descendants;
        mempool.GetTransactionAncestry(r.txhash, ancestors, descendants);
        if (ancestors > eligibility_filter.max_ancestors || descendants > eligibility_filter.max_descendants) {
             continue;
        }

        const COutputRecord *oR = rtx->GetOutput(r.i);
        if (!oR) {
            return werror("%s: GetOutput failed, %s, %d.\n", r.txhash.ToString(), r.i);
        }

        CAmount nV = oR->GetRawValue();
        std::pair<CAmount,std::pair<MapRecords_t::const_iterator,unsigned int> > coin = std::make_pair(nV, std::make_pair(rtxi, r.i));

        if (nV == nTargetValue) {
            setCoinsRet.push_back(coin.second);
            nValueRet += coin.first;
            return true;
        } else if (nV < nTargetValue + MIN_CHANGE) {
            vValue.push_back(coin);
            nTotalLower += nV;
        } else if (nV < coinLowestLarger.first) {
            coinLowestLarger = coin;
        }
    }

    if (nTotalLower == nTargetValue) {
        for (unsigned int i = 0; i < vValue.size(); ++i) {
            setCoinsRet.push_back(vValue[i].second);
            nValueRet += vValue[i].first;
        }

        return true;
    }

    if (nTotalLower < nTargetValue) {
        if (coinLowestLarger.second.first == mapRecords.end())
            return false;

        setCoinsRet.push_back(coinLowestLarger.second);
        nValueRet += coinLowestLarger.first;
        return true;
    }

    // Solve subset sum by stochastic approximation
    std::sort(vValue.begin(), vValue.end(), CompareValueOnly());
    std::reverse(vValue.begin(), vValue.end());
    std::vector<char> vfBest;
    CAmount nBest;

    ApproximateBestSubset(vValue, nTotalLower, nTargetValue, vfBest, nBest);
    if (nBest != nTargetValue && nTotalLower >= nTargetValue + MIN_CHANGE)
        ApproximateBestSubset(vValue, nTotalLower, nTargetValue + MIN_CHANGE, vfBest, nBest);

    // If we have a bigger coin and (either the stochastic approximation didn't find a good solution,
    //                                   or the next bigger coin is closer), return the bigger coin
    if (coinLowestLarger.second.first != mapRecords.end() &&
        ((nBest != nTargetValue && nBest < nTargetValue + MIN_CHANGE) || coinLowestLarger.first <= nBest)) {
        setCoinsRet.push_back(coinLowestLarger.second);
        nValueRet += coinLowestLarger.first;
    } else {
        for (unsigned int i = 0; i < vValue.size(); i++)
            if (vfBest[i]) {
                setCoinsRet.push_back(vValue[i].second);
                nValueRet += vValue[i].first;
            }
    }

    return true;
}

/**
 * Outpoint is spent if any non-conflicted transaction
 * spends it:
 */
bool AnonWallet::IsSpent(const uint256& hash, unsigned int n) const
{
    const COutPoint outpoint(hash, n);
    std::pair<TxSpends::const_iterator, TxSpends::const_iterator> range;
    range = mapTxSpends.equal_range(outpoint);

    if (range.first == range.second) {
        // the outpoint was not in the Tx Spends map.  check it directly
        MapRecords_t::const_iterator mi = mapRecords.find(hash);
        if (mi != mapRecords.end()) {
            const COutputRecord *outputRecord = mi->second.GetOutput(n);
            if (outputRecord != nullptr)
                return outputRecord->IsSpent();
        }
        // If we got this far, assume it's spent
        return true;
    }

    for (TxSpends::const_iterator it = range.first; it != range.second; ++it) {
        const uint256 &wtxid = it->second;

        MapRecords_t::const_iterator rit = mapRecords.find(wtxid);
        if (rit == mapRecords.end())
            return true; // Could not find, consider spent

        const COutputRecord* outRecord = rit->second.GetOutput(n);
        if (!outRecord)
            return true; // Could not find the record, so just consider it spent.
        if (outRecord->IsSpent())
            return true;
        if (rit->second.IsAbandoned())
            continue;

        int depth = GetDepthInMainChain(rit->second.blockHash, rit->second.nIndex);
        if (depth >= 0)
            return true; // Spent
    }

    return false;
}

std::set<uint256> AnonWallet::GetConflicts(const uint256 &txid) const
{
    std::set<uint256> result;
    AssertLockHeld(pwalletParent->cs_wallet);

    MapRecords_t::const_iterator mri = mapRecords.find(txid);

    if (mri != mapRecords.end()) {
        const CTransactionRecord &rtx = mri->second;
        std::pair<TxSpends::const_iterator, TxSpends::const_iterator> range;

        if (!(rtx.nFlags & ORF_ANON_IN))
        for (const auto &prevout : rtx.vin) {
            if (mapTxSpends.count(prevout) <= 1)
                continue;  // No conflict if zero or one spends

            range = mapTxSpends.equal_range(prevout);
            for (TxSpends::const_iterator _it = range.first; _it != range.second; ++_it)
                result.insert(_it->second);
        }

        return result;
    }

    return pwalletParent->GetConflicts(txid);
}

void AnonWallet::MarkConflicted(const uint256 &hashBlock, const uint256 &hashTx)
{
    LOCK2(cs_main, pwalletParent->cs_wallet);

    int conflictconfirms = 0;

    BlockMap::iterator mi = mapBlockIndex.find(hashTx);
    if (mi != mapBlockIndex.end()) {
        if (chainActive.Contains(mi->second))
            conflictconfirms = -(chainActive.Height() - mi->second->nHeight + 1);
    }

    // If number of conflict confirms cannot be determined, this means
    // that the block is still unknown or not yet part of the main chain,
    // for example when loading the wallet during a reindex. Do nothing in that
    // case.
    if (conflictconfirms >= 0)
        return;

    // Do not flush the wallet here for performance reasons
    AnonWalletDB walletdb(*walletDatabase, "r+", false);

    MapRecords_t::iterator mri;
    std::set<uint256> todo, done;
    todo.insert(hashTx);

    size_t nChangedRecords = 0;
    while (!todo.empty()) {
        uint256 now = *todo.begin();
        todo.erase(now);
        done.insert(now);

        if ((mri = mapRecords.find(now)) != mapRecords.end()) {
            CTransactionRecord &rtx = mri->second;
            int currentconfirm = GetDepthInMainChain(rtx.blockHash, rtx.nIndex);

            if (conflictconfirms < currentconfirm) {
                // Block is 'more conflicted' than current confirm; update.
                // Mark transaction as conflicted with this block.
                rtx.nIndex = -1;
                rtx.blockHash = hashBlock;
                walletdb.WriteTxRecord(now, rtx);

                // Iterate over all its outputs, and mark transactions in the wallet that spend them conflicted too
                TxSpends::const_iterator iter = mapTxSpends.lower_bound(COutPoint(now, 0));
                while (iter != mapTxSpends.end() && iter->first.hash == now) {
                     if (!done.count(iter->second))
                         todo.insert(iter->second);
                     iter++;
                }
            }

            nChangedRecords++;
            continue;
        }

        LogPrintf("%s: Warning txn %s not recorded in wallet.\n", __func__, now.ToString());
    }

//    if (nChangedRecords > 0) // HACK, alternative is to load CStoredTransaction to get vin
//        MarkDirty();
}

bool AnonWallet::GetPrevout(const COutPoint &prevout, CTxOutBaseRef &txout)
{
    MapRecords_t::const_iterator mir = mapRecords.find(prevout.hash);
    if (mir != mapRecords.end()) {
        const COutputRecord *oR = mir->second.GetOutput(prevout.n);

        if (oR && oR->nType == OUTPUT_STANDARD) { // get outputs other than standard from the utxodb or chain instead
            txout = MAKE_OUTPUT<CTxOutStandard>(oR->GetAmount(), oR->scriptPubKey);
            return true;
        }
    }

    return false;
}
