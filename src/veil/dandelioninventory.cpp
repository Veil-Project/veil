// Copyright (c) 2018-2019 Veil developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <timedata.h>
#include "dandelioninventory.h"

#include <algorithm>

namespace veil {

DandelionInventory dandelion;

void DandelionInventory::Add(const uint256& hashInventory, const int64_t& nTimeStemEnd, const int64_t& nNodeIDFrom)
{
    Stem stem;
    // Clamp the stem end time so a peer-supplied value cannot pin an entry in the stem phase
    // indefinitely; the honest stem window is nDefaultStemTime.
    int64_t nNow = GetAdjustedTime();
    stem.nTimeStemEnd = std::max(nNow, std::min(nTimeStemEnd, nNow + nDefaultStemTime));
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

    // In the stem phase while the end time is still in the future (this was inverted, which made
    // IsInStemPhase always false and silently disabled stem routing, fluffing every tx from its origin).
    return GetAdjustedTime() < mapStemInventory.at(hash).nTimeStemEnd;
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
    mapNodeToSentTo.erase(hash);
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
    mapNodeToSentTo.erase(hash);
}

void DandelionInventory::Process(const std::vector<CNode*>& vNodes)
{
    //Clear all the old node destinations
    mapNodeToSentTo.clear();
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
        if (GetAdjustedTime() - stem.nTimeLastRoll < 5)
            continue;
        mapStemInventory.at(hash).nTimeLastRoll = GetAdjustedTime();

        //Set the index to send to: choose a random peer other than the one the tx came from.
        //If no eligible peer exists (e.g. only the origin is connected, or the peer list is empty)
        //skip this entry until the next round rather than looping forever or indexing out of range.
        //The old code used GetRandInt(vNodes.size() - 1) with a do/while on the origin id, which
        //never terminated with a single eligible peer and underflowed on an empty peer list.
        std::vector<int64_t> vEligible;
        vEligible.reserve(vNodes.size());
        for (CNode* pnode : vNodes) {
            if (pnode->GetId() != stem.nNodeIDFrom)
                vEligible.push_back(pnode->GetId());
        }
        if (vEligible.empty())
            continue;
        int64_t nNodeID = vEligible[GetRandInt(static_cast<int>(vEligible.size()))];
        mapNodeToSentTo.insert(std::make_pair(hash, nNodeID));

        // Randomly decide to send this if it is in stem phase
        auto n = GetRandInt(3);
        if (n == 1)
            setPendingSend.emplace(hash);
    }
}

    bool DandelionInventory::IsCorrectNodeToSend(const uint256& hash, const int64_t nNodeID)
    {
        return mapNodeToSentTo[hash] == nNodeID;
    }

}

