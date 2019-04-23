// Copyright (c) 2019 The Veil Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.


#ifndef VEIL_LRUCACHE_H
#define VEIL_LRUCACHE_H

#include <veil/zerocoin/witness.h>
#include <unordered_map>
#include <list>

class PrecomputeLRUCache
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
    PrecomputeLRUCache();
    void MoveDirtyToLRU(const uint256& hash);
    void MoveLastToDirtyIfFull();
    void Remove(const uint256& hash);
    int Size() const;
    int DirtyCacheSize() const;
};



template<typename K, typename V = K>
class LRUCacheTemplate
{

private:
    std::list<K>items;
    std::unordered_map <K, std::pair<V, typename std::list<K>::iterator>> keyValuesMap;
    int csize;

public:
    LRUCacheTemplate(int s) :csize(s) {
        if (csize < 1)
            csize = 10;
    }

    void set(const K key, const V value) {
        auto pos = keyValuesMap.find(key);
        if (pos == keyValuesMap.end()) {
            items.push_front(key);
            keyValuesMap[key] = { value, items.begin() };
            if (keyValuesMap.size() > csize) {
                keyValuesMap.erase(items.back());
                items.pop_back();
            }
        }
        else {
            items.erase(pos->second.second);
            items.push_front(key);
            keyValuesMap[key] = { value, items.begin() };
        }
    }

    bool get(const K key, V &value) {
        auto pos = keyValuesMap.find(key);
        if (pos == keyValuesMap.end())
            return false;
        items.erase(pos->second.second);
        items.push_front(key);
        keyValuesMap[key] = { pos->second.first, items.begin() };
        value = pos->second.first;
        return true;
    }
};

#endif //VEIL_LRUCACHE_H