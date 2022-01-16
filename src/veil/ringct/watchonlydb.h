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
    bool WriteAddressKey(const CBitcoinAddress& address, const CKey& scan_secret, const CPubKey& scan_pubkey);
    bool ReadAddressKey(const CBitcoinAddress& address, CKey& scan_secret, CPubKey& scan_pubkey);
    bool LoadWatchOnlyAddresses();
    bool WriteWatchOnlyTx(const CKey& key, const int& current_count, const CWatchOnlyTx& watchonlytx);
    bool ReadWatchOnlyTx(const CKey& key, const int& count, CWatchOnlyTx& watchonlytx);
    bool ReadKeyCount(const CKey& key, int& current_count);
    bool WriteKeyCount(const CKey& key, const int& new_count);
};

#endif //VEIL_WATCHONLYDB_H
