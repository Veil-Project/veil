// Copyright (c) 2021 The Flux Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.


#include "watchonlydb.h"
#include <chainparams.h>
#include <hash.h>
#include <random.h>
#include <pow.h>
#include <shutdown.h>
#include <uint256.h>
#include <rpc/util.h>
#include <ui_interface.h>

#include <stdint.h>

#include <boost/thread.hpp>
#include <primitives/zerocoin.h>
#include <veil/invalid.h>
#include <key_io.h>
#include <veil/ringct/watchonly.h>

std::unique_ptr<CWatchOnlyDB> pwatchonlyDB;

static const char DB_WATCHONLY_KEY = 'K';        // V1: String-based address keys
static const char DB_WATCHONLY_KEY_V2 = 'k';     // V2: CKeyID-based keys (lowercase)
static const char DB_WATCHONLY_TXS = 'T';
static const char DB_WATCHONLY_KEY_COUNT = 'C';
static const char DB_WATCHONLY_BLOCK_TX = 'B';
static const char DB_WATCHONLY_CHECKPOINT = 'P';
static const char DB_WATCHONLY_VERSION = 'V';    // Database version

CWatchOnlyDB::CWatchOnlyDB(size_t nCacheSize, bool fMemory, bool fWipe) : CDBWrapper(GetDataDir() / "watchonly", nCacheSize, fMemory, fWipe)
{
}

// TODO - do we want to use address as the key. we could also use data.scan_secret.GetPubKey().GetID();
bool CWatchOnlyDB::WriteWatchOnlyAddress(const std::string& address, const CWatchOnlyAddress& data)
{
    CDBBatch batch(*this);
    batch.Write(std::make_pair(DB_WATCHONLY_KEY, address), data);
    LogPrint(BCLog::WATCHONLYDB, "Writing watchonly address data %s to db.\n", address);
    return WriteBatch(batch);
}

// TODO - do we want to use address as the key. we could also use data.scan_secret.GetPubKey().GetID();
bool CWatchOnlyDB::ReadWatchOnlyAddress(const std::string& address, CWatchOnlyAddress& data)
{
    bool fSuccess = Read(std::make_pair(DB_WATCHONLY_KEY, address), data);
    LogPrint(BCLog::WATCHONLYDB, "Reading watchonly address %s from db.\n", address);
    return fSuccess;
}

