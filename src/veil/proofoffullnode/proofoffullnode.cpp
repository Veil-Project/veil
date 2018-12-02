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

    bool ValidateProofOfFullNode(const CBlock& block, const CBlockIndex* pindexPrev, const CChainParams& params, uint256& outputHash)
    {
        uint256 hashBlock = block.GetHash();
        std::vector<uint256> vGeneratedSignatures;
        outputHash;
        if(!GenerateProofOfFullNodeVector(hashBlock, outputHash, vGeneratedSignatures, pindexPrev, params))
            return false;

        return outputHash == block.hashPoFN;
    }

    bool GenerateProofOfFullNode(CBlock* block, const CBlockIndex* prev, const CChainParams& params){
        uint256 hashBlock = block->GetHash();
        std::vector<uint256> vPoFNSigs;
        uint256 outputHash;
        if (!GenerateProofOfFullNodeVector(hashBlock, outputHash, vPoFNSigs, prev, params))
            return false;

        block->hashPoFN = outputHash;
        return true;
    }

    bool GenerateProofOfFullNodeVector(const uint256& hashBlock, uint256& outputHash, std::vector<uint256>& vPoFNSignatures, const CBlockIndex* pindexPrev, const CChainParams& params)
    {
        std::string strRewardAddress = params.NetworkRewardAddress();
        CTxDestination dest = DecodeDestination(strRewardAddress);
        CScript rewardScript = GetScriptForDestination(dest);
        uint256 hashScriptAndBlock = Hash(BEGIN(hashBlock), END(hashBlock), BEGIN(rewardScript), END(rewardScript));
        int nHeight = pindexPrev->nHeight + 1;
        arith_uint256 nNextBlockIndex = UintToArith256(hashScriptAndBlock) % nHeight;

        for(int i = 0; i < params.ProofOfFullNodeRounds(); i++ ) {
            CBlockIndex* pcurrentBlock = mapBlockIndex.at(ArithToUint256(nNextBlockIndex));
            CBlock blockCurrent;
            if(!ReadBlockFromDisk(blockCurrent, pcurrentBlock, params.GetConsensus()))
                return false;
            uint256 seed;
            if(blockCurrent.IsProofOfStake()) {
                CTransactionRef txPOS = blockCurrent.vtx[1];
                libzerocoin::CoinSpend spend = *TxInToZerocoinSpend(txPOS->vin[0]).get();
                std::unique_ptr<CStakeInput> stake = std::unique_ptr<CStakeInput>(new ZerocoinStake(spend));
                arith_uint256 bnTargetPerCoinDay;
                bnTargetPerCoinDay.SetCompact(blockCurrent.nBits);
                uint64_t nStakeModifier;
                stake->GetModifier(nStakeModifier);
                unsigned int nBlockFromTime = pcurrentBlock->pprev->nTime;
                unsigned int nTxTime = blockCurrent.nTime;
                CheckStake(stake->GetUniqueness(), stake->GetValue(), nStakeModifier, ArithToUint256(bnTargetPerCoinDay), nBlockFromTime, nTxTime, seed);
            } else
                seed = blockCurrent.GetPoWHash();
            std::default_random_engine rng(UintToArith256(seed).GetLow64());
            std::shuffle(blockCurrent.vtx.begin(), blockCurrent.vtx.end(), rng);
            uint256 hashMerKleRoot = BlockMerkleRoot(blockCurrent);
            vPoFNSignatures.push_back(hashMerKleRoot);
            nNextBlockIndex = (UintToArith256(hashMerKleRoot) & ((arith_uint256(2)<<64) - 1) ) % nHeight;
        }

        for(const auto& blockTxHash : vPoFNSignatures)
            outputHash = Hash(BEGIN(blockTxHash), END(blockTxHash), outputHash.begin(), outputHash.end());

        return true;
    }


}