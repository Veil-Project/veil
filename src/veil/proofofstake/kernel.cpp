// Copyright (c) 2012-2013 The PPCoin developers
// Copyright (c) 2015-2019 The PIVX developers
// Copyright (c) 2019 The Veil developers
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
#include "util.h"
#include "stakeinput.h"
#include "veil/zerocoin/zchain.h"
#include "libzerocoin/bignum.h"
#include <versionbits.h>

using namespace std;

//test hash vs target
bool stakeTargetHit(arith_uint256 hashProofOfStake, int64_t nValueIn, arith_uint256 bnTargetPerCoinDay)
{
    //get the stake weight - weight is equal to coin amount
    arith_uint256 bnTarget = arith_uint256(nValueIn) * bnTargetPerCoinDay;

    //Double check for overflow, give max value if overflow
    if (bnTargetPerCoinDay > bnTarget)
        bnTarget = ~arith_uint256();

    // Now check if proof-of-stake hash meets target protocol
    return hashProofOfStake < bnTarget;
}

bool CheckStake(const CDataStream& ssUniqueID, CAmount nValueIn, const uint64_t nStakeModifier, const uint256& bnTarget,
                unsigned int nTimeBlockFrom, unsigned int& nTimeTx, uint256& hashProofOfStake)
{
    CDataStream ss(SER_GETHASH, 0);
    ss << nStakeModifier << nTimeBlockFrom << ssUniqueID << nTimeTx;
    hashProofOfStake = Hash(ss.begin(), ss.end());
    //LogPrintf("%s: modifier:%d nTimeBlockFrom:%d nTimeTx:%d hash:%s\n", __func__, nStakeModifier, nTimeBlockFrom, nTimeTx, hashProofOfStake.GetHex());

    return stakeTargetHit(UintToArith256(hashProofOfStake), nValueIn, UintToArith256(bnTarget));
}


//Sets nValueIn with the weighted amount given a certain zerocoin denomination
void WeightStake(CAmount& nValueIn, const libzerocoin::CoinDenomination denom)
{
    if (denom == libzerocoin::CoinDenomination::ZQ_ONE_HUNDRED) {
        //10% reduction
        nValueIn = (nValueIn * 90) / 100;
    } else if (denom == libzerocoin::CoinDenomination::ZQ_ONE_THOUSAND) {
        //20% reduction
        nValueIn = (nValueIn * 80) / 100;
    } else if (denom == libzerocoin::CoinDenomination::ZQ_TEN_THOUSAND) {
        //30% reduction
        nValueIn = (nValueIn * 70) / 100;
    }
}

std::set<uint256> setFoundStakes;
bool Stake(CStakeInput* stakeInput, unsigned int nBits, unsigned int nTimeBlockFrom, unsigned int& nTimeTx, const CBlockIndex* pindexBest, uint256& hashProofOfStake, bool fWeightStake)
{
    if (nTimeTx < nTimeBlockFrom)
        return error("Stake() : nTime violation");

    if (nTimeBlockFrom + nStakeMinAge > nTimeTx) // Min age requirement
        return error("Stake() : min age violation - nTimeBlockFrom=%d nStakeMinAge=%d nTimeTx=%d",
                     nTimeBlockFrom, nStakeMinAge, nTimeTx);

    //grab difficulty
    arith_uint256 bnTargetPerCoinDay;
    bnTargetPerCoinDay.SetCompact(nBits);

    //grab stake modifier
    uint64_t nStakeModifier = 0;
    if (!stakeInput->GetModifier(nStakeModifier, chainActive.Tip()))
        return error("failed to get kernel stake modifier");

    bool fSuccess = false;
    unsigned int nTryTime = 0;
    int nHeightStart = pindexBest->nHeight;
    //staking too far into future increases chances of orphan
    int64_t nMaxTime = (int)GetAdjustedTime() + MAX_FUTURE_BLOCK_TIME - 40;

    CDataStream ssUniqueID = stakeInput->GetUniqueness();
    CAmount nValueIn = stakeInput->GetValue();

    //Adjust stake weights to larger denoms
    if (fWeightStake)
        WeightStake(nValueIn, stakeInput->GetDenomination());

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
        if (!CheckStake(ssUniqueID, nValueIn, nStakeModifier, ArithToUint256(bnTargetPerCoinDay), nTimeBlockFrom, nTryTime, hashProofOfStake))
            continue;

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
bool CheckProofOfStake(CBlockIndex* pindexCheck, const CTransactionRef txRef, const uint32_t& nBits, const unsigned int& nTimeBlock, uint256& hashProofOfStake, std::unique_ptr<CStakeInput>& stake)
{
    if (!txRef->IsCoinStake())
        return error("CheckProofOfStake() : called on non-coinstake %s", txRef->GetHash().ToString().c_str());

    //Construct the stakeinput object
    if (txRef->vin.size() != 1 && txRef->vin[0].IsZerocoinSpend())
        return error("%s: Stake is not a zerocoinspend", __func__);

    const CTxIn& txin = txRef->vin[0];

    auto spend = TxInToZerocoinSpend(txin);
    if (!spend)
        return false;
    stake = std::unique_ptr<CStakeInput>(new ZerocoinStake(*spend.get()));
    if (spend->getSpendType() != libzerocoin::SpendType::STAKE)
        return error("%s: spend is using the wrong SpendType (%d)", __func__, (int)spend->getSpendType());

    CBlockIndex* pindex = stake->GetIndexFrom();
    if (!pindex)
        return error("%s: Failed to find the block index", __func__);

    // Read block header
    CBlock blockprev;
    if (!ReadBlockFromDisk(blockprev, pindex->GetBlockPos(), Params().GetConsensus()))
        return error("CheckProofOfStake(): INFO: failed to find block");

    arith_uint256 bnTargetPerCoinDay;
    bnTargetPerCoinDay.SetCompact(nBits);

    uint64_t nStakeModifier = 0;
    if (!stake->GetModifier(nStakeModifier, pindexCheck->pprev))
        return error("%s failed to get modifier for stake input\n", __func__);

    unsigned int nBlockFromTime = blockprev.nTime;
    unsigned int nTxTime = nTimeBlock;
    CAmount nValue = stake->GetValue();

    // Enforce VIP-1 after it was activated
    if ((int)nTxTime > Params().EnforceWeightReductionTime())
        WeightStake(nValue, stake->GetDenomination());

    if (!CheckStake(stake->GetUniqueness(), nValue, nStakeModifier, ArithToUint256(bnTargetPerCoinDay), nBlockFromTime,
                    nTxTime, hashProofOfStake)) {
        return error("CheckProofOfStake() : INFO: check kernel failed on coinstake %s, hashProof=%s \n",
                     txRef->GetHash().GetHex(), hashProofOfStake.GetHex());
    }

    return true;
}
