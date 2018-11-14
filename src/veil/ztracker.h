#ifndef VEIL_ZTRACKER_H
#define VEIL_ZTRACKER_H

#include "primitives/zerocoin.h"
#include "wallet/walletdb.h"
#include <list>

class CDeterministicMint;
class CWallet;

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
    bool UpdateStatusInternal(const std::set<uint256>& setMempool, CMintMeta& mint);
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
};

#endif //VEIL_ZTRACKER_H
