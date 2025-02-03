// Copyright (c) 2017-2019 The Particl Core developers
// Copyright (c) 2018-2019 Veil developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef PARTICL_WALLET_HDWALLET_H
#define PARTICL_WALLET_HDWALLET_H

#include <memory>

#include <wallet/wallet.h>
#include <wallet/walletbalances.h>
#include <veil/ringct/anonwalletdb.h>
#include <veil/ringct/rpcanonwallet.h>
#include <veil/ringct/extkey.h>
#include <veil/ringct/temprecipient.h>
#include <veil/ringct/outputrecord.h>
#include <veil/ringct/transactionrecord.h>

#include <key_io.h>
#include <veil/ringct/stealth.h>

typedef std::map<CKeyID, CStealthKeyMetadata> StealthKeyMetaMap;
typedef std::map<CKeyID, CExtKeyAccount*> ExtKeyAccountMap;
typedef std::map<CKeyID, CStoredExtKey*> ExtKeyMap;

typedef std::map<uint256, CWalletTx> MapWallet_t;

typedef std::multimap<int64_t, std::map<uint256, CTransactionRecord>::iterator> RtxOrdered_t;

class UniValue;
class CWatchOnlyTx;

const uint16_t OR_PLACEHOLDER_N = 0xFFFF; // index of a fake output to contain reconstructed amounts for txns with undecodeable outputs

class CStoredTransaction
{
public:
    CTransactionRef tx;
    std::vector<std::pair<int, uint256> > vBlinds;

    bool InsertBlind(int n, const uint8_t *p)
    {
        for (auto &bp : vBlinds)
        {
            if (bp.first == n)
            {
                memcpy(bp.second.begin(), p, 32);
                return true;
            };
        };
        uint256 insert;
        memcpy(insert.begin(), p, 32);
        vBlinds.push_back(std::make_pair(n, insert));
        return true;
    };

    bool GetBlind(int n, uint8_t *p) const
    {
        for (auto &bp : vBlinds)
        {
            if (bp.first == n)
            {
                memcpy(p, bp.second.begin(), 32);
                return true;
            };
        };
        return false;
    };

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action)
    {
        READWRITE(tx);
        READWRITE(vBlinds);
    };
};

// A class that holds reference to a pending tx, so it can mark inputs
// as no longer pending spend and delete the transaction if the transaction is not
// broadcast successfully.
class CPendingSpend
{
public:
    CPendingSpend(AnonWallet& wallet, uint256 txhash) : wallet(wallet), txhash(txhash) {}
    ~CPendingSpend();

    void SetSuccess(bool s) { success = s; }

private:
    AnonWallet& wallet;
    uint256 txhash;
    bool success;
};

class AnonWallet
{
    std::shared_ptr<WalletDatabase> walletDatabase;
    std::shared_ptr<CWallet> pwalletParent;
    std::map<CKeyID, uint32_t> mapAccountCounter;
    std::map<CKeyID, std::pair<CKeyID, BIP32Path> > mapKeyPaths; //childKey->[accountId, derivedPathFromAccount]

    std::map<CKeyID, CStealthAddress> mapStealthAddresses;
    std::map<CKeyID, CKeyID> mapStealthDestinations; // [stealthdest, stealth addr] Destinations created by external wallets that are derived from our wallet's stealth address

    std::unique_ptr<CExtKey> pkeyMaster;
    CKeyID idMaster;
    CKeyID idDefaultAccount;
    CKeyID idStealthAccount;
    CKeyID idChangeAccount;
    CKeyID idChangeAddress;
    CKeyID idStakeAccount;
    CKeyID idStakeAddress;

    typedef std::multimap<COutPoint, uint256> TxSpends;
    TxSpends mapTxSpends;

public:
    AnonWallet(std::shared_ptr<CWallet> pwallet, std::string name, std::shared_ptr<WalletDatabase> dbw_in)
    {
        this->walletDatabase = dbw_in;
        this->pwalletParent = pwallet;
        mapTxSpends.clear();
    };

    ~AnonWallet()
    {
        Finalise();
    }

    std::shared_ptr<CWallet> GetParent() { return pwalletParent; }

    bool IsLocked() const;
    void Lock();

