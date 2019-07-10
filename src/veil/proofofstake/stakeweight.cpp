#include "stakeweight.h"

CAmount GetStakeWeightBracket(const CAmount& nValue)
{
    if (nValue > 10000)
        return 10000;
    if (nValue > 1000)
        return 1000;
    if (nValue > 100)
        return 100;
    if (nValue > 10)
        return 10;

    return 0;
}

//Sets nValueIn with the weighted amount given a certain zerocoin denomination
void WeightStake(const CAmount& nValueIn, CAmount& nWeight)
{
    nWeight = 0;
    if (nValueIn == 10*COIN) {
        //No reduction
        nWeight = nValueIn;
    } else if (nValueIn == 100*COIN) {
        //10% reduction
        nWeight = (nValueIn * 90) / 100;
    } else if (nValueIn == 1000*COIN) {
        //20% reduction
        nWeight = (nValueIn * 80) / 100;
    } else if (nValueIn == 10000*COIN) {
        //30% reduction
        nWeight = (nValueIn * 70) / 100;
    }
}