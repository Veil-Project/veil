// Copyright (c) 2019-2020 The Veil developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef VEIL_STAKEWEIGHT_H
#define VEIL_STAKEWEIGHT_H

#include "veil/proofofstake/stakeinput/stakeinput.h"
#include <amount.h>

static const uint32_t BRACKETBASE = 16;
static const uint32_t BRACKETSHIFT = 2;

static const CAmount nBareMinStake = BRACKETBASE;
static const CAmount nOneSat = 1;

int RingCtWeightBits(const CAmount& nAmount, int& ct_bits);

int RingCtWeightBracket(const CAmount& nAmount, uint32_t& bracket);

bool RingCtStakeWeight(const CAmount& nAmount, CAmount& weight, int& ct_bits);

//Sets nValueIn with the weighted amount given a certain zerocoin denomination
void ZerocoinStakeWeight(const CAmount& nAmount, CAmount& weight);

bool StakeWeight(const CAmount& nAmount, const StakeInputType& sType, CAmount& weight);

#endif //VEIL_STAKEWEIGHT_H