    std::map<CTxDestination, CAddressBookData> mapAddressBook;
    std::string GetName() const { return "ringctwallet"; }
    CKeyID GetDefaultAccount() const { return idDefaultAccount; }
    CKeyID GetMasterID() const { return idMaster; }

    int Finalise();

    bool Initialise(CExtKey* pExtMaster = nullptr);

    void AddToSpends(const COutPoint& outpoint, const uint256& wtxid);

    isminetype HaveAddress(const CTxDestination &dest) const;

    bool HaveTransaction(const uint256 &txhash) const;

    bool GetKey(const CKeyID &address, CKey &keyOut) const;

    bool GetPubKey(const CKeyID &address, CPubKey &pkOut) const;

    isminetype HaveStealthAddress(const CStealthAddress &sxAddr) const;
    bool GetStealthAddressScanKey(CStealthAddress &sxAddr) const;
    bool GetStealthAddressSpendKey(CStealthAddress &sxAddr, CKey &key) const;
    bool GetAddressMeta(const CStealthAddress& address, CKeyID& idAccount, std::string& strPath) const;
    int GetLastUsedAddressIndex(const CKeyID& idAccount) const;
    void ForgetUnusedStealthAddresses(int nBuffer);

    bool ImportStealthAddress(const CStealthAddress &sxAddr, const CKey &skSpend);
    bool RestoreAddresses(int nCount);

    std::map<CTxDestination, CAmount> GetAddressBalances() EXCLUSIVE_LOCKS_REQUIRED(cs_main);

    isminetype IsMine(const CTxIn& txin) const;
    isminetype IsMine(const CTxOutBase *txout) const;
    bool IsMine(const CTransaction& tx) const;
    bool IsFromMe(const CTransaction& tx) const;


    /**
     * Returns amount of debit if the input matches the
     * filter, otherwise returns 0
     */
    CAmount GetDebit(const CTxIn& txin, const isminefilter& filter) const;
    CAmount GetDebit(const CTransaction& tx, const isminefilter& filter) const;
    CAmount GetDebit(AnonWalletDB *pwdb, const CTransactionRecord &rtx, const isminefilter& filter) const;

    /** Returns whether all of the inputs match the filter */
    bool IsAllFromMe(const CTransaction& tx, const isminefilter& filter) const;

    CAmount GetCredit(const CTransaction &tx, const isminefilter &filter) const;
    CAmount GetCredit(const COutPoint& outpoint, const isminefilter& filter) const;

    void GetCredit(const CTransaction &tx, CAmount &nSpendable, CAmount &nWatchOnly) const;

    bool GetOutputRecord(const COutPoint& outpoint, COutputRecord& record) const;
    CAmount GetOutputValue(const COutPoint &op, bool fAllowTXIndex);
    CAmount GetOwnedOutputValue(const COutPoint &op, isminefilter filter);

    int GetDepthInMainChain(const uint256 &blockhash, int nIndex = 0) const;
    bool InMempool(const uint256 &hash) const;
    bool IsTrusted(const uint256 &hash, const uint256 &blockhash, int nIndex = 0) const;

    CAmount GetBalance(const isminefilter& filter=ISMINE_SPENDABLE, const int min_depth=0) const;
    CAmount GetSpendableBalance() const;        // Includes watch_only_cs balance
    CAmount GetUnconfirmedBalance() const;
    CAmount GetBlindBalance(const int min_depth=0);
    CAmount GetAnonBalance(const int min_depth=0);

    bool GetBalances(BalanceList &bal);
//    CAmount GetAvailableBalance(const CCoinControl* coinControl = nullptr) const;
    CAmount GetAvailableAnonBalance(const CCoinControl* coinControl = nullptr) const;
    CAmount GetAvailableBlindBalance(const CCoinControl* coinControl = nullptr) const;


    bool IsChange(const CTxOutBase *txout) const;

    void AddOutputRecordMetaData(CTransactionRecord &rtx, std::vector<CTempRecipient> &vecSend);
    bool ExpandTempRecipients(std::vector<CTempRecipient> &vecSend, std::string &sError);
    void MarkInputsAsPendingSpend(const std::vector<COutPoint>& rtxvin);

    bool AddCTData(CTxOutBase *txout, CTempRecipient &r, std::string &sError);

    bool SetChangeDest(const CCoinControl *coinControl, CTempRecipient &r, std::string &sError);

