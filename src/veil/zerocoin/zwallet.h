// Copyright (c) 2017-2018 The PIVX developers
// Copyright (c) 2018 The Veil developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

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
    std::map<CKeyID, CKey> mapMasterSeeds; //Should usually only have one master seed, but occasionally user may have added another
    CKeyID seedMasterID; //The currently active master seed
    uint32_t nCountLastUsed;
    std::shared_ptr<WalletDatabase> walletDatabase;
    CMintPool mintPool;

public:
    CzWallet(CWallet* wallet);

    void AddToMintPool(const std::pair<uint256, uint32_t>& pMint, bool fVerbose);
    bool HasEmptySeed() const { return mapMasterSeeds.empty() || mapMasterSeeds.count(seedMasterID) == 0; }
    bool GetMasterSeed(CKey& key) const;
    CKeyID GetMasterSeedID() { return seedMasterID; }
    void SyncWithChain(bool fGenerateMintPool = true);
    void GenerateDeterministicZerocoin(libzerocoin::CoinDenomination denom, libzerocoin::PrivateCoin& coin, CDeterministicMint& dMint, bool fGenerateOnly = false);
    void GenerateMint(const CKeyID& seedID, const uint32_t& nCount, const libzerocoin::CoinDenomination denom, libzerocoin::PrivateCoin& coin, CDeterministicMint& dMint);
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
    void SeedToZerocoin(const uint512& seed, CBigNum& bnValue, CBigNum& bnSerial, CBigNum& bnRandomness, CKey& key);
    void SetMasterSeed(const CKey& keyMaster, bool fResetCount = false);

private:
    uint512 GetZerocoinSeed(const CKeyID& keyID, uint32_t n);
};


#endif //VEIL_ZWALLET_H
