// Copyright (c) 2018-2019 The Bitcoin Core developers
// Copyright (c) 2018-2019 The Particl developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <interfaces/wallet.h>

#include <amount.h>
#include <chain.h>
#include <consensus/validation.h>
#include <interfaces/handler.h>
#include <net.h>
#include <policy/feerate.h>
#include <policy/fees.h>
#include <policy/policy.h>
#include <primitives/transaction.h>
#include <script/ismine.h>
#include <script/standard.h>
#include <support/allocators/secure.h>
#include <sync.h>
#include <timedata.h>
#include <ui_interface.h>
#include <uint256.h>
#include <validation.h>
#include <veil/ringct/anonwallet.h>
#include <veil/zerocoin/zchain.h>
#include <wallet/feebumper.h>
#include <wallet/fees.h>
#include <wallet/wallet.h>

namespace interfaces {
namespace {

class PendingWalletTxImpl : public PendingWalletTx
{
public:
    PendingWalletTxImpl(CWallet& wallet) : m_wallet(wallet), m_key(&wallet) {}

    const CTransaction& get() override { return *m_tx; }

    int64_t getVirtualSize() override
    {
        if (!m_tx)
            return 0;
        return GetVirtualTransactionSize(*m_tx);
    }

    bool commit(WalletValueMap value_map,
        WalletOrderForm order_form,
        std::string& reject_reason) override
    {
        LOCK2(cs_main, m_wallet.cs_wallet);
        CValidationState state;

        if (!m_wallet.CommitTransaction(m_tx, std::move(value_map), std::move(order_form), &m_key, g_connman.get(), state)) {
            reject_reason = state.GetRejectReason();
            return false;
        }
        return true;
    }

