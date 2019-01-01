// Copyright (c) 2019 The Veil developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef VEIL_PROOFOFFULLNODE_H
#define VEIL_PROOFOFFULLNODE_H


#include "chain.h"
#include "chainparams.h"

class CBlock;
extern CCriticalSection cs_main;

namespace veil {

uint256 GetFullNodeHash(const CBlock& block, const CBlockIndex* prev) ASSERT_EXCLUSIVE_LOCK(cs_main);

/**
 * Generates a proof of full node signature vector. Returns false if the proof fails.
 */
bool GenerateProofOfFullNodeVector(const uint256& hashUniqueToOwner, const uint256& hashUniqueToBlock,
                                   const CBlockIndex* pindexPrev, uint256& hashProofOfFullNode);

}
#endif //VEIL_PROOFOFFULLNODE_H