bool CWatchOnlyDB::LoadWatchOnlyAddresses()
{
    // Check database version
    int dbVersion = GetDatabaseVersion();
    LogPrintf("Loading watchonly addresses from database (version %d)\n", dbVersion);

    // If V1 database, run migration
    if (dbVersion < WATCHONLY_DB_VERSION_2) {
        LogPrintf("Detected V1 database, running migration to V2...\n");
        if (!MigrateToV2()) {
            return error("Failed to migrate watchonly database to V2");
        }
        dbVersion = WATCHONLY_DB_VERSION_2;
    }

    // Load V2 format (hash-based keys)
    std::unique_ptr<CDBIterator> pcursor(NewIterator());
    pcursor->Seek(std::make_pair(DB_WATCHONLY_KEY_V2, CKeyID()));

    while (pcursor->Valid()) {
        boost::this_thread::interruption_point();
        std::pair<char, CKeyID> key;

        if (pcursor->GetKey(key) && key.first == DB_WATCHONLY_KEY_V2) {
            CWatchOnlyAddress value;

            if (pcursor->GetValue(value)) {
                mapWatchOnlyAddresses.insert(std::make_pair(key.second, value));
                LogPrint(BCLog::WATCHONLYDB, "Loaded watchonly address with keyID %s\n", key.second.ToString());
            } else {
                return error("failed to read watchonly addresses");
            }
            pcursor->Next();
        } else {
            break;
        }
    }

    LogPrintf("Loaded %d watchonly addresses from database\n", mapWatchOnlyAddresses.size());

    // Verify and recover from checkpoints if enabled
    bool fUseCheckpoints = gArgs.GetBoolArg("-watchonlycheckpoints", true);
    if (fUseCheckpoints) {
        LogPrintf("Verifying watchonly checkpoints for crash recovery...\n");
        int nRecovered = 0;

        for (auto& pair : mapWatchOnlyAddresses) {
            CWatchOnlyScanCheckpoint checkpoint;
            CKey scan_secret = pair.second.scan_secret;

            if (ReadCheckpoint(scan_secret, checkpoint)) {
                // Compare checkpoint with address scanned height
                if (checkpoint.nBlockHeight != pair.second.nCurrentScannedHeight) {
                    // Checkpoint and address height differ - need to decide which to trust
                    if (checkpoint.nBlockHeight > pair.second.nCurrentScannedHeight) {
                        // Checkpoint is AHEAD - this means crash during scan
                        // Use checkpoint (it has the last known good state with verified transactions)
                        LogPrintf("WARNING: Checkpoint ahead for keyID %s. DB: %d, Checkpoint: %d. Using checkpoint (crash recovery).\n",
                            pair.first.ToString(), pair.second.nCurrentScannedHeight, checkpoint.nBlockHeight);
                        pair.second.nCurrentScannedHeight = checkpoint.nBlockHeight;
                        pair.second.fDirty = true;
                        nRecovered++;
                    } else {
                        // Address height is AHEAD - this means scanning continued after last checkpoint
                        // Trust the address height (it was flushed at end of scan)
                        LogPrint(BCLog::WATCHONLYDB, "Address height ahead for keyID %s. DB: %d, Checkpoint: %d. Using DB height (normal scan completion).\n",
                            pair.first.ToString(), pair.second.nCurrentScannedHeight, checkpoint.nBlockHeight);
                    }
                }

                LogPrint(BCLog::WATCHONLYDB, "Verified checkpoint for keyID %s at height %d with %d txs\n",
                    pair.first.ToString(), checkpoint.nBlockHeight, checkpoint.nTxCount);
            } else {
                LogPrint(BCLog::WATCHONLYDB, "No checkpoint found for keyID %s (may be old database or no txs yet)\n",
                    pair.first.ToString());
            }
        }

        if (nRecovered > 0) {
            LogPrintf("Recovered %d watchonly addresses from checkpoints after crash\n", nRecovered);
        }
    }

    return true;
}

bool CWatchOnlyDB::WriteWatchOnlyTx(const CKey& key, const int& current_count, const CWatchOnlyTx& watchonlytx)
{
    int next_count = current_count + 1;
    CDBBatch batch(*this);
    batch.Write(std::make_pair(DB_WATCHONLY_TXS, std::make_pair(key, next_count)), watchonlytx);
    LogPrint(BCLog::WATCHONLYDB, "Writing watchonly txhash %s to db at count %d.\n", watchonlytx.tx_hash.GetHex(), next_count);
    batch.Write(std::make_pair(DB_WATCHONLY_KEY_COUNT, key), next_count);
    LogPrint(BCLog::WATCHONLYDB, "Writing to db, increasing keycount to %d.\n", next_count);
    return WriteBatch(batch);
}

bool CWatchOnlyDB::ReadWatchOnlyTx(const CKey& key, const int& count, CWatchOnlyTx& watchonlytx)
{
    bool fSuccess = Read(std::make_pair(DB_WATCHONLY_TXS, std::make_pair(key,count)), watchonlytx);
    LogPrint(BCLog::WATCHONLYDB, "Reading %d watchonly txhash %s from db.\n", count, watchonlytx.tx_hash.GetHex());
    return fSuccess;
}