    CTransactionRef m_tx;
    CWallet& m_wallet;
    CReserveKey m_key;
};

//! Construct wallet tx struct.
WalletTx MakeWalletTx(CWallet& wallet, const CWalletTx& wtx)
{
    std::list<COutputEntry> listReceived;
    std::list<COutputEntry> listSent;
    CAmount nFee;
    wtx.GetAmounts(listReceived, listSent, nFee, ISMINE_ALL);
    auto panonwallet = wallet.GetAnonWallet();

    WalletTx result;
    result.tx = wtx.tx;
    result.value_map = wtx.mapValue;
    auto txid = wtx.tx->GetHash();
    if (wtx.tx->HasBlindedValues()) {
        if (panonwallet->mapRecords.count(txid)) {
            result.rtx = panonwallet->mapRecords.at(txid);
            result.has_rtx = true;
        }
    }

    result.txin_is_mine.reserve(wtx.tx->vin.size());
    bool fInputsFromMe = false;
    for (const auto& txin : wtx.tx->vin) {
        auto inputmine = wallet.IsMine(txin);
        result.txin_is_mine.emplace_back(inputmine);
        if (inputmine)
            fInputsFromMe = true;
    }
    result.txout_is_mine.reserve(wtx.tx->vpout.size());
    if (!result.has_rtx)
        result.txout_address.reserve(wtx.tx->vpout.size());
    else
        result.txout_address.reserve(result.rtx.vout.size());
    result.txout_address_is_mine.reserve(wtx.tx->vpout.size());
    for (unsigned int i = 0; i < wtx.tx->vpout.size(); i++) {
        auto txout = wtx.tx->vpout[i];
        auto ismine = wallet.IsMine(txout.get());
        result.txout_is_mine.emplace_back(ismine);
        CScript scriptPubKey;
        auto fAddressIsMine = ismine;
        if (txout->GetScriptPubKey(scriptPubKey)) {
            CTxDestination dest;
            bool fSuccess = ExtractDestination(scriptPubKey, dest);
            if (fSuccess)
                result.txout_address.emplace_back(dest);
            result.txout_address_is_mine.emplace_back(fSuccess ? IsMine(wallet, dest) : ISMINE_NO);

            //Check if this is a stealth destination, if so include stealth address
            if (dest.type() == typeid(CKeyID)) {
                auto idStealthDestination = boost::get<CKeyID>(dest);
                if (panonwallet->HaveStealthDestination(idStealthDestination)) {
                    CStealthAddress stealthAddress;
                    if (panonwallet->GetStealthLinked(idStealthDestination, stealthAddress)) {
                        result.value_map[strprintf("stealth:%d", i)] = stealthAddress.ToString(true);
                    }
                }
            }

        } else {
            if (txout->nVersion == OUTPUT_DATA && fInputsFromMe)
                fAddressIsMine = ISMINE_SPENDABLE;
            result.txout_address_is_mine.emplace_back(fAddressIsMine);
        }

        /** RingCT/CT/Data Output **/
        if (!txout->IsStandardOutput()) {
            bool found = false;
            if (txout->nVersion == OUTPUT_DATA) {
                //Likely the fee for ringct
                auto dataout = (CTxOutData*)txout.get();
                CAmount nFee;
                if (dataout->GetCTFee(nFee)) {
                    found = true;
                    result.map_anon_value_out.emplace(i, nFee);
                    result.ct_fee.first = i;
                    result.ct_fee.second = nFee;
                }
            } else {
                for (const COutputEntry& entry : listSent) {
                    if (entry.vout == (int)i) {
                        found = true;
                        result.map_anon_value_out.emplace(i, entry.amount);
                        result.is_anon_send = true;
                        break;
                    }
                }
                if (!found) {
                    for (const COutputEntry& entry : listReceived) {
                        if (entry.vout == (int)i) {
                            result.map_anon_value_out.emplace(i, entry.amount);
                            found = true;
                            result.is_anon_recv = true;
                            break;
                        }
                    }
                }
            }
            //Add a dummy, not sure what else to do here...
            if (!found)
                result.map_anon_value_out.emplace(i, DUMMY_VALUE);
        }
    }

    // Get txout addresses a bit differently for transactions with blinded values
    if (result.has_rtx) {
        for (unsigned int i = 0; i < result.rtx.vout.size(); i++) {
            auto &out = result.rtx.vout[i];
            if (out.IsChange()) {
                result.txout_address.emplace_back(CNoDestination());
                continue;
            }

            CTxDestination address = CNoDestination();
            if (out.vPath.size() > 0) {
                if (out.vPath[0] == ORA_STEALTH) {
                    if (out.vPath.size() < 5) {
                        LogPrintf("%s: Warning, malformed vPath.\n", __func__);
                    } else {
                        CKeyID id;
                        CStealthAddress sx;

                        if (out.GetStealthID(id) && wallet.GetAnonWallet()->GetStealthAddress(id, sx))
                            address = sx;
                    };
                };
            } else if (address.type() == typeid(CNoDestination))
                ExtractDestination(out.scriptPubKey, address);

            result.txout_address.emplace_back(address);
        }
    }

    result.credit = wtx.GetCredit(ISMINE_ALL);
    result.debit = wtx.GetDebit(ISMINE_ALL);
    result.change = wtx.GetChange();
    result.time = wtx.GetTxTime();
    result.is_coinbase = wtx.IsCoinBase();
    result.is_coinstake = wtx.IsCoinStake();

    CzTracker* pzTracker = wallet.GetZTrackerPointer();
    result.is_my_zerocoin_mint = pzTracker->HasMintTx(wtx.GetHash());
    result.is_my_zerocoin_spend = false;
    if (wtx.IsZerocoinSpend()) {
        auto spend = TxInToZerocoinSpend(wtx.tx->vin[0]);
        if (spend)
            result.is_my_zerocoin_spend = wallet.IsMyZerocoinSpend(spend->getCoinSerialNumber());
    }

    result.computetime = wtx.nComputeTime;

    return result;
}
/*
//! Construct wallet tx struct.
WalletTx MakeWalletTx(CHDWallet& wallet, MapRecords_t::const_iterator irtx)
{
    WalletTx result;
    result.is_record = true;
    result.irtx = irtx;
    result.time = irtx->second.GetTxTime();
    result.veilWallet = &wallet;

    result.is_coinbase = false;
    result.is_coinstake = false;

    return result;
}
*/
//! Construct wallet tx status struct.
WalletTxStatus MakeWalletTxStatus(const CWalletTx& wtx)
{
    LOCK(cs_main);
    WalletTxStatus result;
    auto mi = ::mapBlockIndex.find(wtx.hashBlock);
    CBlockIndex* block = mi != ::mapBlockIndex.end() ? mi->second : nullptr;
    result.block_height = (block ? block->nHeight : std::numeric_limits<int>::max());
    result.blocks_to_maturity = wtx.GetBlocksToMaturity();
    result.depth_in_main_chain = wtx.GetDepthInMainChain();
    result.time_received = wtx.nTimeReceived;
    result.lock_time = wtx.tx->nLockTime;
    result.is_final = CheckFinalTx(*wtx.tx);
    result.is_trusted = wtx.IsTrusted();
    result.is_abandoned = wtx.isAbandoned();
    result.is_coinbase = wtx.IsCoinBase();
    result.is_in_main_chain = wtx.IsInMainChain();
    return result;
}

/*
WalletTxStatus MakeWalletTxStatus(AnonWallet* pAnonWallet, const uint256 &hash, const CTransactionRecord &rtx)
{
    WalletTxStatus result;
    auto mi = ::mapBlockIndex.find(rtx.blockHash);

    CBlockIndex* block = mi != ::mapBlockIndex.end() ? mi->second : nullptr;
    result.block_height = (block ? block->nHeight : std::numeric_limits<int>::max()),

            result.blocks_to_maturity = 0;
    result.depth_in_main_chain = pAnonWallet->GetDepthInMainChain(rtx.blockHash, rtx.nIndex);
    result.time_received = rtx.nTimeReceived;
    result.lock_time = 0; // TODO
    result.is_final = true; // TODO
    result.is_trusted = pAnonWallet->IsTrusted(hash, rtx.blockHash);
    result.is_abandoned = rtx.IsAbandoned();
    result.is_coinbase = false;
    result.is_in_main_chain = result.depth_in_main_chain > 0;
    return result;
}
*/

//! Construct wallet TxOut struct.
WalletTxOut MakeWalletTxOut(CWallet& wallet, const CWalletTx& wtx, int n, int depth)
{
    WalletTxOut result;
    result.pout = wtx.tx->vpout[n];
    result.time = wtx.GetTxTime();
    result.depth_in_main_chain = depth;
    result.is_spent = wallet.IsSpent(wtx.GetHash(), n);
    return result;
}

class WalletImpl : public Wallet
{
public:
    WalletImpl(const std::shared_ptr<CWallet>& wallet) : m_shared_wallet(wallet), m_wallet(*wallet.get()) {}

