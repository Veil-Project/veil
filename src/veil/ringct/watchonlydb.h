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

/** A database (watchonly/) */
class CWatchOnlyDB : public CDBWrapper
{
public:
    explicit CWatchOnlyDB(size_t nCacheSize, bool fMemory = false, bool fWipe = false);

private:
    CWatchOnlyDB(const CWatchOnlyDB&);
    void operator=(const CWatchOnlyDB&);

public:
    /** Write Keys to list of keys to scan */
    bool WriteWatchOnlyAddress(const std::string& address, const CWatchOnlyAddress& data);
    bool ReadWatchOnlyAddress(const std::string& address, CWatchOnlyAddress& data);
    bool LoadWatchOnlyAddresses();

    bool WriteWatchOnlyTx(const CKey& key, const int& current_count, const CWatchOnlyTx& watchonlytx);
    bool ReadWatchOnlyTx(const CKey& key, const int& count, CWatchOnlyTx& watchonlytx);

    bool ReadKeyCount(const CKey& key, int& current_count);
    bool WriteKeyCount(const CKey& key, const int& new_count);

    bool ReadBlockTransactions(const int64_t& nBlockHeight, std::vector<CTxOutWatchonly>& vTransactions);
    bool WriteBlockTransactions(const int64_t& blockheight, const std::vector<CTxOutWatchonly>& vTransactions);
};

#endif //VEIL_WATCHONLYDB_H
