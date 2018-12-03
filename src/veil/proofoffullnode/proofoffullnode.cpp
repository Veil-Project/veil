// Copyright (c) 2018 The VEIL developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "veil/proofoffullnode/proofoffullnode.h"

#include <random>
#include "arith_uint256.h"
#include "consensus/merkle.h"
#include "veil/proofofstake/kernel.h"
#include "key_io.h"
#include "net_processing.h"
#include "primitives/block.h"
#include "script/standard.h"
#include "utilstrencodings.h"
#include "validation.h"
#include "veil/zerocoin/zchain.h"

namespace veil{

uint256 GetFullNodeHash(const CBlock& block, const CBlockIndex* pindexPrev)
{

    uint256 hashOut;
    if (!GenerateProofOfFullNodeVector(block.hashMerkleRoot, block.hashPrevBlock, pindexPrev, hashOut))
        return uint256();

    return hashOut;
}

//! Construct a hash that challenges the owner of the block to prove they are a full node by grabbing a deterministic
//!  psuedo-random previous block, and reording the transactions to construct a new merkle tree
bool GenerateProofOfFullNodeVector(const uint256& hashUniqueToOwner, const uint256& hashUniqueToBlock,
        const CBlockIndex* pindexPrev, uint256& hashProofOfFullNode)
{
    //Commit the owner to the previous block of the chain
    uint256 hashCommitToChain = Hash(hashUniqueToOwner.begin(), hashUniqueToOwner.end(), hashUniqueToBlock.begin(), hashUniqueToBlock.end());
    uint32_t nCommitNumber = UintToArith256(hashCommitToChain).GetLow32();

    // Use the commitment hash to get a random previous block in the chain
    uint32_t nHeightBlockCheck = nCommitNumber % pindexPrev->nHeight;

    std::vector<uint256> vProofs;
    for (int i = 0; i < Params().ProofOfFullNodeRounds(); i++ ) {
        auto pindexCheck = pindexPrev->GetAncestor(nHeightBlockCheck);
        if (!pindexCheck)
            return error("%s: do not have ancestor block at height %d", __func__, nHeightBlockCheck);
        CBlock block;
        if (!ReadBlockFromDisk(block, pindexCheck, Params().GetConsensus()))
            return false;

        //Get data from the block that a full node would have
        uint32_t nRandTx = nCommitNumber % block.vtx.size();
        nRandTx = std::min(nRandTx, (uint32_t)block.vtx.size() - 1);
        CMutableTransaction txMutate(*block.vtx[nRandTx]);

        // Mutate the transaction and get a new hash
        for (auto& txin : txMutate.vin)
            txin.nSequence = nCommitNumber;
        uint256 hashMutatedTx = txMutate.GetHash();

        // Strengthen commitment to owner, chain, and mutation
        uint256 seed = Hash(hashMutatedTx.begin(), hashMutatedTx.end(), hashCommitToChain.begin(), hashCommitToChain.end());

        // Use the seed to randomly shuffle the block's transactions and construct a mutated merkle root that contains mutated tx
        std::default_random_engine rng(UintToArith256(seed).GetLow64());
        CTransaction tx(txMutate);
        block.vtx.emplace_back(MakeTransactionRef(tx));
        std::shuffle(block.vtx.begin(), block.vtx.end(), rng);

        // Bind with mutated merkle root
        uint256 hashMutatedRoot = BlockMerkleRoot(block);
        hashMutatedRoot = Hash(hashMutatedRoot.begin(), hashMutatedRoot.end(), seed.begin(), seed.end());
        vProofs.emplace_back(hashMutatedRoot);
        nHeightBlockCheck = UintToArith256(hashMutatedRoot).GetLow32() % pindexPrev->nHeight;
    }

    hashProofOfFullNode = Hash(vProofs.begin(), vProofs.end());

    return true;
}


}