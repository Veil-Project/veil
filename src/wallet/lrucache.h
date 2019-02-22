#ifndef VEIL_LRUCACHE_H
#define VEIL_LRUCACHE_H

#include <veil/zerocoin/witness.h>

class LRUCache
{
private:
    std::list<std::pair<uint256, CoinWitnessCacheData> > cache_list;
    std::map<uint256, std::list<std::pair<uint256, CoinWitnessCacheData> >::iterator> mapCacheLocation;
    std::map<uint256, CoinWitnessCacheData> mapDirtyWitnessData;

public:
    void AddNew(const uint256& hash, CoinWitnessCacheData& data);
    void AddToCache(const uint256& hash, CoinWitnessCacheData& serialData);
    bool Contains(const uint256& hash) const;
    void Clear();
    void FlushToDisk(CPrecomputeDB* pprecomputeDB);
    CoinWitnessData GetWitnessData(const uint256& hash);
    LRUCache();
    void MoveDirtyToLRU(const uint256& hash);
    void MoveLastToDirtyIfFull();
    void Remove(const uint256& hash);
    int Size() const;
    int DirtySize() const;
};

#endif //VEIL_LRUCACHE_H
