// Copyright (c) 2019-2020 The Veil developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef VEIL_STAKEWEIGHT_H
#define VEIL_STAKEWEIGHT_H

#include "veil/proofofstake/stakeinput/stakeinput.h"
#include <amount.h>

void RingCtStakeWeight(const CAmount& nAmount, CAmount& weight);

//Sets nValueIn with the weighted amount given a certain zerocoin denomination
void ZerocoinStakeWeight(const CAmount& nAmount, CAmount& weight);

void RingCtStakeWeightBits(const CAmount& nAmount, CAmount& weight, int& ct_bits);

void StakeWeight(const CAmount& nAmount, const StakeInputType& sType, CAmount& weight);

#endif //VEIL_STAKEWEIGHT_H
