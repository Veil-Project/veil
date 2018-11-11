// Copyright (c) 2018 The VEIL developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef VEIL_DANDELIONINVENTORY_H
#define VEIL_DANDELIONINVENTORY_H

#include <protocol.h>
#include <sync.h>
#include "net.h"

namespace veil {

struct Stem
{
    int64_t nTimeStemEnd;
    int64_t nTimeLastRoll;
    int64_t nNodeIDFrom;
    int64_t nNodeIDSentTo;
};

class DandelionInventory;
extern DandelionInventory dandelion;

class DandelionInventory
{
private:
    std::map<uint256, Stem> mapStemInventory;
    std::set<uint256> setPendingSend; // Inventory that is ready to be sent
    std::map<uint256, int64_t> mapNodeToSentTo; //Maps each tx to a next node to send to
public:
    const int64_t nDefaultStemTime = 120; //120 seconds
    //! Indicates the tx came from the current node
    const int64_t nDefaultNodeID = -1;
    void Add(const uint256& hashInventory, const int64_t& nTimeStemEnd, const int64_t& nNodeIDFrom);
    bool IsFromNode(const uint256& hash, const int64_t nNodeID) const;
    bool IsNodePendingSend(const uint256& hashInventory, const int64_t nNodeID);
    int64_t GetTimeStemPhaseEnd(const uint256& hashObject) const;
    bool IsInStemPhase(const uint256& hash) const;
    bool IsSent(const uint256& hash) const;
    bool IsQueuedToSend(const uint256& hashObject) const;
    void SetInventorySent(const uint256& hash, const int64_t nNodeID);
    void MarkSent(const uint256& hash);
    void Process(const std::vector<CNode*>& vNodes);
    bool IsCorrectNodeToSend(const uint256& hash, const int64_t nNodeID);

    CCriticalSection cs;
};

}
#endif //VEIL_DANDELIONINVENTORY_H
