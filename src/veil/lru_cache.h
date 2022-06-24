// Copyright (c) 2021 Veil developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef VEIL_LRU_CACHE_H
#define VEIL_LRU_CACHE_H

#include "sync.h"

#include <list>
#include <unordered_map>
#include <utility>

namespace veil {

/**
 * The SimpleLRUCache is a fixed-size key-value map that automatically
 * evicts the least-recently-used item when a new item is added while the cache is full.
 *
 * This is a naive, non-optimized implementation of an LRU cache that uses an
 * internal mutex to prevent concurrent access.
 *
 * K must be a hashable type, but you can define your own Hash for it, or equivalently implement
 * std::hash<K> for your type. It must be a default-constructible struct or class
 * defining std::size_t operator()(const K&) const, e.g.
 *
 * namespace std {
 * template<> struct hash<MyType>
 * {
 *     std::size_t operator()(const MyType& m) const {
 *         return std::hash<std::string>()(m.ImportantString()) ^ m.ImportantInteger;
 *     }
 * };
 * }
 * SimpleLRUCache<MyType, MyValue> cache(100);
 */
template<typename K, typename V = K, class Hash = std::hash<K>>
class SimpleLRUCache
{

private:
    std::list<K> items;
    std::unordered_map<K, std::pair<V, typename std::list<K>::iterator>, Hash> keyValuesMap;
    int csize;
    CCriticalSection cs_mycache;

public:
    SimpleLRUCache(int s) : csize(s < 1 ? 10 : s), keyValuesMap(s < 1 ? 10 : s) {}

    void set(const K key, const V value) {
        LOCK(cs_mycache);
        auto pos = keyValuesMap.find(key);
        if (pos == keyValuesMap.end()) {
            items.push_front(key);
            keyValuesMap[key] = { value, items.begin() };
            if (keyValuesMap.size() > csize) {
                keyValuesMap.erase(items.back());
                items.pop_back();
            }
        } else {
            items.erase(pos->second.second);
            items.push_front(key);
            keyValuesMap[key] = { value, items.begin() };
        }
    }

    bool get(const K key, V &value) {
        LOCK(cs_mycache);
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

} // namespace veil

#endif // VEIL_LRU_CACHE_H
