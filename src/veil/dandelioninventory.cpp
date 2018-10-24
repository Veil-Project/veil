// Copyright (c) 2018 The VEIL developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <timedata.h>
#include "dandelioninventory.h"

namespace veil {

DandelionInventory dandelion;

void DandelionInventory::Add(const uint256& hashInventory, const int64_t& nTimeStemEnd, const int64_t& nNodeIDFrom)
{
    Stem stem;
    stem.nTimeStemEnd = nTimeStemEnd;
    stem.nNodeIDFrom = nNodeIDFrom;
    mapStemInventory.emplace(std::make_pair(hashInventory, stem));
}

int64_t DandelionInventory::GetTimeStemPhaseEnd(const uint256& hashObject) const
{
    if (!mapStemInventory.count(hashObject))
        return 0;

    return mapStemInventory.at(hashObject).nTimeStemEnd;
}

bool DandelionInventory::IsFromNode(const uint256& hash, const int64_t nNodeID) const
{
    if (!mapStemInventory.count(hash))
        return false;

    return mapStemInventory.at(hash).nNodeIDFrom == nNodeID;
}

bool DandelionInventory::IsInStemPhase(const uint256& hash) const
{
    if (!mapStemInventory.count(hash))
        return false;

    return mapStemInventory.at(hash).nTimeStemEnd < GetAdjustedTime();
}

//Only send to a node that requests the tx if the inventory was broadcast to this node
bool DandelionInventory::IsNodePendingSend(const uint256& hashInventory, const int64_t nNodeID)
{
    if (!mapStemInventory.count(hashInventory))
        return true;

    return mapStemInventory.at(hashInventory).nNodeIDSentTo == nNodeID;
}

bool DandelionInventory::IsSent(const uint256& hash) const
{
    //Assume that if it is not here, then it is sent
    if (!mapStemInventory.count(hash))
        return true;

    return mapStemInventory.at(hash).nNodeIDSentTo != 0;
}

void DandelionInventory::SetInventorySent(const uint256& hash, const int64_t nNodeID)
{
    if (!mapStemInventory.count(hash))
        return;
    mapStemInventory.at(hash).nNodeIDSentTo = nNodeID;
    setPendingSend.erase(hash);
}

bool DandelionInventory::IsQueuedToSend(const uint256& hashObject) const
{
    //If no knowledge of this hash, then assume safe to send
    if (!mapStemInventory.count(hashObject))
        return true;

    return static_cast<bool>(setPendingSend.count(hashObject));
}

void DandelionInventory::MarkSent(const uint256& hash)
{
    mapStemInventory.erase(hash);
    setPendingSend.erase(hash);
}

void DandelionInventory::Process()
{
    auto mapStem = mapStemInventory;
    for (auto mi : mapStem) {
        auto hash = mi.first;
        auto stem = mi.second;

        //If in the fluff phase, remove from this tracker
        if (stem.nTimeStemEnd < GetAdjustedTime()) {
            mapStemInventory.erase(mi.first);
            setPendingSend.erase(mi.first);
            continue;
        }

        //Already marked this to send
        if (setPendingSend.count(mi.first))
            continue;

        //If rolled recently, then wait
        if (GetAdjustedTime() - stem.nTimeStemEnd > 5)
            continue;

        // Randomly decide to send this if it is in stem phase
        auto n = (int64_t)&stem.nTimeStemEnd; //get sort of random entropy from memory location
        if (n % 3 == 0)
            setPendingSend.emplace(hash);
        else
            mapStemInventory.at(hash).nTimeLastRoll = GetAdjustedTime();
    }
}

}

