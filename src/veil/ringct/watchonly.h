// Copyright (c) 2021 The Flux Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.


#ifndef VEIL_WATCHONLY_H
#define VEIL_WATCHONLY_H

class CBitcoinAddress;
class CWatchOnlyAddress;
class CKeyID;
class uint256;
//class CPubKey;
class CTransaction;
class CWatchOnlyDB;
class UniValue;


#include <vector>
#include <map>
#include <uint256.h>
#include <key.h>
#include <primitives/transaction.h>
#include <veil/ringct/types.h>

// Forward declare CWatchOnlyTx for use in CWatchOnlyTxCache
class CWatchOnlyTx;

// Forward declare stealth functions
int SetPublicKey(const CPubKey &pk, ec_point &out);

/** Map of watchonly keys */
extern std::map<CKeyID, CWatchOnlyAddress> mapWatchOnlyAddresses;

/** Global variable that points to the active watchonly database (protected by cs_main) */
extern std::unique_ptr<CWatchOnlyDB> pwatchonlyDB;

/** Transaction write cache for batching database writes */
struct CWatchOnlyTxCache {
    std::map<CKey, std::vector<CWatchOnlyTx>> mapPendingTxes;
    std::map<CKey, int> mapPendingCounts;
    std::map<CKey, int64_t> mapHighestBlockHeight; // Track highest block height per key for checkpoints
    std::map<CKey, uint256> mapHighestBlockHash;   // Track block hash for checkpoints
    size_t nMaxCacheSize = 1000; // Fixed cache size (TOTAL across all addresses)
    size_t nTotalCached = 0;     // Running counter of total cached transactions (O(1) access)
    CCriticalSection cs_cache;

    bool ShouldFlush(const CKey& key);
    void AddTx(const CKey& key, const CWatchOnlyTx& tx);
    void GetCachedTxes(const CKey& key, std::vector<CWatchOnlyTx>& vTxes); // Get all cached txes for a key
    bool Flush(const CKey& key);
    bool FlushAll();
    size_t GetSize(const CKey& key);
    size_t GetTotalSize(); // Get total cached transactions across all keys (O(1))
};

/** Block transaction cache for batching block index writes during scanning */
struct CWatchOnlyBlockCache {
    std::map<int64_t, std::vector<CTxOutWatchonly>> mapPendingBlocks;
    size_t nMaxBlocks = 10000; // Cache up to 10,000 blocks before flushing (huge performance boost on initial scan)
    CCriticalSection cs_blockcache;

    void AddBlock(int64_t nHeight, const std::vector<CTxOutWatchonly>& vTxes);
    bool ShouldFlush();
    bool FlushAll();
    size_t GetSize();
};

extern CWatchOnlyTxCache watchonlyTxCache;
extern CWatchOnlyBlockCache watchonlyBlockCache;

//class CWatchOnlyTX
//{
//public:
//    CWatchOnlyAddress(const CKey& scan, const CPubKey& spend);
//
//    CKey scan_secret;
//    CPubKey spend_pubkey;
//};

void LinkWatchOnlyThreadGroup(void* pthreadgroup);
void StopWatchonlyScanningThread();
bool StartWatchonlyScanningThread();
bool StartWatchonlyScanningIfNotStarted();
void ScanWatchOnlyAddresses();

class CWatchOnlyAddress
{
public:
    CWatchOnlyAddress();
    CWatchOnlyAddress(const CKey& scan, const CPubKey& spend, const int64_t& nStartScanning, const int64_t& nImported, const int64_t& nScannedHeight);

    CKey scan_secret;
    CPubKey spend_pubkey;
    int64_t nScanStartHeight;
    int64_t nImportedHeight;
    int64_t nCurrentScannedHeight;

    //Memory only -
    bool fDirty = false;

    ADD_SERIALIZE_METHODS;
    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream &s, Operation ser_action)
    {
        READWRITE(scan_secret);
        READWRITE(spend_pubkey);
        READWRITE(nScanStartHeight);
        READWRITE(nImportedHeight);
        READWRITE(nCurrentScannedHeight);
    };
};

// Precomputed structure to avoid repeated key derivations during scanning
struct CWatchOnlyAddressPrecomputed {
    CKeyID keyID;            // V2: Use CKeyID as map key
    CWatchOnlyAddress address;
    ec_point ecSpendPubKey;  // Pre-computed once during initialization