    /** Update wallet after successful transaction */
    bool SaveRecord(const uint256& txid, const CTransactionRecord& rtx);
    int AddStandardInputs(CWalletTx &wtx, CTransactionRecord &rtx, std::vector<CTempRecipient> &vecSend, bool sign,
            CAmount &nFeeRet, const CCoinControl *coinControl, std::string &sError, bool fZerocoinInputs, CAmount nInputValue);
    int AddStandardInputs_Inner(CWalletTx &wtx, CTransactionRecord &rtx, std::vector<CTempRecipient> &vecSend, bool sign,
            CAmount &nFeeRet, const CCoinControl *coinControl, std::string &sError, bool fZerocoinInputs, CAmount nInputValue);

    int AddBlindedInputs(CWalletTx &wtx, CTransactionRecord &rtx, std::vector<CTempRecipient> &vecSend, bool sign,
            size_t nMaximumInputs, CAmount &nFeeRet, const CCoinControl *coinControl, std::string &sError);
    int AddBlindedInputs_Inner(CWalletTx &wtx, CTransactionRecord &rtx, std::vector<CTempRecipient> &vecSend, bool sign,
            size_t nMaximumInputs, CAmount &nFeeRet, const CCoinControl *coinControl, std::string &sError);


    bool PlaceRealOutputs(std::vector<std::vector<int64_t> > &vMI, size_t &nSecretColumn, size_t nRingSize, std::set<int64_t> &setHave,
        const std::vector<std::pair<MapRecords_t::const_iterator,unsigned int> > &vCoins, std::vector<uint8_t> &vInputBlinds, std::string &sError);
    bool PickHidingOutputs(std::vector<std::vector<int64_t> > &vMI, size_t nSecretColumn, size_t nRingSize, std::set<int64_t> &setHave,
         std::string &sError);

    bool GetRandomHidingOutputs(size_t nInputSize, size_t nRingSize, std::set<int64_t> &setHave, std::vector<CLightWalletAnonOutputData>& randomoutputs, std::string &sError);

    bool AddCoinbaseRewards(CMutableTransaction& txCoinbase, CAmount nStakeReward, std::string& sError);

    bool IsMyAnonInput(const CTxIn& txin, COutPoint& myOutpoint);
    bool IsMyAnonInput(const CTxIn& txin, COutPoint& myOutpoint, CKey& key);
    bool AddAnonInputs_Inner(CWalletTx &wtx, CTransactionRecord &rtx, std::vector<CTempRecipient> &vecSend,
         bool sign, size_t nRingSize, size_t nInputsPerSig, size_t nMaximumInputs, CAmount &nFeeRet,
         const CCoinControl *coinControl, std::string &sError, bool fZerocoinInputs, CAmount nInputValue);
    bool AddAnonInputs(CWalletTx &wtx, CTransactionRecord &rtx, std::vector<CTempRecipient> &vecSend,
         bool sign, size_t nRingSize, size_t nInputsPerSig, size_t nMaximumInputs, CAmount &nFeeRet,
         const CCoinControl *coinControl, std::string &sError, bool fZerocoinInputs = false,
         CAmount nInputValue = 0);

    void GetAllScanKeys(std::vector<CStealthAddress>& vStealthAddresses);
    bool IsMyPubKey(const CKeyID& keyId);

    void LoadToWallet(const uint256 &hash, const CTransactionRecord &rtx);
    bool LoadTxRecords();

    /** Remove txn from mapwallet and TxSpends */
    void RemoveFromTxSpends(const uint256 &hash, const CTransactionRef pt);
    int UnloadTransaction(const uint256 &hash);

    bool MakeDefaultAccount(const CExtKey& extKeyMaster);
    bool CreateStealthChangeAccount(AnonWalletDB* wdb);
    bool CreateStealthStakeAccount(AnonWalletDB* wdb);
    bool SetMasterKey(const CExtKey& keyMasterIn);
    bool UnlockWallet(const CExtKey& keyMasterIn, bool fRescan = true);
    bool LoadAccountCounters();
    bool LoadKeys();
    CKeyID GetSeedHash() const;
    int GetStealthAccountCount() const;