bool CWatchOnlyDB::WriteBulkWatchOnlyTx(const CKey& key, int starting_count, const std::vector<CWatchOnlyTx>& vTxes)
{
    if (vTxes.empty())
        return true;

    CDBBatch batch(*this);

    int count = starting_count;
    for (const auto& tx : vTxes) {
        count++;
        batch.Write(std::make_pair(DB_WATCHONLY_TXS, std::make_pair(key, count)), tx);
    }

    // Only write counter once at the end (instead of once per transaction)
    batch.Write(std::make_pair(DB_WATCHONLY_KEY_COUNT, key), count);

    LogPrint(BCLog::WATCHONLYDB, "Bulk writing %d watchonly txes for key, new count %d\n",
             vTxes.size(), count);

    return WriteBatch(batch);
}

bool CWatchOnlyDB::WriteKeyCount(const CKey& key, const int& new_count)
{
    CDBBatch batch(*this);
    batch.Write(std::make_pair(DB_WATCHONLY_KEY_COUNT, key), new_count);
    LogPrint(BCLog::WATCHONLYDB, "Writing keycount %d to db.\n", new_count);
    return WriteBatch(batch);
}

bool CWatchOnlyDB::ReadKeyCount(const CKey& key, int& current_count)
{
    bool fSuccess = Read(std::make_pair(DB_WATCHONLY_KEY_COUNT, key), current_count);
    LogPrint(BCLog::WATCHONLYDB, "Reading keycount %d from db.\n", current_count);
    return fSuccess;
}

bool CWatchOnlyDB::WriteBlockTransactions(const int64_t& nBlockHeight, const std::vector<CTxOutWatchonly>& vTransactions)
{
    CDBBatch batch(*this);
    batch.Write(std::make_pair(DB_WATCHONLY_BLOCK_TX, nBlockHeight), vTransactions);
    // No logging here - bulk operations log at a higher level (see CWatchOnlyBlockCache::FlushAll)
    return WriteBatch(batch);
}

bool CWatchOnlyDB::ReadBlockTransactions(const int64_t& nBlockHeight, std::vector<CTxOutWatchonly>& vTransactions)
{
    bool fSuccess = Read(std::make_pair(DB_WATCHONLY_BLOCK_TX, nBlockHeight), vTransactions);
    LogPrint(BCLog::WATCHONLYDB, "Reading block txes for height %d from db.\n", nBlockHeight);
    return fSuccess;
}

bool CWatchOnlyDB::WriteBulkBlockTransactions(const std::map<int64_t, std::vector<CTxOutWatchonly>>& mapBlocks)
{
    if (mapBlocks.empty())
        return true;

    CDBBatch batch(*this);

    for (const auto& pair : mapBlocks) {
        batch.Write(std::make_pair(DB_WATCHONLY_BLOCK_TX, pair.first), pair.second);
    }

    LogPrint(BCLog::WATCHONLYDB, "Bulk writing %d block transactions to db in single batch\n", mapBlocks.size());

    return WriteBatch(batch);
}

bool CWatchOnlyDB::WriteCheckpoint(const CKey& key, const CWatchOnlyScanCheckpoint& checkpoint)
{
    CDBBatch batch(*this);
    batch.Write(std::make_pair(DB_WATCHONLY_CHECKPOINT, key), checkpoint);
    LogPrint(BCLog::WATCHONLYDB, "Writing checkpoint for height %d, tx count %d\n",
             checkpoint.nBlockHeight, checkpoint.nTxCount);
    return WriteBatch(batch);
}

bool CWatchOnlyDB::ReadCheckpoint(const CKey& key, CWatchOnlyScanCheckpoint& checkpoint)
{
    bool fSuccess = Read(std::make_pair(DB_WATCHONLY_CHECKPOINT, key), checkpoint);
    if (fSuccess) {
        LogPrint(BCLog::WATCHONLYDB, "Read checkpoint for height %d, tx count %d\n",
                 checkpoint.nBlockHeight, checkpoint.nTxCount);
    }
    return fSuccess;
}

// ===== V2 Database Methods (Hash-based keys) =====