    bool encryptWallet(const SecureString& wallet_passphrase) override
    {
        //AnonWallet * pHDWallet = (AnonWallet *) &m_wallet;
        //return pHDWallet->EncryptWallet(wallet_passphrase);
        return m_wallet.EncryptWallet(wallet_passphrase);
    }
    bool isCrypted() override { return m_wallet.IsCrypted(); }
    bool lock() override { return m_wallet.LockWallet(); }
    bool unlock(const SecureString& wallet_passphrase, bool fUnlockForStakingOnly) override { return m_wallet.Unlock(wallet_passphrase, fUnlockForStakingOnly); }
    bool isLocked() override { return m_wallet.IsLocked(); }
    bool isUnlockedForStakingOnly() override { return m_wallet.IsUnlockedForStakingOnly(); }
    bool changeWalletPassphrase(const SecureString& old_wallet_passphrase,
        const SecureString& new_wallet_passphrase) override
    {
        return m_wallet.ChangeWalletPassphrase(old_wallet_passphrase, new_wallet_passphrase);
    }
    void setStakingEnabled(bool fEnableStaking) override { m_wallet.SetStakingEnabled(fEnableStaking); }
    bool isStakingEnabled() override { return m_wallet.IsStakingEnabled(); }

    void abortRescan() override { m_wallet.AbortRescan(); }
    bool backupWallet(const std::string& filename) override { return m_wallet.BackupWallet(filename); }
    std::string getWalletName() override { return m_wallet.GetName(); }
    bool getKeyFromPool(bool internal, CPubKey& pub_key) override
    {
        return m_wallet.GetKeyFromPool(pub_key, internal);
    }
    bool getPubKey(const CKeyID& address, CPubKey& pub_key) override { return m_wallet.GetPubKey(address, pub_key); }
    bool getPrivKey(const CKeyID& address, CKey& key) override { return m_wallet.GetKey(address, key); }
    bool getPrivKey(CStealthAddress& address, CKey& key) override
    {
        if (m_wallet.GetAnonWallet()->GetStealthAddressScanKey(address)) {
            key = address.scan_secret;
            return true;
        }
        return false;
    }
    bool isSpendable(const CTxDestination& dest) override { return IsMine(m_wallet, dest) & ISMINE_SPENDABLE; }

    std::string mintZerocoin(CAmount nValue, std::vector<CDeterministicMint>& vDMints, OutputTypes inputtype,
            const CCoinControl* coinControl) override
    {
        CWalletTx wtx(&m_wallet, nullptr);
        return m_wallet.MintZerocoin(nValue, wtx , vDMints, inputtype,coinControl);
    }

