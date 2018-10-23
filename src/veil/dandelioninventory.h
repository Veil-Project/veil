// Copyright (c) 2018 The VEIL developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef VEIL_DANDELIONINVENTORY_H
#define VEIL_DANDELIONINVENTORY_H

#include <protocol.h>

namespace veil {

struct Stem
{
    int64_t nTimeStemEnd;
    int64_t nTimeLastRoll;
};

class DandelionInventory;
extern DandelionInventory dandelion;

class DandelionInventory
{
private:
    std::map<uint256, Stem> mapStemInventory;
    std::set<uint256> setPendingSend; // Inventory that is ready to be sent


public:
    void Add(const uint256& hashInventory, const int64_t& nTimeStemEnd);
    int64_t GetTimeStemPhaseEnd(const uint256& hashObject) const;
    bool IsInStemPhase(const uint256& hash) const;
    bool IsQueuedToSend(const uint256& hashObject) const;
    void Process();
};

}
#endif //VEIL_DANDELIONINVENTORY_H