    bool HaveKeyID(const CKeyID& id);
    bool NewKeyFromAccount(const CKeyID &idAccount, CKey& key);
    bool NewExtKeyFromAccount(const CKeyID& idAccount, CExtKey& keyDerive, CKey& key);
    bool CreateAccountWithKey(const CExtKey& key);
    bool RegenerateKey(const CKeyID& idKey, CKey& key) const;
    bool RegenerateExtKey(const CKeyID& idKey, CExtKey& extkey) const;
    bool RegenerateAccountExtKey(const CKeyID& idAccount, CExtKey& keyAccount) const;
    bool RegenerateKeyFromIndex(const CKeyID& idAccount, int nIndex, CExtKey& keyDerive) const;
    bool MakeSigningKeystore(CBasicKeyStore& keystore, const CScript& scriptPubKey);

    bool NewStealthKey(CStealthAddress& stealthAddress, uint32_t nPrefixBits, const char *pPrefix, CKeyID* paccount = nullptr);
    CStealthAddress GetStealthChangeAddress();
    CStealthAddress GetStealthStakeAddress();

    /**
     * Insert additional inputs into the transaction by
     * calling CreateTransaction();
     */
    int LoadStealthAddresses();
    bool AddStealthDestination(const CKeyID& idStealthAddress, const CKeyID& idStealthDestination);
    bool AddStealthDestinationMeta(const CKeyID& idStealth, const CKeyID& idStealthDestination, std::vector<uint8_t> &vchEphemPK);
    bool AddKeyToParent(const CKey& keySharedSecret);
    bool CalculateStealthDestinationKey(const CKeyID& idStealthSpend, const CKeyID& idStealthDestination, const CKey& sShared, CKey& keyDestination) const;
    bool RecordOwnedStealthDestination(const CKey& sShared, const CKeyID& idStealth, const CKeyID& destStealth);
    bool GetStealthLinked(const CKeyID &stealthDest, CStealthAddress &sx) const;
    bool GetStealthAddress(const CKeyID& idStealth, CStealthAddress& stealthAddress);
    bool HaveStealthDestination(const CKeyID& destStealth) { return mapStealthDestinations.count(destStealth) > 0; }
    bool ProcessLockedStealthOutputs();
    bool ProcessLockedBlindedOutputs();
    bool ProcessStealthOutput(const CTxDestination &address,
        std::vector<uint8_t> &vchEphemPK, uint32_t prefix, bool fHavePrefix, CKey &sShared, CWatchOnlyTx& watchOnlyOutput, bool fNeedShared=false);

    int CheckForStealthAndNarration(const CTxOutBase *pb, const CTxOutData *pdata, std::string &sNarr);
    bool FindStealthTransactions(const CTransaction &tx, mapValue_t &mapNarr);

    bool ScanForOwnedOutputs(const CTransaction &tx, size_t &nCT, size_t &nRingCT, mapValue_t &mapNarr, std::vector<CWatchOnlyTx>& vecWatchOnlyTx);
    bool AddToWalletIfInvolvingMe(const CTransactionRef& ptx, const CBlockIndex* pIndex, int posInBlock, bool fUpdate);
    void MarkOutputSpent(const COutPoint& outpoint, bool isSpent);

    std::unique_ptr<CPendingSpend> GetPendingSpendForTx(uint256 txid);
    void DeletePendingTx(uint256 txid);

    enum RescanWalletType {
      RESCAN_WALLET_FULL,
      RESCAN_WALLET_LOCKED_ONLY,
    };
    template<RescanWalletType = RESCAN_WALLET_FULL>
    void RescanWallet() EXCLUSIVE_LOCKS_REQUIRED(cs_main, pwalletParent->cs_wallet);

    int InsertTempTxn(const uint256 &txid, const CTransactionRecord *rtx) const;

    bool GetCTBlindsFromOutput(const CTxOutBase *pout, uint256& blind) const;
    bool GetCTBlinds(CKeyID idKey, std::vector<uint8_t>& vData, secp256k1_pedersen_commitment* commitment, std::vector<uint8_t>& vRangeproof, uint256 &blind, int64_t& nValue) const;
    bool OwnBlindOut(AnonWalletDB *pwdb, const uint256 &txhash, const CTxOutCT *pout, COutputRecord &rout, CStoredTransaction &stx, bool &fUpdated);
    int OwnAnonOut(AnonWalletDB *pwdb, const uint256 &txhash, const CTxOutRingCT *pout, COutputRecord &rout, CStoredTransaction &stx, bool &fUpdated);