    bool getMint(const uint256& serialHash, CZerocoinMint& mint) override
    {
        return m_wallet.GetMint(serialHash, mint);
    }

    std::unique_ptr<PendingWalletTx> prepareZerocoinSpend(CAmount nValue, int nSecurityLevel,
            CZerocoinSpendReceipt& receipt, std::vector<CZerocoinMint>& vMintsSelected, bool fMintChange,
            bool fMinimizeChange, std::vector<CommitData>& vCommitData, libzerocoin::CoinDenomination denomFilter,
            CTxDestination* addressTo = NULL) override
    {
        auto pending = MakeUnique<PendingWalletTxImpl>(m_wallet);
        if (!m_wallet.PrepareZerocoinSpend(nValue, nSecurityLevel, receipt, vMintsSelected, fMintChange,
                fMinimizeChange, vCommitData, libzerocoin::CoinDenomination::ZQ_ERROR, addressTo))
            return {};
        auto vtx = receipt.GetTransactions();
        pending->m_tx = vtx[0];
        return std::move(pending);
    }

    bool commitZerocoinSpend(CZerocoinSpendReceipt& receipt, std::vector<CommitData>& vCommitData, int computeTime) override
    {
        return m_wallet.CommitZerocoinSpend(receipt, vCommitData, computeTime);
    }

    bool haveWatchOnly() override { return m_wallet.HaveWatchOnly(); };
    bool setAddressBook(const CTxDestination& dest, const std::string& name, const std::string& purpose, bool bench32) override
    {
        return m_wallet.SetAddressBook(dest, name, purpose, bench32);
    }
    bool delAddressBook(const CTxDestination& dest) override
    {
        // Don't delete my addresses in any circumstance..
        if(!IsMine(m_wallet, dest)){
            return m_wallet.DelAddressBook(dest);
        }
        std::cout << "Wallet trying to remove a receiving address.. this cannot happen" << std::endl;
        return false;
    }
    bool getAddress(const CTxDestination& dest,
        std::string* name,
        isminetype* is_mine,
        std::string* purpose) override
    {
        LOCK(m_wallet.cs_wallet);
        auto it = m_wallet.mapAddressBook.find(dest);
        if (it == m_wallet.mapAddressBook.end()) {
            return false;
        }
        if (name) {
            *name = it->second.name;
        }
        if (is_mine) {
            *is_mine = IsMine(m_wallet, dest);
        }
        if (purpose) {
            *purpose = it->second.purpose;
        }
        return true;
    }

    std::vector<WalletAddress> getLabelAddress(const std::string label) override{
        LOCK(m_wallet.cs_wallet);
        std::vector<WalletAddress> result;
        for(const CTxDestination ctx : m_wallet.GetLabelAddresses(label)){
            std::string name;
            std::string purpose;
            this->getAddress(ctx,&name, nullptr,&purpose);
            result.emplace_back(ctx, IsMine(m_wallet, ctx), name, purpose);
        }
        return result;
    }

    std::vector<WalletAddress> getAddresses() override
    {
        LOCK(m_wallet.cs_wallet);
        std::vector<WalletAddress> result;
        for (const auto& item : m_wallet.mapAddressBook) {
            result.emplace_back(item.first, IsMine(m_wallet, item.first), item.second.name, item.second.purpose);
        }
        return result;
    }
    std::vector<WalletAddress> getAddresses(bool IsMineAddresses) override
    {
        LOCK(m_wallet.cs_wallet);
        std::vector<WalletAddress> result;
        for (const auto& item : m_wallet.mapAddressBook) {
            if(IsMine(m_wallet, item.first) == IsMineAddresses)
                result.emplace_back(item.first, IsMine(m_wallet, item.first), item.second.name, item.second.purpose);
        }
        return result;
    }

    std::vector<WalletAddress> getStealthAddresses(bool IsMineAddresses) override
    {
        LOCK(m_wallet.cs_wallet);
        std::vector<WalletAddress> result;
        for (const auto& item : m_wallet.mapAddressBook) {
            if(IsMine(m_wallet, item.first) == IsMineAddresses && item.second.purpose == "stealth_receive")
                result.emplace_back(item.first, IsMine(m_wallet, item.first), item.second.name, item.second.purpose);
        }
        return result;
    }

