// Copyright (c) 2018 The VEIL developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef VEIL_DANDELIONINVENTORY_H
#define VEIL_DANDELIONINVENTORY_H

#include <protocol.h>

namespace veil {

class DandelionInventory;
extern DandelionInventory dandelion;

class DandelionInventory
{
private:
    std::map<uint256, int32_t> mapStemInventory; // hash of the object and time that stem phase ends
    std::set<uint256> setPendingSend; // Inventory that is ready to be sent


public:
    void Add(const uint256& hashInventory, const int32_t& nTimeStemEnd);
    bool IsInStemPhase(const uint256& hash) const;
    int32_t GetTimeStemPhaseEnd(const uint256& hashObject) const;
    bool IsQueuedToSend(const uint256& hashObject) const;
};

}
#endif //VEIL_DANDELIONINVENTORY_H
