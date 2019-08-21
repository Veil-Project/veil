// Copyright (c) 2019 Veil developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <logging.h>
#include "invalid.h"
#include "invalid_list.h"
#include <univalue/include/univalue.h>

namespace blacklist
{
    std::set<uint256> setPubcoinHashes;
    std::set<COutPoint> setRingCtOutPoints;
    std::set<COutPoint> setBasecoinOutPoints;
    std::set<COutPoint> setStealthOutPoints;
    std::map<std::string, COutPoint> mapAddresses;

    UniValue GetUvArraryFromString(const std::string& strJson)
    {
        UniValue uv;
        if (!uv.read(strJson) || !uv.isArray()) {
            return NullUniValue;
        }

        return uv.get_array();
    }

    COutPoint OutPointFromUniValue(UniValue objOutpoint)
    {
        std::map<std::string, UniValue> mapKeyValue;
        objOutpoint.getObjMap(mapKeyValue);
        COutPoint outpoint;
        if (!mapKeyValue.empty()) {
            outpoint.hash = uint256S(mapKeyValue.begin()->first);
            outpoint.n = mapKeyValue.begin()->second.get_int64();
        }
        return outpoint;
    }

    void InitializeAddressBlacklist()
    {
        auto uv = GetUvArraryFromString(GetInitialAddressBlacklist());
        std::set<std::string> setAddresses;
        for (unsigned int i = 0; i < uv.size(); i++) {
            AddAddress(uv[i].get_str(), COutPoint());
        }
    }

    void InitializeBasecoinBlacklist()
    {
        auto uvBasecoinOutpoints = GetUvArraryFromString(GetInitialBasecoinBlacklist());
        for (unsigned int i = 0; i < uvBasecoinOutpoints.size(); i++) {
            auto arrBanned = uvBasecoinOutpoints[i].get_array();
            //First object is the banned outpoint, the second is the source of the ban
            auto objOutpoint = arrBanned[0].get_obj();
            COutPoint outpoint = OutPointFromUniValue(objOutpoint);
            blacklist::AddBasecoinOutPoint(outpoint);
        }
    }

    void InitializeStealthBlacklist()
    {
        auto uvStealthOutpoints = GetUvArraryFromString(GetInitialStealthBlacklist());
        for (unsigned int i = 0; i < uvStealthOutpoints.size(); i++) {
            auto arrBanned = uvStealthOutpoints[i].get_array();
            //First object is the banned outpoint, the second is the source of the ban
            auto objOutpoint = arrBanned[0].get_obj();
            COutPoint outpoint = OutPointFromUniValue(objOutpoint);
            blacklist::AddStealthOutPoint(outpoint);
        }
    }

    void InitializeRingCtBlacklist()
    {
        auto uvRingCtOutpoints = GetUvArraryFromString(GetInitialRingCtBlacklist());
        for (unsigned int i = 0; i < uvRingCtOutpoints.size(); i++) {
            auto uvOutPoint = uvRingCtOutpoints[i].get_obj();
            COutPoint outpoint = OutPointFromUniValue(uvOutPoint);
            blacklist::AddRctOutPoint(outpoint);
        }
    }

    void InitializePubcoinBlacklist()
    {
        auto uvPubcoins = GetUvArraryFromString(GetInitialPubcoinBlacklist());
        for (unsigned int i = 0; i < uvPubcoins.size(); i++) {
            blacklist::AddPubcoinHash(uint256S(uvPubcoins[i].get_str()));
        }
    }

    void InitializeBlacklist()
    {
        InitializeAddressBlacklist();
        InitializeBasecoinBlacklist();
        InitializeStealthBlacklist();
        InitializeRingCtBlacklist();
        InitializePubcoinBlacklist();
    }

    bool ContainsOutPoint(const COutPoint& outpoint, int& nOutputType)
    {
        if (ContainsRingCtOutPoint(outpoint)) {
            nOutputType = OUTPUT_RINGCT;
            return true;
        }

        if (ContainsStandardOutPoint(outpoint)) {
            nOutputType = OUTPUT_STANDARD;
            return true;
        }

        if (ContainsStealthOutPoint(outpoint)) {
            nOutputType = OUTPUT_CT;
            return true;
        }

        return false;
    }

    bool ContainsAddress(const std::string& strAddress)
    {
        return mapAddresses.count(strAddress) > 0;
    }

    bool ContainsPubcoinHash(const uint256& hash)
    {
        return setPubcoinHashes.count(hash) > 0;
    }

    bool ContainsRingCtOutPoint(const COutPoint& out)
    {
        return setRingCtOutPoints.count(out) > 0;
    }

    bool ContainsStandardOutPoint(const COutPoint& out)
    {
        return setBasecoinOutPoints.count(out) > 0;
    }

    bool ContainsStealthOutPoint(const COutPoint& out)
    {
        return setStealthOutPoints.count(out) > 0;
    }

    bool GetLinkForAddress(const std::string& strAddress, COutPoint& prevout)
    {
        if (mapAddresses.count(strAddress)) {
            prevout = mapAddresses.at(strAddress);
            return true;
        }
        return false;
    }

    std::set<std::string> GetAddressList()
    {
        std::set<std::string> setAddresses;
        for (auto pair : mapAddresses)
            setAddresses.emplace(pair.first);
        return setAddresses;
    }

    std::set<COutPoint> GetBasecoinBlacklist()
    {
        return setBasecoinOutPoints;
    }

    std::set<COutPoint> GetStealthBlacklist()
    {
        return setStealthOutPoints;
    }

    std::set<COutPoint> GetRingCtBlacklist()
    {
        return setRingCtOutPoints;
    }

    std::set<uint256> GetPubcoinBlacklist()
    {
        return setPubcoinHashes;
    }

    int GetBasecoinListSize()
    {
        return static_cast<int>(setBasecoinOutPoints.size());
    }

    int GetStealthListSize()
    {
        return static_cast<int>(setStealthOutPoints.size());
    }

    int GetRingCtListSize()
    {
        return static_cast<int>(setRingCtOutPoints.size());
    }

    int GetPubcoinListSize()
    {
        return static_cast<int>(setPubcoinHashes.size());
    }

    void AddAddress(const std::string& strAddress, const COutPoint& inputSource)
    {
        mapAddresses.emplace(strAddress, inputSource);
    }

    void AddPubcoinHash(const uint256& hashPubcoin)
    {
        setPubcoinHashes.emplace(hashPubcoin);
    }

    void AddRctOutPoint(const COutPoint& outpoint)
    {
        setRingCtOutPoints.emplace(outpoint);
    }

    void AddStealthOutPoint(const COutPoint& outpoint)
    {
        setStealthOutPoints.emplace(outpoint);
    }

    void AddBasecoinOutPoint(const COutPoint& outpoint)
    {
        setBasecoinOutPoints.emplace(outpoint);
    }
}