int CWatchOnlyDB::GetDatabaseVersion()
{
    int version = WATCHONLY_DB_VERSION_1; // Default to V1 for backward compatibility
    Read(DB_WATCHONLY_VERSION, version);
    LogPrint(BCLog::WATCHONLYDB, "Database version: %d\n", version);
    return version;
}

bool CWatchOnlyDB::SetDatabaseVersion(int version)
{
    LogPrint(BCLog::WATCHONLYDB, "Setting database version to: %d\n", version);
    return Write(DB_WATCHONLY_VERSION, version);
}

bool CWatchOnlyDB::WriteWatchOnlyAddressV2(const CKeyID& keyID, const CWatchOnlyAddress& data)
{
    CDBBatch batch(*this);
    batch.Write(std::make_pair(DB_WATCHONLY_KEY_V2, keyID), data);
    LogPrint(BCLog::WATCHONLYDB, "Writing watchonly address data (V2) %s to db.\n", keyID.ToString());
    return WriteBatch(batch);
}

bool CWatchOnlyDB::ReadWatchOnlyAddressV2(const CKeyID& keyID, CWatchOnlyAddress& data)
{
    bool fSuccess = Read(std::make_pair(DB_WATCHONLY_KEY_V2, keyID), data);
    LogPrint(BCLog::WATCHONLYDB, "Reading watchonly address (V2) %s from db.\n", keyID.ToString());
    return fSuccess;
}

bool CWatchOnlyDB::MigrateToV2()
{
    int currentVersion = GetDatabaseVersion();
    if (currentVersion >= WATCHONLY_DB_VERSION_2) {
        LogPrintf("WatchOnly DB already at version %d, no migration needed\n", currentVersion);
        return true;
    }

    LogPrintf("Starting WatchOnly DB migration from V1 to V2...\n");

    // Read all V1 entries
    std::map<std::string, CWatchOnlyAddress> addressesToMigrate;
    std::unique_ptr<CDBIterator> pcursor(NewIterator());
    pcursor->Seek(std::make_pair(DB_WATCHONLY_KEY, std::string()));

    while (pcursor->Valid()) {
        boost::this_thread::interruption_point();
        std::pair<char, std::string> key;
        if (pcursor->GetKey(key) && key.first == DB_WATCHONLY_KEY) {
            CWatchOnlyAddress value;
            if (pcursor->GetValue(value)) {
                addressesToMigrate[key.second] = value;
            }
            pcursor->Next();
        } else {
            break;
        }
    }

    LogPrintf("Found %d addresses to migrate\n", addressesToMigrate.size());

    if (addressesToMigrate.empty()) {
        // No addresses to migrate, just set version
        return SetDatabaseVersion(WATCHONLY_DB_VERSION_2);
    }

    // Write V2 entries
    CDBBatch batch(*this);
    for (const auto& pair : addressesToMigrate) {
        CKeyID keyID = pair.second.scan_secret.GetPubKey().GetID();
        batch.Write(std::make_pair(DB_WATCHONLY_KEY_V2, keyID), pair.second);
        LogPrint(BCLog::WATCHONLYDB, "Migrating address %s to V2 with keyID %s\n",
                 pair.first, keyID.ToString());

        // Keep V1 entry for rollback capability
        // Can be cleaned up later with a separate tool if needed
    }

    // Update version
    batch.Write(DB_WATCHONLY_VERSION, WATCHONLY_DB_VERSION_2);

    if (!WriteBatch(batch)) {
        return error("Failed to write migrated data");
    }

    LogPrintf("WatchOnly DB migration completed successfully\n");
    return true;
}

// ===== Database Maintenance =====

void CWatchOnlyDB::CompactDatabase()
{
    LogPrintf("Starting watchonly database compaction...\n");

    // Compact the entire database by compacting all key ranges
    // We'll compact between the smallest and largest possible keys
    char cStart = DB_WATCHONLY_KEY_V2;
    char cEnd = DB_WATCHONLY_KEY_V2 + 1;

    CompactRange(cStart, cEnd);

    LogPrintf("Watchonly database compaction completed\n");
}
