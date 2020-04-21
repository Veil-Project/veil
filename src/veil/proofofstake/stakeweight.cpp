// Copyright (c) 2019-2020 The Veil developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <tgmath.h>

#include "stakeweight.h"

const int tab64[64] = {
    63,  0, 58,  1, 59, 47, 53,  2,
    60, 39, 48, 27, 54, 33, 42,  3,
    61, 51, 37, 40, 49, 18, 28, 20,
    55, 30, 34, 11, 43, 14, 22,  4,
    62, 57, 46, 52, 38, 26, 32, 41,
    50, 36, 17, 19, 29, 10, 13, 21,
    56, 45, 25, 31, 35, 16,  9, 12,
    44, 24, 15,  8, 23,  7,  6,  5};

int log2_64 (uint64_t value)
{
    value |= value >> 1;
    value |= value >> 2;
    value |= value >> 4;
    value |= value >> 8;
    value |= value >> 16;
    value |= value >> 32;
    return tab64[((uint64_t)((value - (value >> 1))*0x07EDD5E59A4E28C2)) >> 58];
}

bool CheckMinStake(const CAmount& nAmount)
{
    // Protocol needs to enforce bare minimum (mathematical min). User can define selective min
    if (nAmount <= nBareMinStake)
        return false;
    return true;
}

int RingCtWeightBits(const CAmount& nAmount, int& ct_bits)
{
    if (!CheckMinStake(nAmount)) {
        return -1;
    }

    u_int32_t bracket = 0;
    bracket = RingCtWeightBracket(nAmount, bracket);
    ct_bits = bracket << BRACKETSHIFT;

    return 0;
}


int RingCtWeightBracket(const CAmount& nAmount, u_int32_t& bracket)
{
    if (!CheckMinStake(nAmount)) {
        return -1;
    }

    bracket = (static_cast<u_int32_t>(floor(log2_64(nAmount-nOneSat)))) >> BRACKETSHIFT;

    return 0;
}

//      Bracket                 min                                     max
//      -------                 ---                                     ---
//         1                    5 (        0.00000005)                 16 (        0.00000016)
//         2                   17 (        0.00000017)                 64 (        0.00000064)
//         3                   65 (        0.00000065)                256 (        0.00000256)
//         4                  257 (        0.00000257)               1024 (        0.00001024)
//         5                 1025 (        0.00001025)               4096 (        0.00004096)
//         6                 4097 (        0.00004097)              16384 (        0.00016384)
//         7                16385 (        0.00016385)              65536 (        0.00065536)
//         8                65537 (        0.00065537)             262144 (        0.00262144)
//         9               262145 (        0.00262145)            1048576 (        0.01048576)
//         10             1048577 (        0.01048577)            4194304 (        0.04194304)
//         11             4194305 (        0.04194305)           16777216 (        0.16777216)
//         12            16777217 (        0.16777217)           67108864 (        0.67108864)
//         13            67108865 (        0.67108865)          268435456 (        2.68435456)
//         14           268435457 (        2.68435457)         1073741824 (       10.73741824)
//         15          1073741825 (       10.73741825)         4294967296 (       42.94967296)
//         16          4294967297 (       42.94967297)        17179869184 (      171.79869184)
//         17         17179869185 (      171.79869185)        68719476736 (      687.19476736)
//         18         68719476737 (      687.19476737)       274877906944 (     2748.77906944)
//         19        274877906945 (     2748.77906945)      1099511627776 (    10995.11627776)
//         20       1099511627777 (    10995.11627777)      4398046511104 (    43980.46511104)
//         21       4398046511105 (    43980.46511105)     17592186044416 (   175921.86044416)
//         22      17592186044417 (   175921.86044417)     70368744177664 (   703687.44177664)
//         23      70368744177665 (   703687.44177665)    281474976710656 (  2814749.76710656)
//         24     281474976710657 (  2814749.76710657)   1125899906842624 ( 11258999.06842624)
//         25    1125899906842625 ( 11258999.06842625)   4503599627370496 ( 45035996.27370496)
//         26    4503599627370497 ( 45035996.27370497)  18014398509481984 (180143985.09481984)
//         27   18014398509481985 (180143985.09481985)  72057594037927936 (720575940.37927936)
bool RingCtStakeWeight(const CAmount& nAmount, CAmount& weight, int& ct_bits)
{
    if (!CheckMinStake(nAmount)) {
        return false;
    }

    // calculate the bracket
    u_int32_t bracket = 0;
    RingCtWeightBracket(nAmount, bracket);
    ct_bits = bracket << BRACKETSHIFT;

    // calculate the weight
    weight = static_cast<u_int64_t>(pow(BRACKETBASE, bracket)) + nOneSat;

    return true;
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

bool StakeWeight(const CAmount& nAmount, const StakeInputType& sType, CAmount& weight)
{
    if (!CheckMinStake(nAmount)) {
        return false;
    }

    if (sType == STAKE_ZEROCOIN) {
        ZerocoinStakeWeight(nAmount, weight);
    } else {
        int unused;
        RingCtStakeWeight(nAmount, weight, unused);
    }

    return true;
}
