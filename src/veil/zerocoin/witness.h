#ifndef VEIL_WITNESS_H
#define VEIL_WITNESS_H


#include <libzerocoin/Accumulator.h>
#include <libzerocoin/Coin.h>
#include "primitives/zerocoin.h"
#include "serialize.h"
#include <dbwrapper.h>

// The number of items allows in the precompute LRU cache, if the LRU cache gets bigger than this, they are removed from the LRU cache and placed in the
// dirty map, which holds the PRECOMPUTE_MAX_DIRTY_CACHE_SIZE amount
#define PRECOMPUTE_LRU_CACHE_SIZE 2000
// This number represents how many items from the precompute LRU cache is held in memory before a database write is executed
#define PRECOMPUTE_MAX_DIRTY_CACHE_SIZE 1000
// How often precomputes will flush to database
#define PRECOMPUTE_FLUSH_TIME 900 // 15 minutes

class CoinWitnessCacheData;
class LRUCache;

class CoinWitnessData
{
public:
    std::unique_ptr<libzerocoin::PublicCoin> coin;
    std::unique_ptr<libzerocoin::Accumulator> pAccumulator;
    std::unique_ptr<libzerocoin::AccumulatorWitness> pWitness;
    libzerocoin::CoinDenomination denom;
    int nHeightCheckpoint;
    int nHeightMintAdded;
    int nHeightAccStart;
    int nHeightPrecomputed;
    int nMintsAdded;
    uint256 txid;
    mutable CCriticalSection cs;

    CoinWitnessData();
    CoinWitnessData(CZerocoinMint& mint);
    CoinWitnessData(CoinWitnessCacheData& data);
    CoinWitnessData(const CoinWitnessData& other);
    CoinWitnessData& operator=(const CoinWitnessData& other);
    void SetHeightMintAdded(int nHeight);
    void SetNull();
    std::string ToString();
};

class CoinWitnessCacheData
{
public:
    libzerocoin::CoinDenomination denom;
    int nHeightCheckpoint;
    int nHeightMintAdded;
    int nHeightAccStart;
    int nHeightPrecomputed;
    int nMintsAdded;
    uint256 txid;
    CBigNum coinAmount;
    libzerocoin::CoinDenomination coinDenom;
    CBigNum accumulatorAmount;
    libzerocoin::CoinDenomination accumulatorDenom;

    CoinWitnessCacheData();
    CoinWitnessCacheData(CoinWitnessData* coinWitnessData);
    void SetNull();

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action)
    {
        READWRITE(denom);
        READWRITE(nHeightCheckpoint);
        READWRITE(nHeightMintAdded);
        READWRITE(nHeightAccStart);
        READWRITE(nHeightPrecomputed);
        READWRITE(nMintsAdded);
        READWRITE(txid);
        READWRITE(coinAmount); // used to create the PublicCoin
        READWRITE(coinDenom);
        READWRITE(accumulatorAmount); // used to create the pAccumulator
        READWRITE(accumulatorDenom);
    };
};

/** Precompute database (precomputes/) */
class CPrecomputeDB : public CDBWrapper
{
public:
    explicit CPrecomputeDB(size_t nCacheSize, bool fMemory = false, bool fWipe = false);

private:
    CPrecomputeDB(const CPrecomputeDB&);
    void operator=(const CPrecomputeDB&);

public:
    /** Veil zerocoin precompute database functions */
    bool LoadPrecomputes(LRUCache* lru);
    bool LoadPrecomputes(std::set<uint256> setHashes);
    void EraseAllPrecomputes();
    bool WritePrecompute(const uint256& hash, const CoinWitnessCacheData& data);
    bool ReadPrecompute(const uint256& hash, CoinWitnessCacheData& data);
    bool ErasePrecompute(const uint256& hash);
};
#endif //VEIL_WITNESS_H
