// Copyright (c) 2017-2019 The PIVX developers
// Copyright (c) 2019 The Veil developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <tinyformat.h>
#include "veil/zerocoin/accumulators.h"
#include "veil/zerocoin/mintmeta.h"
#include "chain.h"
#include "chainparams.h"
#include "wallet/deterministicmint.h"
#include "validation.h"
#include "stakeinput.h"
#include "veil/proofofstake/kernel.h"
#ifdef ENABLE_WALLET
#include "wallet/wallet.h"
#endif

typedef std::vector<unsigned char> valtype;

ZerocoinStake::ZerocoinStake(const libzerocoin::CoinSpend& spend)
{
    this->nChecksum = spend.getAccumulatorChecksum();
    this->denom = spend.getDenomination();
    uint256 nSerial = spend.getCoinSerialNumber().getuint256();
    this->hashSerial = Hash(nSerial.begin(), nSerial.end());
    this->pindexFrom = nullptr;
    fMint = false;
}

int ZerocoinStake::GetChecksumHeightFromMint()
{
    int nNewBlockHeight = chainActive.Height() + 1;
    int nHeightChecksum = 0;
    if (nNewBlockHeight >= Params().HeightLightZerocoin()) {
        nHeightChecksum = nNewBlockHeight - Params().Zerocoin_RequiredStakeDepthV2();
    } else {
        nHeightChecksum = chainActive.Height() + 1 - Params().Zerocoin_RequiredStakeDepth();
    }

    //Need to return the first occurance of this checksum in order for the validation process to identify a specific
    //block height
    uint256 nChecksum;
    nChecksum = chainActive[nHeightChecksum]->mapAccumulatorHashes[denom];
    return GetChecksumHeight(nChecksum, denom);
}

int ZerocoinStake::GetChecksumHeightFromSpend()
{
    return GetChecksumHeight(nChecksum, denom);
}

uint256 ZerocoinStake::GetChecksum()
{
    return nChecksum;
}

// The Zerocoin block index is the first appearance of the accumulator checksum that was used in the spend
// note that this also means when staking that this checksum should be from a block that is beyond 60 minutes old and
// 100 blocks deep.
CBlockIndex* ZerocoinStake::GetIndexFrom()
{
    if (pindexFrom)
        return pindexFrom;

    int nHeightChecksum = 0;

    if (fMint)
        nHeightChecksum = GetChecksumHeightFromMint();
    else
        nHeightChecksum = GetChecksumHeightFromSpend();

    if (nHeightChecksum > chainActive.Height()) {
        pindexFrom = nullptr;
    } else {
        //note that this will be a nullptr if the height DNE
        pindexFrom = chainActive[nHeightChecksum];
    }

    return pindexFrom;
}

CAmount ZerocoinStake::GetValue()
{
    return denom * COIN;
}

int ZerocoinStake::HeightToModifierHeight(int nHeight)
{
    //Nearest multiple of KernelModulus that is over KernelModulus bocks deep in the chain
    return (nHeight - Params().KernelModulus()) - (nHeight % Params().KernelModulus()) ;
}

