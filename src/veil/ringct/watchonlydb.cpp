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

static const char DB_WATCHONLY_KEY = 'K';
static const char DB_WATCHONLY_TXS = 'T';
static const char DB_WATCHONLY_KEY_COUNT = 'C';

static const char DB_WATCHONLY_BLOCK_TX = 'B';

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
    std::unique_ptr<CDBIterator> pcursor(NewIterator());
    pcursor->Seek(std::make_pair(DB_WATCHONLY_KEY, std::string()));

    while (pcursor->Valid()) {
        boost::this_thread::interruption_point();
        std::pair<char, std::string> key;

        if (pcursor->GetKey(key) && key.first == DB_WATCHONLY_KEY) {
            CWatchOnlyAddress value;

            std::string keyString = key.second;
            if (pcursor->GetValue(value)) {
                mapWatchOnlyAddresses.insert(std::make_pair(keyString, value));
            } else {
                return error("failed to read watchonly addresses");
            }
            pcursor->Next();
        } else {
            break;
        }
    }

    LogPrint(BCLog::WATCHONLYDB, "Loaded %d watchonly addresses from database\n", mapWatchOnlyAddresses.size());
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
    LogPrint(BCLog::WATCHONLYDB, "Writing block txes for height %d to db.\n", nBlockHeight);
    return WriteBatch(batch);
}

bool CWatchOnlyDB::ReadBlockTransactions(const int64_t& nBlockHeight, std::vector<CTxOutWatchonly>& vTransactions)
{
    bool fSuccess = Read(std::make_pair(DB_WATCHONLY_BLOCK_TX, nBlockHeight), vTransactions);
    LogPrint(BCLog::WATCHONLYDB, "Reading block txes for height %d from db.\n", nBlockHeight);
    return fSuccess;
}
