#ifndef VEIL_ZTRACKER_H
#define VEIL_ZTRACKER_H

#include "primitives/rpczerocoin.h"
#include "wallet/walletdb.h"
#include <list>

class CDeterministicMint;
class CWallet;

class CzTracker
{
private:
    bool fInitialized;
    std::shared_ptr<WalletDatabase> walletDatabase;
    std::map<uint256, CMintMeta> mapSerialHashes;
    std::map<uint256, uint256> mapPendingSpends; //serialhash, txid of spend
    bool UpdateStatusInternal(const std::set<uint256>& setMempool, CMintMeta& mint);
public:
    CzTracker(CWallet* wallet);
    ~CzTracker();
    void Add(const CDeterministicMint& dMint, bool isNew = false, bool isArchived = false);
    void Add(const CZerocoinMint& mint, bool isNew = false, bool isArchived = false);
    bool Archive(CMintMeta& meta);
    bool HasPubcoin(const CBigNum& bnValue) const;
    bool HasPubcoinHash(const uint256& hashPubcoin) const;
    bool HasSerial(const CBigNum& bnSerial) const;
    bool HasSerialHash(const uint256& hashSerial) const;
    bool HasMintTx(const uint256& txid);
    bool IsEmpty() const { return mapSerialHashes.empty(); }
    void Init();
    CMintMeta Get(const uint256& hashSerial);
    CMintMeta GetMetaFromPubcoin(const uint256& hashPubcoin);
    bool GetMetaFromStakeHash(const uint256& hashStake, CMintMeta& meta) const;
    CAmount GetBalance(bool fConfirmedOnly, bool fUnconfirmedOnly) const;
    std::vector<uint256> GetSerialHashes();
    std::vector<CMintMeta> GetMints(bool fConfirmedOnly) const;
    CAmount GetUnconfirmedBalance() const;
    std::set<CMintMeta> ListMints(bool fUnusedOnly, bool fMatureOnly, bool fUpdateStatus);
    void RemovePending(const uint256& txid);
    void SetPubcoinUsed(const uint256& hashPubcoin, const uint256& txid);
    void SetPubcoinNotUsed(const uint256& hashPubcoin);
    bool UnArchive(const uint256& hashPubcoin, bool isDeterministic);
    bool UpdateZerocoinMint(const CZerocoinMint& mint);
    bool UpdateState(const CMintMeta& meta);
    void Clear();
};

#endif //VEIL_ZTRACKER_H
