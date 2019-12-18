// Copyright (c) 2019 The Veil developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef VEIL_STAKEWEIGHT_H
#define VEIL_STAKEWEIGHT_H

#include <amount.h>

CAmount GetStakeWeightBracket(const CAmount& nValue);

//Sets nValueIn with the weighted amount given a certain zerocoin denomination
void WeightStake(const CAmount& nValueIn, CAmount& nWeight);

#endif //VEIL_STAKEWEIGHT_H