    void learnRelatedScripts(const CPubKey& key, OutputType type) override { m_wallet.LearnRelatedScripts(key, type); }
    bool addDestData(const CTxDestination& dest, const std::string& key, const std::string& value) override
    {
        LOCK(m_wallet.cs_wallet);
        return m_wallet.AddDestData(dest, key, value);
    }
    bool eraseDestData(const CTxDestination& dest, const std::string& key) override
    {
        LOCK(m_wallet.cs_wallet);
        return m_wallet.EraseDestData(dest, key);
    }
    std::vector<std::string> getDestValues(const std::string& prefix) override
    {
        return m_wallet.GetDestValues(prefix);
    }
    void lockCoin(const COutPoint& output) override
    {
        LOCK2(cs_main, m_wallet.cs_wallet);
        return m_wallet.LockCoin(output);
    }
    void unlockCoin(const COutPoint& output) override
    {
        LOCK2(cs_main, m_wallet.cs_wallet);
        return m_wallet.UnlockCoin(output);
    }
    bool isLockedCoin(const COutPoint& output) override
    {
        LOCK2(cs_main, m_wallet.cs_wallet);
        return m_wallet.IsLockedCoin(output.hash, output.n);
    }
    void listLockedCoins(std::vector<COutPoint>& outputs) override
    {
        LOCK2(cs_main, m_wallet.cs_wallet);
        return m_wallet.ListLockedCoins(outputs);
    }
    std::unique_ptr<PendingWalletTx> createTransaction(std::vector<CTempRecipient>& recipients,
        const CCoinControl& coin_control,
        bool sign,
        int& change_pos,
        CAmount& fee,
        OutputTypes inputType,
        std::string& fail_reason) override
    {
        LOCK2(cs_main, m_wallet.cs_wallet);
        auto pending = MakeUnique<PendingWalletTxImpl>(m_wallet);
        size_t nRingSize = Params().DefaultRingSize();
        size_t nInputsPerSig = 32;

        CTransactionRef tx_new;
        auto pwalletAnon = m_wallet.GetAnonWallet();

        CWalletTx wtx(&m_wallet, tx_new);
        CTransactionRecord rtx;

        CAmount nFeeRet = 0;
        bool fFailed = false;
        bool fCheckFeeOnly = false;
        switch (inputType) {
            case OUTPUT_STANDARD:
            {
                if (0 !=
                    pwalletAnon->AddStandardInputs(wtx, rtx, recipients, !fCheckFeeOnly, nFeeRet, &coin_control, fail_reason, false, 0))
                    fFailed = true;
                break;
            }
            case OUTPUT_CT:
                if (0 != pwalletAnon->AddBlindedInputs(wtx, rtx, recipients, !fCheckFeeOnly, nFeeRet, &coin_control, fail_reason))
                    fFailed = true;
                break;
            case OUTPUT_RINGCT:
                if (!pwalletAnon->AddAnonInputs(wtx, rtx, recipients, !fCheckFeeOnly, nRingSize,
                                                nInputsPerSig, nFeeRet, &coin_control, fail_reason))
                    fFailed = true;
                break;
            default:
                fFailed = true;
        }

        if (fFailed) {
            fail_reason = "Failed to add inputs";
            return {};
        }

        CValidationState state;
        CReserveKey reservekey(&m_wallet);

        pending->m_tx = wtx.tx;
//        if (!m_wallet.CommitTransaction(wtx.tx, wtx.mapValue, wtx.vOrderForm, reservekey, g_connman.get(), state)) {
//            fail_reason = "Transaction commit failed";
//        }

        fee = nFeeRet;

//        if (!m_wallet.CreateTransaction(recipients, pending->m_tx, pending->m_key, fee, change_pos,
//                fail_reason, coin_control, sign)) {
//            return {};
//        }
        return std::move(pending);
    }
    bool transactionCanBeAbandoned(const uint256& txid) override { return m_wallet.TransactionCanBeAbandoned(txid); }
    bool abandonTransaction(const uint256& txid) override
    {
        LOCK2(cs_main, m_wallet.cs_wallet);
        return m_wallet.AbandonTransaction(txid);
    }
    bool transactionCanBeBumped(const uint256& txid) override
    {
        return feebumper::TransactionCanBeBumped(&m_wallet, txid);
    }
    bool createBumpTransaction(const uint256& txid,
        const CCoinControl& coin_control,
        CAmount total_fee,
        std::vector<std::string>& errors,
        CAmount& old_fee,
        CAmount& new_fee,
        CMutableTransaction& mtx) override
    {
        return feebumper::CreateTransaction(&m_wallet, txid, coin_control, total_fee, errors, old_fee, new_fee, mtx) ==
               feebumper::Result::OK;
    }
    bool signBumpTransaction(CMutableTransaction& mtx) override { return feebumper::SignTransaction(&m_wallet, mtx); }
    bool commitBumpTransaction(const uint256& txid,
        CMutableTransaction&& mtx,
        std::vector<std::string>& errors,
        uint256& bumped_txid) override
    {
        return feebumper::CommitTransaction(&m_wallet, txid, std::move(mtx), errors, bumped_txid) ==
               feebumper::Result::OK;
    }
    CTransactionRef getTx(const uint256& txid) override
    {
        LOCK2(::cs_main, m_wallet.cs_wallet);
        auto mi = m_wallet.mapWallet.find(txid);
        if (mi != m_wallet.mapWallet.end()) {
            return mi->second.tx;
        }
        return {};
    }
    WalletTx getWalletTx(const uint256& txid) override
    {
        LOCK2(::cs_main, m_wallet.cs_wallet);
        auto mi = m_wallet.mapWallet.find(txid);
        if (mi != m_wallet.mapWallet.end()) {
            return MakeWalletTx(m_wallet, mi->second);
        }

        return {};
    }
    std::vector<WalletTx> getWalletTxs() override
    {
        LOCK2(::cs_main, m_wallet.cs_wallet);
        std::vector<WalletTx> result;
        result.reserve(m_wallet.mapWallet.size());
        for (const auto& entry : m_wallet.mapWallet) {
            result.emplace_back(MakeWalletTx(m_wallet, entry.second));
        }
        /*
        if (m_shared_wallet.get()) {
            CHDWallet *phdwallet = (CHDWallet*)(m_shared_wallet.get());
            for (auto mi = phdwallet->mapRecords.begin(); mi != phdwallet->mapRecords.end(); mi++) {
                result.emplace_back(MakeWalletTx(*phdwallet, mi));
            }
        }
         */
        return result;
    }
    bool tryGetTxStatus(const uint256& txid,
        interfaces::WalletTxStatus& tx_status,
        int& num_blocks,
        int64_t& adjusted_time) override
    {
        TRY_LOCK(::cs_main, locked_chain);
        if (!locked_chain) {
            return false;
        }
        TRY_LOCK(m_wallet.cs_wallet, locked_wallet);
        if (!locked_wallet) {
            return false;
        }
        auto mi = m_wallet.mapWallet.find(txid);
        if (mi == m_wallet.mapWallet.end()) {
            return false;
        }
        num_blocks = ::chainActive.Height();
        adjusted_time = GetAdjustedTime();
        tx_status = MakeWalletTxStatus(mi->second);
        return true;
    }
    WalletTx getWalletTxDetails(const uint256& txid,
        WalletTxStatus& tx_status,
        WalletOrderForm& order_form,
        bool& in_mempool,
        int& num_blocks,
        int64_t& adjusted_time) override
    {
        LOCK2(::cs_main, m_wallet.cs_wallet);
        auto mi = m_wallet.mapWallet.find(txid);
        if (mi != m_wallet.mapWallet.end()) {
            num_blocks = ::chainActive.Height();
            adjusted_time = GetAdjustedTime();
            in_mempool = mi->second.InMempool();
            order_form = mi->second.vOrderForm;
            tx_status = MakeWalletTxStatus(mi->second);
            return MakeWalletTx(m_wallet, mi->second);
        }
/*
        if (m_shared_wallet.get()) {
            CHDWallet *phdwallet = (CHDWallet*)(m_shared_wallet.get());
            auto mi = phdwallet->mapRecords.find(txid);
            if (mi != phdwallet->mapRecords.end())
            {
                num_blocks = ::chainActive.Height();
                adjusted_time = GetAdjustedTime();
                in_mempool = phdwallet->InMempool(mi->first);
                order_form = {};
                tx_status = MakeWalletTxStatus(*phdwallet, mi->first, mi->second);
                return MakeWalletTx(*phdwallet, mi);
            }
        }
*/
        return {};
    }
    WalletBalances getBalances() override
    {
        WalletBalances result;
        BalanceList walletBalances;
        m_wallet.GetBalances(walletBalances);

        result.basecoin_balance = walletBalances.nVeil;
        result.basecoin_unconfirmed_balance = walletBalances.nVeilUnconf;
        result.basecoin_immature_balance = walletBalances.nVeilImmature;
        result.ct_balance = walletBalances.nCT;
        result.ct_unconfirmed_balance = walletBalances.nCTUnconf;
        result.ct_immature_balance = walletBalances.nCTImmature;
        result.ring_ct_balance = walletBalances.nRingCT;
        result.ring_ct_unconfirmed_balance = walletBalances.nRingCTUnconf;
        result.ring_ct_immature_balance = walletBalances.nRingCTImmature;
        result.zerocoin_balance = walletBalances.nZerocoin;
        result.zerocoin_unconfirmed_balance = walletBalances.nZerocoinUnconf;
        result.zerocoin_immature_balance = walletBalances.nZerocoinImmature;
        result.have_watch_only = walletBalances.nVeilWatchOnly || walletBalances.nVeilWatchOnlyUnconf;
        if (result.have_watch_only) {
            result.watch_only_balance = walletBalances.nVeilWatchOnly;
            result.unconfirmed_watch_only_balance = walletBalances.nVeilWatchOnlyUnconf;
            result.immature_watch_only_balance = m_wallet.GetImmatureWatchOnlyBalance();
        }

        result.total_balance = result.basecoin_balance + result.ct_balance + result.ring_ct_balance + result.zerocoin_balance;
        result.total_immature_balance = result.basecoin_immature_balance + result.ct_immature_balance +
                result.ring_ct_immature_balance + result.zerocoin_immature_balance;
        result.total_unconfirmed_balance = result.basecoin_unconfirmed_balance + result.ct_unconfirmed_balance +
                result.ring_ct_unconfirmed_balance + result.zerocoin_unconfirmed_balance;

        //Total balance should include any immature or unconfirmed
        result.total_balance += result.total_immature_balance;

        return result;
    }
    bool tryGetBalances(WalletBalances& balances, int& num_blocks) override
    {
        TRY_LOCK(cs_main, locked_chain);
        if (!locked_chain) return false;
        TRY_LOCK(m_wallet.cs_wallet, locked_wallet);
        if (!locked_wallet) {
            return false;
        }
        balances = getBalances();
        num_blocks = ::chainActive.Height();
        return true;
    }
    CAmount getBalance() override { return m_wallet.GetBalance(); }
    CAmount getAvailableBalance(const CCoinControl& coin_control) override
    {
        return m_wallet.GetAvailableBalance(&coin_control);
    }
    CAmount getAvailableCTBalance(const CCoinControl& coin_control) override
    {
        if (!m_wallet.GetAnonWallet())
            return 0;
        return m_wallet.GetAnonWallet()->GetAvailableBlindBalance(&coin_control);
    }
    CAmount getAvailableRingCTBalance(const CCoinControl& coin_control) override
    {
        if (!m_wallet.GetAnonWallet()) {
            std::cout << "Couldn't get anon wallet" << std::endl;
            return 0;
        }
        return m_wallet.GetAnonWallet()->GetAvailableAnonBalance(&coin_control);
    }
    CAmount getAvailableZerocoinBalance(const CCoinControl& coin_control) override
    {
        return m_wallet.GetAvailableZerocoinBalance(&coin_control);
    }
    CWallet* getWalletPointer() override
    {
        return m_shared_wallet.get();
    }
    isminetype txinIsMine(const CTxIn& txin) override
    {
        LOCK2(::cs_main, m_wallet.cs_wallet);
        return m_wallet.IsMine(txin);
    }
    isminetype txoutbaseIsMine(const CTxOutBase* txout) override
    {
        LOCK2(::cs_main, m_wallet.cs_wallet);
        return m_wallet.IsMine(txout);
    }
    isminetype txoutIsMine(const CTxOut& txout) override
    {
        LOCK2(::cs_main, m_wallet.cs_wallet);
        return m_wallet.IsMine(txout);
    }
    CAmount getDebit(const CTxIn& txin, isminefilter filter) override
    {
        LOCK2(::cs_main, m_wallet.cs_wallet);
        return m_wallet.GetDebit(txin, filter);
    }
    CAmount getCredit(const CTxOut& txout, isminefilter filter) override
    {
        LOCK2(::cs_main, m_wallet.cs_wallet);
        return m_wallet.GetCredit(txout, filter);
    }
    CAmount getAnonCredit(const COutPoint& outpoint, isminefilter filter) override
    {
        LOCK2(::cs_main, m_wallet.cs_wallet);
        return m_wallet.GetAnonCredit(outpoint, filter);
    }
    bool getNewStealthAddress(CStealthAddress& address) override
    {
        LOCK(m_wallet.cs_wallet);
        return m_wallet.GetAnonWallet()->NewStealthKey(address, 0, nullptr);
    }
    CoinsList listCoins() override
    {
        LOCK2(::cs_main, m_wallet.cs_wallet);
        CoinsList result;
        for (const auto& entry : m_wallet.ListCoins()) {
            auto& group = result[entry.first];
            for (const auto& coin : entry.second) {
                group.emplace_back(
                    COutPoint(coin.tx->GetHash(), coin.i), MakeWalletTxOut(m_wallet, *coin.tx, coin.i, coin.nDepth));
            }
        }
        return result;
    }
    std::vector<WalletTxOut> getCoins(const std::vector<COutPoint>& outputs) override
    {
        LOCK2(::cs_main, m_wallet.cs_wallet);
        std::vector<WalletTxOut> result;
        result.reserve(outputs.size());
        for (const auto& output : outputs) {
            result.emplace_back();
            auto it = m_wallet.mapWallet.find(output.hash);
            if (it != m_wallet.mapWallet.end()) {
                int depth = it->second.GetDepthInMainChain();
                if (depth >= 0) {
                    result.back() = MakeWalletTxOut(m_wallet, it->second, output.n, depth);
                }
            }
        }
        return result;
    }
    CAmount getRequiredFee(unsigned int tx_bytes) override { return GetRequiredFee(m_wallet, tx_bytes); }
    CAmount getMinimumFee(unsigned int tx_bytes,
        const CCoinControl& coin_control,
        int* returned_target,
        FeeReason* reason) override
    {
        FeeCalculation fee_calc;
        CAmount result;
        result = GetMinimumFee(m_wallet, tx_bytes, coin_control, ::mempool, ::feeEstimator, &fee_calc);
        if (returned_target) *returned_target = fee_calc.returnedTarget;
        if (reason) *reason = fee_calc.reason;
        return result;
    }
    unsigned int getConfirmTarget() override { return m_wallet.m_confirm_target; }
    bool hdEnabled() override { return m_wallet.IsHDEnabled(); }
    bool IsWalletFlagSet(uint64_t flag) override { return m_wallet.IsWalletFlagSet(flag); }
    OutputType getDefaultAddressType() override { return m_wallet.m_default_address_type; }
    OutputType getDefaultChangeType() override { return m_wallet.m_default_change_type; }
    std::unique_ptr<Handler> handleUnload(UnloadFn fn) override
    {
        return MakeHandler(m_wallet.NotifyUnload.connect(fn));
    }
    std::unique_ptr<Handler> handleShowProgress(ShowProgressFn fn) override
    {
        return MakeHandler(m_wallet.ShowProgress.connect(fn));
    }
    std::unique_ptr<Handler> handleStatusChanged(StatusChangedFn fn) override
    {
        return MakeHandler(m_wallet.NotifyStatusChanged.connect([fn](CCryptoKeyStore*) { fn(); }));
    }
    std::unique_ptr<Handler> handleAddressBookChanged(AddressBookChangedFn fn) override
    {
        return MakeHandler(m_wallet.NotifyAddressBookChanged.connect(
            [fn](CWallet*, const CTxDestination& address, const std::string& label, bool is_mine,
                const std::string& purpose, ChangeType status) { fn(address, label, is_mine, purpose, status); }));
    }
    std::unique_ptr<Handler> handleTransactionChanged(TransactionChangedFn fn) override
    {
        return MakeHandler(m_wallet.NotifyTransactionChanged.connect(
            [fn](CWallet*, const uint256& txid, ChangeType status) { fn(txid, status); }));
    }
    std::unique_ptr<Handler> handleWatchOnlyChanged(WatchOnlyChangedFn fn) override
    {
        return MakeHandler(m_wallet.NotifyWatchonlyChanged.connect(fn));
    }

    std::shared_ptr<CWallet> m_shared_wallet;
    CWallet& m_wallet;
};

} // namespace

std::unique_ptr<Wallet> MakeWallet(const std::shared_ptr<CWallet>& wallet) { return MakeUnique<WalletImpl>(wallet); }

} // namespace interfaces
