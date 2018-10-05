// Copyright (c) 2017 The PIVX developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef DENOMINATIONS_H_
#define DENOMINATIONS_H_

#include <cstdint>
#include <string>
#include <vector>

namespace libzerocoin {

enum  CoinDenomination {
    ZQ_ERROR = 0,
    ZQ_TEN = 10,
    ZQ_ONE_HUNDRED = 100,
    ZQ_ONE_THOUSAND = 1000,
    ZQ_TEN_THOUSAND = 10000
};

// Order is with the Smallest Denomination first and is important for a particular routine that this order is maintained
const std::vector<CoinDenomination> zerocoinDenomList = {ZQ_TEN, ZQ_ONE_HUNDRED, ZQ_ONE_THOUSAND, ZQ_TEN_THOUSAND};
// These are the max number you'd need at any one Denomination before moving to the higher denomination. Last number is 1, since it's the max number of
// possible spends at the moment (20,000)    /
const std::vector<int> maxCoinsAtDenom   = {9, 9, 9, 2};

int64_t ZerocoinDenominationToInt(const CoinDenomination& denomination);
int64_t ZerocoinDenominationToAmount(const CoinDenomination& denomination);
CoinDenomination IntToZerocoinDenomination(int64_t amount);
CoinDenomination AmountToZerocoinDenomination(int64_t amount);
CoinDenomination AmountToClosestDenomination(int64_t nAmount, int64_t& nRemaining);
CoinDenomination get_denomination(std::string denomAmount);
int64_t get_amount(std::string denomAmount);

} /* namespace libzerocoin */
#endif /* DENOMINATIONS_H_ */