// Use the PoW hash or the PoS hash
uint256 GetHashFromIndex(const CBlockIndex* pindexSample)
{
    if (pindexSample->IsProofOfWork())
        return pindexSample->GetBlockPoWHash();

    uint256 hashProof = pindexSample->GetBlockPoSHash();
    //it is possible that the block index won't have every hash needed (during the first transition to the protocol)
    assert(!hashProof.IsNull()); //todo give some type of warning that tells user to reindex chainstate
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
bool ZerocoinStake::GetModifier(uint64_t& nStakeModifier, const CBlockIndex* pindexChainPrev)
{
    CBlockIndex* pindex = GetIndexFrom();
    if (!pindex)
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

    int nNearest100Block = ZerocoinStake::HeightToModifierHeight(pindex->nHeight);

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

CDataStream ZerocoinStake::GetUniqueness()
{
    //The unique identifier for a Zerocoin VEIL is a hash of the serial
    CDataStream ss(SER_GETHASH, 0);
    ss << hashSerial;
    return ss;
}

bool ZerocoinStake::CreateTxIn(CWallet* pwallet, CTxIn& txIn, uint256 hashTxOut)
{
#ifdef ENABLE_WALLET
    if (gArgs.GetBoolArg("-disablewallet", DEFAULT_DISABLE_WALLET)) {
#endif
        return error("%s: wallet disabled", __func__);
#ifdef ENABLE_WALLET
    }

    CBlockIndex* pindexCheckpoint = GetIndexFrom();
    if (!pindexCheckpoint)
        return error("%s: failed to find checkpoint block index", __func__);

    CZerocoinMint mint;
    if (!pwallet->GetMintFromStakeHash(hashSerial, mint))
        return error("%s: failed to fetch mint associated with serial hash %s", __func__, hashSerial.GetHex());

    if (libzerocoin::ExtractVersionFromSerial(mint.GetSerialNumber()) < 2)
        return error("%s: serial extract is less than v2", __func__);

    int nSecurityLevel = 100;
    CZerocoinSpendReceipt receipt;
    if (!pwallet->MintToTxIn(mint, nSecurityLevel, hashTxOut, txIn, receipt, libzerocoin::SpendType::STAKE, GetIndexFrom()))
        return error("%s\n", receipt.GetStatusMessage());

    return true;
#endif
}

bool ZerocoinStake::CreateTxOuts(CWallet* pwallet, vector<CTxOut>& vout, CAmount nTotal)
{
#ifdef ENABLE_WALLET
    if (gArgs.GetBoolArg("-disablewallet", DEFAULT_DISABLE_WALLET)) {
#endif
        return error("%s: wallet disabled", __func__);
#ifdef ENABLE_WALLET
    }

    //Create an output returning the Zerocoin VEIL that was staked
    CTxOut outReward;
    libzerocoin::CoinDenomination denomStaked = libzerocoin::AmountToZerocoinDenomination(this->GetValue());
    CDeterministicMint dMint;
    if (!pwallet->CreateZOutPut(denomStaked, outReward, dMint))
        return error("%s: failed to create zerocoin output", __func__);
    vout.emplace_back(outReward);

    //Add new staked denom to our wallet
    if (!pwallet->DatabaseMint(dMint))
        return error("%s: failed to database the staked Zerocoin", __func__);

    CAmount nRewardOut = 0;
    while (nRewardOut < nTotal) {
        CTxOut out;
        CDeterministicMint dMintReward;
        auto denomReward = libzerocoin::CoinDenomination::ZQ_TEN;
        if (!pwallet->CreateZOutPut(denomReward, out, dMintReward))
            return error("%s: failed to create Zerocoin output", __func__);
        vout.emplace_back(out);

        if (!pwallet->DatabaseMint(dMintReward))
            return error("%s: failed to database mint reward", __func__);
        nRewardOut += libzerocoin::ZerocoinDenominationToAmount(denomReward);
    }

    return true;
#endif
}

bool ZerocoinStake::GetTxFrom(CTransaction& tx)
{
    return false;
}

bool ZerocoinStake::MarkSpent(CWallet *pwallet, const uint256& txid)
{
#ifdef ENABLE_WALLET
    if (gArgs.GetBoolArg("-disablewallet", DEFAULT_DISABLE_WALLET)) {
#endif
        return error("%s: wallet disabled", __func__);
#ifdef ENABLE_WALLET
    }

    CzTracker* zTracker = pwallet->GetZTrackerPointer();
    CMintMeta meta;
    if (!zTracker->GetMetaFromStakeHash(hashSerial, meta))
        return error("%s: tracker does not have serialhash", __func__);

    zTracker->SetPubcoinUsed(meta.hashPubcoin, txid);
    return true;
#endif
}

uint256 ZerocoinStake::GetSerialStakeHash()
{
    return hashSerial;
}
