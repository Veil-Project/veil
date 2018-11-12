#ifndef VEIL_ZWALLET_H
#define VEIL_ZWALLET_H

#include <map>
#include <wallet/wallet.h>
#include "libzerocoin/Coin.h"
#include "mintpool.h"
#include "uint256.h"
#include "primitives/zerocoin.h"

class CDeterministicMint;

class CzWallet
{
private:
    uint256 seedMaster;
    uint32_t nCountLastUsed;
    std::shared_ptr<WalletDatabase> walletDatabase;
    CMintPool mintPool;

public:
    CzWallet(CWallet* wallet);

    void AddToMintPool(const std::pair<uint256, uint32_t>& pMint, bool fVerbose);
    uint256 GetMasterSeed() { return seedMaster; }
    void SyncWithChain(bool fGenerateMintPool = true);
    void GenerateDeterministicZerocoin(libzerocoin::CoinDenomination denom, libzerocoin::PrivateCoin& coin, CDeterministicMint& dMint, bool fGenerateOnly = false);
    void GenerateMint(const uint32_t& nCount, const libzerocoin::CoinDenomination denom, libzerocoin::PrivateCoin& coin, CDeterministicMint& dMint);
    WalletDatabase& GetDBHandle() { return *walletDatabase; }
    void GetState(int& nCount, int& nLastGenerated);
    bool RegenerateMint(const CDeterministicMint& dMint, CZerocoinMint& mint);
    void GenerateMintPool(uint32_t nCountStart = 0, uint32_t nCountEnd = 0);
    bool LoadMintPoolFromDB();
    void RemoveMintsFromPool(const std::vector<uint256>& vPubcoinHashes);
    bool SetMintSeen(const CBigNum& bnValue, const int& nHeight, const uint256& txid, const libzerocoin::CoinDenomination& denom);
    bool IsInMintPool(const CBigNum& bnValue) { return mintPool.Has(bnValue); }
    void UpdateCount();
    void Lock();
    void SeedToZPIV(const uint512& seed, CBigNum& bnValue, CBigNum& bnSerial, CBigNum& bnRandomness, CKey& key);

private:
    uint512 GetZerocoinSeed(uint32_t n);
};


#endif //VEIL_ZWALLET_H