    bool AddTxinToSpends(const CTxIn &txin, const uint256 &txhash);

    bool ProcessPlaceholder(AnonWalletDB *pwdb, const CTransaction &tx, CTransactionRecord &rtx);
    bool AddToRecord(CTransactionRecord &rtxIn, const CTransaction &tx,
        const CBlockIndex *pIndex, int posInBlock, bool fFlushOnClose=true);

    std::vector<uint256> ResendRecordTransactionsBefore(int64_t nTime, CConnman *connman) EXCLUSIVE_LOCKS_REQUIRED(cs_main);

    /**
     * populate vCoins with vector of available COutputs.
     */
    void AvailableBlindedCoins(std::vector<COutputR>& vCoins, bool fOnlySafe=true, const CCoinControl *coinControl = nullptr, const CAmount& nMinimumAmount = 1, const CAmount& nMaximumAmount = MAX_MONEY, const CAmount& nMinimumSumAmount = MAX_MONEY, const uint64_t& nMaximumCount = 0, const int& nMinDepth = 0, const int& nMaxDepth = 0x7FFFFFFF, bool fIncludeImmature=false) const;
    /**
     * Returns a list of coins for a single transaction based on the target value and allowed number of inputs.
     */
    bool SelectBlindedCoins(const std::vector<COutputR>& vAvailableCoins, const CAmount& nTargetValue, size_t nMaximumCount, std::vector<std::pair<MapRecords_t::const_iterator,unsigned int> > &setCoinsRet, CAmount &nValueRet, const CCoinControl *coinControl = nullptr) const;

    void AvailableAnonCoins(std::vector<COutputR> &vCoins, bool fOnlySafe=true, const CCoinControl *coinControl = nullptr, const CAmount& nMinimumAmount = 1, const CAmount& nMaximumAmount = MAX_MONEY, const CAmount& nMinimumSumAmount = MAX_MONEY, const uint64_t& nMaximumCount = 0, const int& nMinDepth = 0, const int& nMaxDepth = 0x7FFFFFFF, bool fIncludeImmature=false) const;

    /**
     * Return list of available coins and locked coins grouped by non-change output address.
     */

    bool SelectCoinsMinConf(const CAmount& nTargetValue, const CoinEligibilityFilter& eligibility_filter, std::vector<COutputR> &vCoins, std::vector<std::pair<MapRecords_t::const_iterator,unsigned int> > &setCoinsRet, CAmount &nValueRet) const;
    /**
     * Like SelectCoinsMinConf, but always selects at most nMaximumCount coins,
     * with the intention of being put in a single tx that partially accomplishes the target to be sent.
     */
    bool SelectCoinsForOneTx(const CAmount& nTargetValue, const CoinEligibilityFilter& eligibility_filter, std::vector<COutputR> &vCoins, size_t nMaximumCount, std::vector<std::pair<MapRecords_t::const_iterator,unsigned int> > &setCoinsRet, CAmount &nValueRet) const;

    bool IsSpent(const uint256& hash, unsigned int n) const EXCLUSIVE_LOCKS_REQUIRED(cs_main);

    std::set<uint256> GetConflicts(const uint256 &txid) const;

    /* Mark a transaction (and it in-wallet descendants) as abandoned so its inputs may be respent. */
 //   bool AbandonTransaction(const uint256 &hashTx);

    void MarkConflicted(const uint256 &hashBlock, const uint256 &hashTx);

    /* Return a prevout if it exists in the wallet. */
    bool GetPrevout(const COutPoint &prevout, CTxOutBaseRef &txout);

    boost::signals2::signal<void (CAmount nReservedBalance)> NotifyReservedBalanceChanged;

    uint32_t nStealth, nFoundStealth; // for reporting, zero before use

    mutable int m_greatest_txn_depth = 0; // depth of most deep txn
    //mutable int m_least_txn_depth = 0; // depth of least deep txn
    mutable bool m_have_spendable_balance_cached = false;
    mutable CAmount m_spendable_balance_cached = 0;

    mutable MapWallet_t mapTempWallet;

    MapRecords_t mapRecords;
    std::set<uint256> mapLockedRecords; // Keep track of locked transactions (hidden value) for faster unlocking
    RtxOrdered_t rtxOrdered;
    mutable MapRecords_t mapTempRecords; // Hack for sending unmined inputs through fundrawtransactionfrom

