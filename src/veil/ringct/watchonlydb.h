// Copyright (c) 2021 The Flux Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.


#ifndef VEIL_WATCHONLYDB_H
#define VEIL_WATCHONLYDB_H


#include <coins.h>
#include <dbwrapper.h>
#include <chain.h>
#include <veil/ringct/rctindex.h>
#include <primitives/block.h>
#include <libzerocoin/Coin.h>
#include <libzerocoin/CoinSpend.h>

#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

class CBlockIndex;
class CCoinsViewDBCursor;
class uint256;
class CBitcoinAddress;
class CKeyID;
class CWatchOnlyTx;
class CWatchOnlyAddress;

/** Database version constants */
static const int WATCHONLY_DB_VERSION_1 = 1;  // Original string-based keys
static const int WATCHONLY_DB_VERSION_2 = 2;  // Hash-based keys (CKeyID)
static const int WATCHONLY_DB_CURRENT = WATCHONLY_DB_VERSION_2;

/** Checkpoint structure for crash recovery */
struct CWatchOnlyScanCheckpoint {
    CKey scan_secret;
    int64_t nBlockHeight;
    int nTxCount;
    uint256 blockHash;
    int64_t nTimestamp;

    CWatchOnlyScanCheckpoint() : nBlockHeight(0), nTxCount(0), nTimestamp(0) {}

    ADD_SERIALIZE_METHODS;
    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action)
    {
        READWRITE(scan_secret);
        READWRITE(nBlockHeight);
        READWRITE(nTxCount);
        READWRITE(blockHash);
        READWRITE(nTimestamp);
    }
};

/** A database (watchonly/) */
class CWatchOnlyDB : public CDBWrapper
{
public:
    explicit CWatchOnlyDB(size_t nCacheSize, bool fMemory = false, bool fWipe = false);

private:
    CWatchOnlyDB(const CWatchOnlyDB&);
    void operator=(const CWatchOnlyDB&);

public:
    /** V1 Methods - Legacy string-based keys */
    bool WriteWatchOnlyAddress(const std::string& address, const CWatchOnlyAddress& data);
    bool ReadWatchOnlyAddress(const std::string& address, CWatchOnlyAddress& data);
    bool LoadWatchOnlyAddresses();

    /** V2 Methods - Hash-based keys (CKeyID) */
    bool WriteWatchOnlyAddressV2(const CKeyID& keyID, const CWatchOnlyAddress& data);
    bool ReadWatchOnlyAddressV2(const CKeyID& keyID, CWatchOnlyAddress& data);
    bool EraseWatchOnlyAddressV2(const CKeyID& keyID);

    /** Erase all data for a watch-only address (address, transactions, count, checkpoint) */
    bool EraseWatchOnlyAddressData(const CKeyID& keyID, const CKey& scan_secret);

    /** Database version management */
    int GetDatabaseVersion();
    bool SetDatabaseVersion(int version);
    bool MigrateToV2();

    bool WriteWatchOnlyTx(const CKey& key, const int& current_count, const CWatchOnlyTx& watchonlytx);
    bool ReadWatchOnlyTx(const CKey& key, const int& count, CWatchOnlyTx& watchonlytx);

    /** Bulk write method - writes multiple transactions in a single batch */
    bool WriteBulkWatchOnlyTx(const CKey& key, int starting_count, const std::vector<CWatchOnlyTx>& vTxes);

    bool ReadKeyCount(const CKey& key, int& current_count);
    bool WriteKeyCount(const CKey& key, const int& new_count);

    bool ReadBlockTransactions(const int64_t& nBlockHeight, std::vector<CTxOutWatchonly>& vTransactions);
    bool WriteBlockTransactions(const int64_t& blockheight, const std::vector<CTxOutWatchonly>& vTransactions);

    /** Bulk write method - writes multiple blocks in a single batch */
    bool WriteBulkBlockTransactions(const std::map<int64_t, std::vector<CTxOutWatchonly>>& mapBlocks);

    /** Checkpoint methods for atomic crash recovery */
    bool WriteCheckpoint(const CKey& key, const CWatchOnlyScanCheckpoint& checkpoint);
    bool ReadCheckpoint(const CKey& key, CWatchOnlyScanCheckpoint& checkpoint);

    /** Database maintenance */
    void CompactDatabase();
};

#endif //VEIL_WATCHONLYDB_H
