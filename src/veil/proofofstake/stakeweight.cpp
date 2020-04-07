// Copyright (c) 2019-2020 The Veil developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "stakeweight.h"

void RingCtWeightBits(const CAmount& nAmount, int& ct_bits) {
    // TODO: Return the exponent of the bracket to limit the Rangeproof size.
}

// Takes the value
void RingCtStakeWeight(const CAmount& nAmount, CAmount& weight)
{
    // TODO: Return the RingCT stake weight.
}

//Sets nValueIn with the weighted amount given a certain zerocoin denomination
void ZerocoinStakeWeight(const CAmount& nAmount, CAmount& weight)
{
    weight = 0;
    if (nAmount == 10*COIN) {
        //No reduction
        weight = nAmount;
    } else if (nAmount == 100*COIN) {
        //10% reduction
        weight = (nAmount * 90) / 100;
    } else if (nAmount == 1000*COIN) {
        //20% reduction
        weight = (nAmount * 80) / 100;
    } else if (nAmount == 10000*COIN) {
        //30% reduction
        weight = (nAmount * 70) / 100;
    }
}

void StakeWeight(const CAmount& nAmount, const StakeInputType& sType, CAmount& weight)
{
    if (sType == STAKE_ZEROCOIN) {
        ZerocoinStakeWeight(nAmount, weight);
    } else {
        RingCtStakeWeight(nAmount, weight);
    }
}
