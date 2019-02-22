// Copyright (c) 2017-2019 The PIVX developers
// Copyright (c) 2019 The Veil developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#ifndef VEIL_ZTRACKER_H
#define VEIL_ZTRACKER_H

#include "primitives/zerocoin.h"
#include "wallet/walletdb.h"
#include <list>
#include "veil/zerocoin/witness.h"

class CDeterministicMint;
class CWallet;
struct CMintMeta;

typedef uint256 SerialHash;
typedef uint256 PubCoinHash;

class CzTracker
{
private:
    bool fInitialized;
    std::shared_ptr<WalletDatabase> walletDatabase;
    std::map<SerialHash, CMintMeta> mapSerialHashes;
    std::map<SerialHash, uint256> mapPendingSpends; //serialhash, txid of spend
    std::map<PubCoinHash, SerialHash> mapHashPubCoin;
    std::map<SerialHash, std::unique_ptr<CoinWitnessData> > mapSpendCache; //serialhash, witness value, height
    bool UpdateStatusInternal(const std::set<uint256>& setMempoolTx, const std::map<uint256, uint256>& mapMempoolSerials, CMintMeta& mint);
public:
    CzTracker(CWallet* wallet);
    ~CzTracker();
    void Add(const CDeterministicMint& dMint, bool isNew = false, bool isArchived = false);
    void Add(const CZerocoinMint& mint, bool isNew = false, bool isArchived = false);
    bool Archive(CMintMeta& meta);
    bool HasPubcoin(const CBigNum& bnValue) const;
    bool HasPubcoinHash(const PubCoinHash& hashPubcoin) const;
    bool HasSerial(const CBigNum& bnSerial) const;
    bool HasSerialHash(const SerialHash& hashSerial) const;
    bool HasMintTx(const uint256& txid);
    bool IsEmpty() const { return mapSerialHashes.empty(); }
    void Init();
    CMintMeta Get(const SerialHash& hashSerial);
    CMintMeta GetMetaFromPubcoin(const PubCoinHash& hashPubcoin);
    bool GetMetaFromStakeHash(const uint256& hashStake, CMintMeta& meta) const;
    CAmount GetBalance(bool fConfirmedOnly, bool fUnconfirmedOnly) const;
    std::vector<SerialHash> GetSerialHashes();
    std::vector<CMintMeta> GetMints(bool fConfirmedOnly) const;
    CAmount GetUnconfirmedBalance() const;
    std::set<CMintMeta> ListMints(bool fUnusedOnly, bool fMatureOnly, bool fUpdateStatus);
    void RemovePending(const uint256& txid);
    void SetPubcoinUsed(const PubCoinHash& hashPubcoin, const uint256& txid);
    void SetPubcoinNotUsed(const PubCoinHash& hashPubcoin);
    bool UnArchive(const PubCoinHash& hashPubcoin, bool isDeterministic);
    bool UpdateZerocoinMint(const CZerocoinMint& mint);
    bool UpdateState(const CMintMeta& meta);
    void Clear();
    mutable CCriticalSection cs_modify_lock;
    mutable CCriticalSection cs_readlock;
    bool HasSpendCache(const uint256& hashSerial) EXCLUSIVE_LOCKS_REQUIRED(cs_readlock);
    CoinWitnessData* CreateSpendCache(const uint256& hashSerial) EXCLUSIVE_LOCKS_REQUIRED(cs_modify_lock);
    CoinWitnessData* GetSpendCache(const uint256& hashSerial) EXCLUSIVE_LOCKS_REQUIRED(cs_readlock);
    bool GetCoinWitness(const uint256& hashSerial, CoinWitnessData& data) EXCLUSIVE_LOCKS_REQUIRED(cs_readlock);
    bool ClearSpendCache() EXCLUSIVE_LOCKS_REQUIRED(cs_modify_lock);

    static uint8_t GetMintMemFlags(const CMintMeta& mint, int nBestHeight, const std::map<libzerocoin::CoinDenomination, int>& mapMaturity);
};

#endif //VEIL_ZTRACKER_H