    CWatchOnlyAddressPrecomputed() {}
    CWatchOnlyAddressPrecomputed(const CKeyID& id, const CWatchOnlyAddress& addr)
        : keyID(id), address(addr)
    {
        SetPublicKey(address.spend_pubkey, ecSpendPubKey);
    }
};

class CWatchOnlyTx
{
public:
    CWatchOnlyTx(const CKey& key, const uint256& txhash);
    CWatchOnlyTx();

    enum {
        NOTSET = -1,
        STEALTH = 0,
        ANON = 1
    };

    int type;
    CKey scan_secret;
    uint256 tx_hash;
    int tx_index;
    CTxOutRingCT ringctout;
    CTxOutCT ctout;

    // Block information (for fast confirmation/time lookup)
    int64_t nBlockHeight;  // Block height where tx was found (0 if unknown/old db)
    int64_t nBlockTime;    // Block timestamp (0 if unknown/old db)

    //Memory only - Amount (For testing only)
    CAmount nAmount;
    uint256 blind;
    int64_t ringctIndex;

    ADD_SERIALIZE_METHODS;
    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream &s, Operation ser_action)
    {
        READWRITE(type);
        READWRITE(scan_secret);
        READWRITE(tx_hash);
        READWRITE(tx_index);
        if (type == STEALTH) {
            READWRITE(ctout);
        } else if (type == ANON) {
            READWRITE(ringctout);
        }

        // V2: Block information (backward compatible - defaults to 0 for old databases)
        try {
            READWRITE(nBlockHeight);
            READWRITE(nBlockTime);
        } catch (...) {
            // Old database format - set defaults
            if (ser_action.ForRead()) {
                nBlockHeight = 0;
                nBlockTime = 0;
            }
        }
    };

    UniValue GetUniValue(int& index, bool spent = false, std::string keyimage = "", uint256 txhash = uint256(), bool fSkip = true, CAmount amount = 0, int confirmations = -1, int64_t blocktime = 0, std::string rawtx = "") const;
};

class CWatchOnlyTxWithIndex
{
public:
    CWatchOnlyTxWithIndex(){}

    int64_t ringctindex;
    CWatchOnlyTx watchonlytx;


    ADD_SERIALIZE_METHODS;
    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream &s, Operation ser_action)
    {
        READWRITE(ringctindex);
        READWRITE(watchonlytx);
    };
};

/** Watchonly address methods */
bool AddWatchOnlyAddress(const std::string& address, const CKey& scan_secret, const CPubKey& spend_pubkey, const int64_t& nStart, const int64_t& nImported);
bool RemoveWatchOnlyAddress(const std::string& address, const CKey& scan_secret, const CPubKey& spend_pubkey);
bool LoadWatchOnlyAddresses();
bool FlushWatchOnlyAddresses();


/** Watchonly address transaction methods */
bool GetWatchOnlyAddressTransactions(const CBitcoinAddress& address, std::vector<uint256>& txhashses);
bool AddWatchOnlyTransaction(const CKey& key,const CWatchOnlyTx& watchonlytx);
bool ReadWatchOnlyTransaction(const CKey& key, const int& count, CWatchOnlyTx& watchonlytx);
void FetchWatchOnlyTransactions(const CKey& scan_secret, std::vector<std::pair<int, CWatchOnlyTx>>& vTxes, int nStartFromIndex = -1, int nStopIndex = -1);

/** Atomic checkpoint write for crash recovery */
bool WriteWatchOnlyCheckpoint(const CKey& scan_secret, const std::vector<CWatchOnlyTx>& vTxes, int64_t nHeight, const uint256& blockHash);


/* Watchonly adddress transaction count */
bool IncrementWatchOnlyKeyCount(const CKey& key);
bool GetWatchOnlyKeyCount(const CKey& key, int& current_count);

bool GetSecretFromString(const std::string& strSecret, CKey& secret);
bool GetPubkeyFromString(const std::string& strPubkey, CPubKey& pubkey);
bool GetAmountFromWatchonly(const CWatchOnlyTx& watchonlytx, const CKey& scan_secret, const CKey& spend_secret, const CPubKey& spend_pubkey, CAmount& nAmount, uint256& blindOut, CCmpPubKey& keyImage);


bool CheckBlockForWatchOnlyTx();

#endif //VEIL_WATCHONLY_H
