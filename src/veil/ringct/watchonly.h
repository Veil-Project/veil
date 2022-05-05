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
    };

    UniValue GetUniValue(int& index, bool spent = false, std::string keyimage = "", uint256 txhash = uint256(), bool fSkip = true, CAmount amount = 0);
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


/* Watchonly adddress transaction count */
bool IncrementWatchOnlyKeyCount(const CKey& key);
bool GetWatchOnlyKeyCount(const CKey& key, int& current_count);

bool GetSecretFromString(const std::string& strSecret, CKey& secret);
bool GetPubkeyFromString(const std::string& strPubkey, CPubKey& pubkey);
bool GetAmountFromWatchonly(const CWatchOnlyTx& watchonlytx, const CKey& scan_secret, const CKey& spend_secret, const CPubKey& spend_pubkey, CAmount& nAmount, uint256& blindOut, CCmpPubKey& keyImage);


bool CheckBlockForWatchOnlyTx();

#endif //VEIL_WATCHONLY_H