    int64_t nRCTOutSelectionGroup1 = 2400;
    int64_t nRCTOutSelectionGroup2 = 24000;

private:
    std::string GetDisplayName() const { return "ringctwallet"; }
    void ParseAddressForMetaData(const CTxDestination &addr, COutputRecord &rec);

    bool ArrangeBlinds(
        std::vector<CTxIn>& vin,
        std::vector<ec_point>& vInputBlinds,
        std::vector<std::vector<std::vector<int64_t>>>& vMI,
        std::vector<size_t>& vSecretColumns,
        std::vector<std::pair<MapRecords_t::const_iterator, unsigned int>>& setCoins,
        bool dummySigs,
        std::string& sError);
    bool GetKeyImage(
        CTxIn& txin,
        std::vector<std::vector<int64_t>>& vMI,
        size_t& secretColumn,
        std::string& sError);
    bool SetBlinds(
        size_t nSigRingSize,
        size_t nSigInputs,
        std::vector<std::vector<int64_t>>& vMI,
        std::vector<CKey>& vsk,
        std::vector<const uint8_t*>& vpsk,
        std::vector<uint8_t>& vm,
        std::vector<secp256k1_pedersen_commitment>& vCommitments,
        std::vector<const uint8_t*>& vpInCommits,
        std::vector<const uint8_t*>& vpBlinds,
        ec_point& vInputBlinds,
        size_t& secretColumn,
        std::string& sError);
    bool SetOutputs(
        std::vector<CTxOutBaseRef>& vpout,
        std::vector<CTempRecipient>& vecSend,
        CAmount nFeeRet,
        size_t nSubtractFeeFromAmount,
        CAmount& nValueOutPlain,
        int& nChangePosInOut,
        bool fSkipFee,
        bool fFeesFromChange,
        bool fAddFeeDataOutput,
        bool fUseBlindSum,
        std::string& sError);
    bool ArrangeOutBlinds(
        std::vector<CTxOutBaseRef>& vpout,
        std::vector<CTempRecipient>& vecSend,
        std::vector<const uint8_t*>& vpOutCommits,
        std::vector<const uint8_t*>& vpOutBlinds,
        std::vector<uint8_t>& vBlindPlain,
        secp256k1_pedersen_commitment* plainCommitment,
        CAmount nValueOutPlain,
        int nChangePosInOut,
        bool fCTOut,
        std::string& sError);

    void InternalResetSpent(AnonWalletDB& wdb, CTransactionRecord* txrecord)
        EXCLUSIVE_LOCKS_REQUIRED(cs_main, pwalletParent->cs_wallet);

    template<typename... Params>
    bool werror(std::string fmt, Params... parameters) const {
        return error(("%s " + fmt).c_str(), GetDisplayName(), parameters...);
    };
    template<typename... Params>
    int werrorN(int rv, std::string fmt, Params... parameters) const {
        return errorN(rv, ("%s " + fmt).c_str(), GetDisplayName(), parameters...);
    };
    template<typename... Params>
    int wserrorN(int rv, std::string &s, const char *func, std::string fmt, Params... parameters) const {
        return errorN(rv, s, func, ("%s " + fmt).c_str(), GetDisplayName(), parameters...);
    }
};

bool KeyIdFromScriptPubKey(const CScript& script, CKeyID& id);

bool CheckOutputValue(const CTempRecipient &r, const CTxOutBase *txbout, CAmount nFeeRet, std::string sError);
void SetCTOutVData(std::vector<uint8_t> &vData, const CPubKey &pkEphem, uint32_t nStealthPrefix);
int CreateOutput(OUTPUT_PTR<CTxOutBase> &txbout, CTempRecipient &r, std::string &sError);

// Calculate the size of the transaction assuming all signatures are max size
// Use DummySignatureCreator, which inserts 72 byte signatures everywhere.
// NOTE: this requires that all inputs must be in mapWallet (eg the tx should
// be IsAllFromMe).
int64_t CalculateMaximumSignedTxSize(const CTransaction &tx, const AnonWallet *wallet);
int64_t CalculateMaximumSignedTxSize(const CTransaction &tx, const AnonWallet *wallet, const std::vector<CTxOutBaseRef>& txouts);


#endif // PARTICL_WALLET_HDWALLET_H

