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

/** Map of watchonly keys */
extern std::map<std::string, CWatchOnlyAddress> mapWatchOnlyAddresses;

/** Global variable that points to the active watchonly database (protected by cs_main) */
extern std::unique_ptr<CWatchOnlyDB> pwatchonlyDB;

//class CWatchOnlyTX
//{
//public:
//    CWatchOnlyAddress(const CKey& scan, const CPubKey& spend);
//
//    CKey scan_secret;
//    CPubKey spend_pubkey;
//};

class CWatchOnlyAddress
{
public:
    CWatchOnlyAddress(const CKey& scan, const CPubKey& spend);

    CKey scan_secret;
    CPubKey spend_pubkey;

    ADD_SERIALIZE_METHODS;
    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream &s, Operation ser_action)
    {
        READWRITE(scan_secret);
        READWRITE(spend_pubkey);
    };
};

class CWatchOnlyTx
{
public:
    CWatchOnlyTx(const CKey& key, const uint256& txhash);
    CWatchOnlyTx();

    CKey scan_secret;
    uint256 tx_hash;
    int tx_index;
    CTxOutRingCT ringctout;

    //Memory only - Amount (For testing only)
    CAmount nAmount;
    uint256 blind;
    int64_t ringctIndex;

    ADD_SERIALIZE_METHODS;
    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream &s, Operation ser_action)
    {
        READWRITE(scan_secret);
        READWRITE(tx_hash);
        READWRITE(tx_index);
        READWRITE(ringctout);
    };

    UniValue GetUniValue(bool spent = false, uint256 txhash = uint256(), bool fSkip = true, CAmount amount = 0);
};

/** Watchonly address methods */
bool AddWatchOnlyAddress(const CBitcoinAddress& address, const CKey& scan_secret, const CPubKey& spend_pubkey);
bool RemoveWatchOnlyAddress(const CBitcoinAddress& address, const CKey& scan_secret, const CPubKey& spend_pubkey);
bool LoadWatchOnlyAddresses();


/** Watchonly address transaction methods */
bool GetWatchOnlyAddressTransactions(const CBitcoinAddress& address, std::vector<uint256>& txhashses);
bool AddWatchOnlyTransaction(const CKey& key,const CWatchOnlyTx& watchonlytx);
bool ReadWatchOnlyTransaction(const CKey& key, const int& count, CWatchOnlyTx& watchonlytx);
void FetchWatchOnlyTransactions(const CKey& scan_secret, std::vector<std::pair<int, CWatchOnlyTx>>& vTxes, int nStartFromIndex = -1, int nStopIndex = -1);


/* Watchonly adddress transaction count */
bool IncrementWatchOnlyKeyCount(const CKey& key);
bool GetWatchOnlyKeyCount(const CKey& key, int& current_count);


bool GetSecretFromString(const std::string& strSecret, CKey& secret);
bool GetPubkeyFromString(const std::string& strPubkey, CPubKey& pubkey);
bool GetAmountFromWatchonly(const CWatchOnlyTx& watchonlytx, const CKey& scan_secret, const CKey& spend_secret, const CPubKey& spend_pubkey, CAmount& nAmount, uint256& blindOut, CCmpPubKey& keyImage);




#endif //VEIL_WATCHONLY_H
