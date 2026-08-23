// Copyright (c) 2012-2013 The PPCoin developers
// Copyright (c) 2015-2019 The PIVX developers
// Copyright (c) 2019-2022 The Veil developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <boost/assign/list_of.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/foreach.hpp>

#include "chainparams.h"
#include "kernel.h"
#include "policy/policy.h"
#include "script/interpreter.h"
#include "timedata.h"
#include "util/system.h"
#include "stakeinput.h"
#include "veil/zerocoin/zchain.h"
#include <versionbits.h>

using namespace std;

//test hash vs target
bool stakeTargetHit(arith_uint256 hashProofOfStake, int64_t nValueIn, arith_uint256 bnTargetPerCoinDay)
{
    //get the stake weight - weight is equal to coin amount
    arith_uint256 bnTarget(nValueIn);
    bool overflow = false;
    bnTarget.safeMultiply(bnTargetPerCoinDay, overflow);

    //Double check for overflow, give max value if overflow
    if (overflow)
        bnTarget = ~arith_uint256();

    // Now check if proof-of-stake hash meets target protocol
    return hashProofOfStake < bnTarget;
}

bool CheckStake(const CDataStream& ssUniqueID, CAmount nValueIn, const uint64_t nStakeModifier, const arith_uint256& bnTarget,
                unsigned int nTimeBlockFrom, unsigned int& nTimeTx, uint256& hashProofOfStake)
{
    CDataStream ss(SER_GETHASH, 0);
    ss << nStakeModifier << nTimeBlockFrom << ssUniqueID << nTimeTx;
    hashProofOfStake = Hash(ss.begin(), ss.end());
    //LogPrintf("%s: modifier:%d nTimeBlockFrom:%d nTimeTx:%d u:%s hash:%s\n", __func__, nStakeModifier, nTimeBlockFrom, nTimeTx, Hash(ssUniqueID.begin(), ssUniqueID.end()).GetHex(), hashProofOfStake.GetHex());

    return stakeTargetHit(UintToArith256(hashProofOfStake), nValueIn, bnTarget);
}


std::set<uint256> setFoundStakes;
bool Stake(CStakeInput* stakeInput, unsigned int nBits, unsigned int nTimeBlockFrom, unsigned int& nTimeTx, const CBlockIndex* pindexBest, uint256& hashProofOfStake, bool fWeightStake)
{
    if (nTimeTx < nTimeBlockFrom)
        return error("%s: nTime violation", __func__);

    //grab difficulty
    arith_uint256 bnTargetPerCoinDay;
    bnTargetPerCoinDay.SetCompact(nBits);

    //grab stake modifier
    uint64_t nStakeModifier = 0;
    if (!stakeInput->GetModifier(nStakeModifier, pindexBest))
        return error("failed to get kernel stake modifier");

    bool fSuccess = false;
    unsigned int nTryTime = 0;
    int nHeightStart = pindexBest->nHeight;
    //staking too far into future increases chances of orphan
    int64_t nMaxTime = (int)GetAdjustedTime() + MAX_FUTURE_BLOCK_TIME - 40;

    CDataStream ssUniqueID = stakeInput->GetUniqueness();
    // Adjust stake weights
    CAmount nValueIn = fWeightStake ? stakeInput->GetWeight() : stakeInput->GetValue();

    int nBestHeight = pindexBest->nHeight;
    uint256 hashBestBlock = pindexBest->GetBlockHash();
    if (!mapStakeHashCounter.count(nBestHeight))
        mapStakeHashCounter[nBestHeight] = 0;

    int i = 0;
    while (true) //iterate the hashing
    {
        //new block came in, move on
        if (chainActive.Height() != nHeightStart)
            break;

        //hash this iteration
        nTryTime = nTimeTx + i;
        if (nTryTime >= nMaxTime - 5)
            break;

        mapStakeHashCounter[nBestHeight]++;
        i++;

        // if stake hash does not meet the target then continue to next iteration
        if (!CheckStake(ssUniqueID, nValueIn, nStakeModifier, bnTargetPerCoinDay, nTimeBlockFrom, nTryTime, hashProofOfStake))
            continue;

        stakeInput->OnStakeFound(bnTargetPerCoinDay, hashProofOfStake);

        if (setFoundStakes.count(hashProofOfStake))
            continue;
        setFoundStakes.emplace(hashProofOfStake);

        fSuccess = true; // if we make it this far then we have successfully created a stake hash
        nTimeTx = nTryTime;
        break;
    }

    mapHashedBlocks.clear();
    mapHashedBlocks[hashBestBlock] = nTryTime; //store a time stamp of when we last hashed on this block
    return fSuccess;
}

