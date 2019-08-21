// Copyright (c) 2019 Veil developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef VEIL_INVALID_H
#define VEIL_INVALID_H

#include <uint256.h>
#include <primitives/transaction.h>
#include <set>

namespace blacklist
{
    void AddAddress(const std::string& strAddress, const COutPoint& inputSource);
    void AddPubcoinHash(const uint256& hashPubcoin);
    void AddRctOutPoint(const COutPoint& outpoint);
    void AddStealthOutPoint(const COutPoint& outpoint);
    void AddBasecoinOutPoint(const COutPoint& outpoint);

    bool ContainsAddress(const std::string& strAddress);
    bool ContainsPubcoinHash(const uint256& hash);
    bool ContainsRingCtOutPoint(const COutPoint& out);
    bool ContainsStandardOutPoint(const COutPoint& out);
    bool ContainsStealthOutPoint(const COutPoint& out);
    bool ContainsOutPoint(const COutPoint& outpoint, int& nOutputType);

    std::set<std::string> GetAddressList();
    std::set<COutPoint> GetBasecoinBlacklist();
    std::set<COutPoint> GetStealthBlacklist();
    std::set<COutPoint> GetRingCtBlacklist();
    std::set<uint256> GetPubcoinBlacklist();

    int GetBasecoinListSize();
    int GetStealthListSize();
    int GetRingCtListSize();
    int GetPubcoinListSize();

    void InitializeBlacklist();

    bool GetLinkForAddress(const std::string& strAddress, COutPoint& prevout);
    bool GetLinkForOutPoint(const COutPoint& outpoint, COutPoint& prevout);
    bool GetLinkForPubcoin(const uint256& hashPubcoin);
}

#endif //VEIL_INVALID_H
