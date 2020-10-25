#include <chainparams.h>
#include "stakeinput.h"

std::string StakeInputTypeToString(StakeInputType t)
{
    if (t == STAKE_ZEROCOIN) {
        return "ZerocoinStake";
    } else if (t == STAKE_RINGCT) {
        return "RingCtStake";
    } else if (t == STAKE_RINGCTCANDIDATE) {
        return "RingCtStakeCandidate";
    }
    return "error-type";
}

int HeightToModifierHeight(int nHeight)
{
    //Nearest multiple of KernelModulus that is over KernelModulus bocks deep in the chain
    return (nHeight - Params().KernelModulus()) - (nHeight % Params().KernelModulus()) ;
}

// Use the PoW hash or the PoS hash
uint256 GetHashFromIndex(const CBlockIndex* pindexSample)
{
    if (pindexSample->IsProofOfWork()) {
        // By using the pindex time and checking it against the PoWUpdateTimestamp we can tell
        // the code to either set the veildatahash to all zeros if True is passed, or to do
        // nothing if False is passed.   When mining to the local wallet aggressively we have
        // found that occassionaly the memory of the pindex block data shows that the veildatahash
        // is not zero (0000xxxx). This made the wallet not accept valid PoS blocks from the chain
        // tip and allowed the wallet to fork off. This fix allows us to pass in the boolean that
        // is used to tell the code to set the veildatahash to zero manually for us. This can be
        // done only after the PoWUpdateTimestamp as veildatahash isn't used in the block hash
        // calculation after that timestamp.
        return pindexSample->GetX16RTPoWHash(pindexSample->nTime >= Params().PowUpdateTimestamp());
    }

    uint256 hashProof = pindexSample->GetBlockPoSHash();
    return hashProof;
}

// As the sampling gets further back into the chain, use more bits of entropy. This prevents the ability to significantly
// impact the modifier if you create one of the most recent blocks used. For example, the contribution from the first sample
// (which is 101 blocks from the coin tip) will only be 2 bits, thus only able to modify the end result of the modifier
// 4 different ways. As the sampling gets further back into the chain (take previous sample height - nSampleCount*6 for the height
// it will be) it has more entropy that is assigned to that specific sampling. The further back in the chain, the less
// ability there is to have any influence at all over the modifier calculations going forward from that specific block.
// The bits taper down as it gets deeper into the chain so that the precomputability is less significant.
int GetSampleBits(int nSampleCount)
{
    switch(nSampleCount) {
        case 0:
            return 2;
        case 1:
            return 4;
        case 2:
            return 8;
        case 3:
            return 16;
        case 4:
            return 32;
        case 5:
            return 64;
        case 6:
            return 128;
        case 7:
            return 64;
        case 8:
            return 32;
        case 9:
            return 16;
        default:
            return 0;
    }
}

//Use the first accumulator checkpoint that occurs 60 minutes after the block being staked from
bool CStakeInput::GetModifier(uint64_t& nStakeModifier, const CBlockIndex* pindexChainPrev)
{
    CBlockIndex* pindex = GetIndexFrom();
    if (!pindex || !pindexChainPrev)
        return false;

    uint256 hashModifier;
    if (pindexChainPrev->nHeight >= Params().HeightLightZerocoin()) {
        //Use a new modifier that is less able to be "grinded"
        int nHeightChain = pindexChainPrev->nHeight;
        int nHeightPrevious = nHeightChain - 100;
        for (int i = 0; i < 10; i++) {
            int nHeightSample = nHeightPrevious - (6*i);
            nHeightPrevious = nHeightSample;
            auto pindexSample = pindexChainPrev->GetAncestor(nHeightSample);

            //Get a sampling of entropy from this block. Rehash the sample, since PoW hashes may have lots of 0's
            uint256 hashSample = GetHashFromIndex(pindexSample);
            hashSample = Hash(hashSample.begin(), hashSample.end());

            //Reduce the size of the sampling
            int nBitsToUse = GetSampleBits(i);
            auto arith = UintToArith256(hashSample);
            arith >>= (256-nBitsToUse);
            hashSample = ArithToUint256(arith);
            hashModifier = Hash(hashModifier.begin(), hashModifier.end(), hashSample.begin(), hashSample.end());
        }
        nStakeModifier = UintToArith256(hashModifier).GetLow64();
        return true;
    }

    int nNearest100Block = HeightToModifierHeight(pindex->nHeight);

    //Rare case block index < 100, we don't use proof of stake for these blocks
    if (nNearest100Block < 1) {
        nStakeModifier = 1;
        return false;
    }

    while (nNearest100Block != pindex->nHeight) {
        pindex = pindex->pprev;
    }

    nStakeModifier = UintToArith256(pindex->mapAccumulatorHashes[denom]).GetLow64();
    return true;
}