// Check kernel hash target and coinstake signature
// TODO: parameterize
bool CheckProofOfStake(CBlockIndex* pindexCheck, const CTransactionRef txRef, const uint32_t& nBits, const unsigned int& nTimeBlock, uint256& hashProofOfStake, std::unique_ptr<CStakeInput>& stake)
{
    if (!txRef->IsCoinStake())
        return error("%s: called on non-coinstake %s", __func__, txRef->GetHash().ToString().c_str());

    //Construct the stakeinput object
    if (txRef->vin.size() != 1)
        return error("%s: Stake is not correctly sized for coinstake", __func__);

    const CTxIn& txin = txRef->vin[0];

    const CBlockIndex* pindexFrom;
    if (txin.IsZerocoinSpend()) {
        auto spend = TxInToZerocoinSpend(txin);
        if (!spend)
            return false;

        if (spend->getSpendType() != libzerocoin::SpendType::STAKE)
            return error("%s: spend is using the wrong SpendType (%d)", __func__, (int)spend->getSpendType());

        stake = std::unique_ptr<CStakeInput>(new ZerocoinStake(*spend));
        pindexFrom = stake->GetIndexFrom(pindexCheck->pprev);
    } else if (txin.IsAnonInput()) {
        stake = std::unique_ptr<CStakeInput>(new PublicRingCTStake(txRef));
        pindexFrom = stake->GetIndexFrom(pindexCheck->pprev);
    } else {
        return error("%s: Stake is not a zerocoin or ringctspend", __func__);
    }

    if (!pindexFrom)
        return error("%s: Failed to find the block index", __func__);

    // Read block header
    CBlock blockprev;
    if (!ReadBlockFromDisk(blockprev, pindexFrom->GetBlockPos(), Params().GetConsensus()))
        return error("CheckProofOfStake(): INFO: failed to find block");

    arith_uint256 bnTargetPerCoinDay;
    bnTargetPerCoinDay.SetCompact(nBits);

    uint64_t nStakeModifier = 0;
    if (!stake->GetModifier(nStakeModifier, pindexCheck->pprev)) {
        if (txin.IsAnonInput() || pindexCheck->nHeight - Params().HeightLightZerocoin() > 400)
            return error("%s failed to get modifier for stake input\n", __func__);
    }

    unsigned int nBlockFromTime = blockprev.nTime;
    unsigned int nTxTime = nTimeBlock;

    // Enforce VIP-1 after it was activated
    CAmount nValue = txin.IsAnonInput() || (int)nTxTime > Params().EnforceWeightReductionTime()
        ? stake->GetWeight()
        : stake->GetValue();

    if (nValue == 0)
        return error("%s: coinstake %s has no stake weight\n", __func__, txRef->GetHash().GetHex());

    if (!CheckStake(stake->GetUniqueness(), nValue, nStakeModifier, bnTargetPerCoinDay, nBlockFromTime,
                    nTxTime, hashProofOfStake)) {
        return error("CheckProofOfStake() : INFO: check kernel failed on coinstake %s, hashProof=%s \n",
                     txRef->GetHash().GetHex(), hashProofOfStake.GetHex());
    }

    return true;
}
