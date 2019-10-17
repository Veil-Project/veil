// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2019 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <pow.h>

#include <arith_uint256.h>
#include <chain.h>
#include <primitives/block.h>
#include <uint256.h>
#include "tinyformat.h"

// ProgPow
#include <crypto/ethash/lib/ethash/endianness.hpp>
#include <crypto/ethash/include/ethash/progpow.hpp>
#include "crypto/ethash/helpers.hpp"
#include "crypto/ethash/progpow_test_vectors.hpp"

unsigned int GetNextWorkRequired(const CBlockIndex* pindexLast, const CBlockHeader *pblock,
                                    const Consensus::Params& params, bool fProofOfStake, bool fProgPow)
{
    assert(pindexLast != nullptr);

    unsigned int nProofOfWorkLimit = UintToArith256(params.powLimit).GetCompact();

    if (params.fPowNoRetargeting) // regtest only
        return nProofOfWorkLimit;

//    if (params.fPowAllowMinDifficultyBlocks) {
//        // Special difficulty rule for testnet:
//        // If the new block's timestamp is more than 10 minutes
//        // then allow mining of a min-difficulty block.
//        if (pblock->GetBlockTime() > pindexLast->GetBlockTime() + params.nPowTargetSpacing * 5)
//            return nProofOfWorkLimit;
//        else {
//            // Return the last non-special-min-difficulty-rules-block
//            const CBlockIndex *pindex = pindexLast;
//            while (pindex->pprev && (pindex->nBits == nProofOfWorkLimit || pindex->nProofOfStakeFlag != fProofOfStake))
//                pindex = pindex->pprev;
//            return pindex->nBits;
//        }
//    }

    // Retarget every block with DarkGravityWave
    return DarkGravityWave(pindexLast, params, fProofOfStake, fProgPow);
}

unsigned int DarkGravityWave(const CBlockIndex* pindexLast, const Consensus::Params& params, bool fProofOfStake, bool fProgPow) {
    /* current difficulty formula, veil - DarkGravity v3, written by Evan Duffield - evan@dash.org */
    const arith_uint256 bnPowLimit = UintToArith256(params.powLimit);

    const CBlockIndex *pindex = pindexLast;
    const CBlockIndex* pindexLastMatchingProof = nullptr;
    arith_uint256 bnPastTargetAvg = 0;
    if (fProgPow)
        LogPrintf("%s, For ProgPow\n", __func__);

    unsigned int nCountBlocks = 0;
    while (nCountBlocks < params.nDgwPastBlocks) {
        // Ran out of blocks, return pow limit
        if (!pindex)
            return bnPowLimit.GetCompact();

        // Only consider PoW or PoS blocks but not both
        if (pindex->IsProofOfStake() != fProofOfStake) {
            pindex = pindex->pprev;
            continue;
        } else if (pindex->IsProgProofOfWork() != fProgPow) {
            pindex = pindex->pprev;
            continue;
        } else if (!pindexLastMatchingProof) {
            pindexLastMatchingProof = pindex;
        }

        arith_uint256 bnTarget = arith_uint256().SetCompact(pindex->nBits);
        bnPastTargetAvg = (bnPastTargetAvg * nCountBlocks + bnTarget) / (nCountBlocks + 1);

        if (++nCountBlocks != params.nDgwPastBlocks)
            pindex = pindex->pprev;
    }

    arith_uint256 bnNew(bnPastTargetAvg);

    //Should only happen on the first PoS block
    if (pindexLastMatchingProof)
        pindexLastMatchingProof = pindexLast;

    int64_t nPowSpacing;
    if (fProgPow) {
        nPowSpacing = params.nProgPowTargetSpacing;
    } else {
        nPowSpacing = params.nPowTargetSpacing;
    }

    int64_t nActualTimespan = pindexLastMatchingProof->GetBlockTime() - pindex->GetBlockTime();
    int64_t nTargetTimespan = params.nDgwPastBlocks * nPowSpacing;

    if (nActualTimespan < nTargetTimespan/3)
        nActualTimespan = nTargetTimespan/3;
    if (nActualTimespan > nTargetTimespan*3)
        nActualTimespan = nTargetTimespan*3;

    // Retarget
    bnNew *= nActualTimespan;
    bnNew /= nTargetTimespan;

    if (bnNew > bnPowLimit) {
        bnNew = bnPowLimit;
    }

    return bnNew.GetCompact();
}

bool CheckProofOfWork(uint256 hash, unsigned int nBits, const Consensus::Params& params)
{
    bool fNegative;
    bool fOverflow;
    arith_uint256 bnTarget;

    bnTarget.SetCompact(nBits, &fNegative, &fOverflow);

    // Check range
    if (fNegative || bnTarget == 0 || fOverflow || bnTarget > UintToArith256(params.powLimit)) {
        //std::cout << fNegative << " " << (bnTarget == 0) << " " << fOverflow << " " << (bnTarget > UintToArith256(params.powLimit)) << "\n";
        return false;
    }

    // Check proof of work matches claimed amount
    return UintToArith256(hash) < bnTarget;
}

bool CheckProgProofOfWork(const CBlockHeader& block, unsigned int nBits, const Consensus::Params& params)
{
    // Create epoch context
    ethash::epoch_context_ptr context{nullptr, nullptr};

    // Create the eth_boundary from the nBits
    arith_uint256 bnTarget;
    bool fNegative;
    bool fOverflow;

    bnTarget.SetCompact(nBits, &fNegative, &fOverflow);

    // Check range
    if (fNegative || bnTarget == 0 || fOverflow || bnTarget > UintToArith256(params.powLimit)) {
        //std::cout << fNegative << " " << (bnTarget == 0) << " " << fOverflow << " " << (bnTarget > UintToArith256(params.powLimit)) << "\n";
        return false;
    }

    // Get the context from the block height
    const auto epoch_number = ethash::get_epoch_number(block.nHeight);
    if (!context || context->epoch_number != epoch_number)
        context = ethash::create_epoch_context(epoch_number);

    // Build the header_hash
    uint256 nHeaderHash = block.GetProgPowHeaderHash();
    const auto header_hash = to_hash256(nHeaderHash.GetHex());

    // ProgPow hash
    const auto result = progpow::hash(*context, block.nHeight, header_hash, block.nNonce64);

    // Get the eth boundary
    auto boundary = to_hash256(ArithToUint256(bnTarget).GetHex());

    return progpow::verify(
            *context, block.nHeight, header_hash, result.mix_hash, block.nNonce64, boundary);
}
