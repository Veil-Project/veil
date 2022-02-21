// Copyright (c) 2021 The Flux Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.


#include "veil/ringct/watchonly.h"
#include "veil/ringct/watchonlydb.h"

#include <util.h>
#include <key_io.h>
#include <core_io.h>
#include <univalue.h>

/** Map of watchonly keys */
std::map<std::string, CWatchOnlyAddress> mapWatchOnlyAddresses;


CWatchOnlyAddress::CWatchOnlyAddress(const CKey& scan, const CPubKey& spend) {
    scan_secret = scan;
    spend_pubkey = spend;
}


CWatchOnlyTx::CWatchOnlyTx(const CKey& key, const uint256& txhash) {
    scan_secret = key;
    tx_hash = txhash;
}

CWatchOnlyTx::CWatchOnlyTx() {
    scan_secret.Clear();
    tx_hash.SetNull();
}

UniValue CWatchOnlyTx::GetUniValue(bool spent, uint256 txhash, bool fSkip, CAmount amount)
{
    UniValue out(UniValue::VOBJ);

    if (!fSkip) {
        out.pushKV("amount", ValueFromAmount(amount));
        out.pushKV("spent", spent);
        if (spent) {
            out.pushKV("spent_in", txhash.GetHex());
        }
    }

    RingCTOutputToJSON(this->tx_hash, this->tx_index, this->ringctout, out);
    return out;
}

bool AddWatchOnlyAddress(const CBitcoinAddress& address, const CKey& scan_secret, const CPubKey& spend_pubkey)
{
    if (mapWatchOnlyAddresses.count(address.ToString())) {
        auto watchOnlyInfo = mapWatchOnlyAddresses.at(address.ToString());
        if (scan_secret == watchOnlyInfo.scan_secret && spend_pubkey == watchOnlyInfo.spend_pubkey) {
            LogPrint(BCLog::WATCHONLYDB, "Trying to add a watchonly address that already exists: %s\n", address.ToString());
            return true;
        } else {
            LogPrint(BCLog::WATCHONLYDB, "Trying to add a watchonly address with different scan key and pubkey then what already exists: %s\n", address.ToString());
            return false;
        }
    } else {
        if (!pwatchonlyDB->WriteAddressKey(address, scan_secret, spend_pubkey)) {
            LogPrint(BCLog::WATCHONLYDB, "Failed to WriteAddressKey: %s\n", address.ToString());
            return false;
        }

        CWatchOnlyAddress watchonly(scan_secret,spend_pubkey);
        mapWatchOnlyAddresses.insert(std::make_pair(address.ToString(), watchonly));
    }
    return true;
}

bool RemoveWatchOnlyAddress(const CBitcoinAddress& address, const CKey& scan_secret, const CPubKey& spend_pubkey)
{
    return true;
}

bool LoadWatchOnlyAddresses()
{
    return pwatchonlyDB->LoadWatchOnlyAddresses();
}

void PrintWatchOnlyAddressInfo() {
    for (const auto& info: mapWatchOnlyAddresses) {
        LogPrintf("address: %s\n", info.first);
        LogPrintf("scan_secret: %s\n", HexStr(info.second.scan_secret.begin(),  info.second.scan_secret.end()));
        LogPrintf("spend_pubkey: %s\n\n", HexStr(info.second.spend_pubkey.begin(),  info.second.spend_pubkey.end()));
    }
}

bool GetWatchOnlyAddressTransactions(const CBitcoinAddress& address, std::vector<uint256>& txhashses)
{
    return true;
}

bool AddWatchOnlyTransaction(const CKey& key, const CWatchOnlyTx& watchonlytx)
{

    int current_count = 0;
    if (GetWatchOnlyKeyCount(key, current_count)) {
        LogPrintf("%s: adding watchonly transaction to current count %d\n", __func__, current_count);
        // Key count exists
        return pwatchonlyDB->WriteWatchOnlyTx(key, current_count, watchonlytx);
    } else {
        // Key count didn't exist.. do the same thing?
        LogPrintf("%s: adding watchonly transaction to fresh count %d\n", __func__, current_count);
        return pwatchonlyDB->WriteWatchOnlyTx(key, -1, watchonlytx);
    }
}


bool ReadWatchOnlyTransaction(const CKey& key, const int& count, CWatchOnlyTx& watchonlytx)
{
    LogPrintf("%s: reading watchonly transaction from database\n", __func__);
    return pwatchonlyDB->ReadWatchOnlyTx(key, count, watchonlytx);
}

bool IncrementWatchOnlyKeyCount(const CKey& key)
{
    LogPrintf("%s: adding transaction count database\n", __func__);
    int current_count = 0;
    if (GetWatchOnlyKeyCount(key, current_count)) {
        return pwatchonlyDB->WriteKeyCount(key, current_count++);
    }
    return false;
}

bool GetWatchOnlyKeyCount(const CKey& key, int& current_count) {
    LogPrintf("%s: reading key count from database\n", __func__);
    return pwatchonlyDB->ReadKeyCount(key, current_count);
}