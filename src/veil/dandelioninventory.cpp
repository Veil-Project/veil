// Copyright (c) 2018 The VEIL developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <timedata.h>
#include "dandelioninventory.h"

namespace veil {

DandelionInventory dandelion;

void DandelionInventory::Add(const uint256& hashInventory, const int32_t& nTimeStemEnd)
{
    mapStemInventory.emplace(std::make_pair(hashInventory, nTimeStemEnd));
}

int32_t DandelionInventory::GetTimeStemPhaseEnd(const uint256& hashObject) const
{
    if (!mapStemInventory.count(hashObject))
        return 0;

    return mapStemInventory.at(hashObject);
}

bool DandelionInventory::IsInStemPhase(const uint256& hash) const
{
    if (!mapStemInventory.count(hash))
        return false;

    return mapStemInventory.at(hash) < GetAdjustedTime();
}

bool DandelionInventory::IsQueuedToSend(const uint256& hashObject) const
{
    //If no knowledge of this hash, then assume safe to send
    if (!mapStemInventory.count(hashObject))
        return true;

    return static_cast<bool>(setPendingSend.count(hashObject));
}

}

