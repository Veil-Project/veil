// Copyright (c) 2018 The VEIL developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef VEIL_PROOFOFFULLNODE_H
#define VEIL_PROOFOFFULLNODE_H


#include "chain.h"
#include "chainparams.h"

class CBlock;

namespace veil {

    bool GenerateProofOfFullNode(CBlock* block, const CBlockIndex* prev, const CChainParams& params);

    /**
     * Generates a proof of full node signature vector. Returns false if the proof fails.
     */
    bool GenerateProofOfFullNodeVector(const uint256& hashBlock, uint256& outputHash, std::vector<uint256>& vPoFNSignatures, const CBlockIndex* pindexPrev, const CChainParams& params);

    bool ValidateProofOfFullNode(const CBlock& block, const CBlockIndex* pindexPrev, const CChainParams& params, uint256& outputHash);

}
#endif //VEIL_PROOFOFFULLNODE_H
