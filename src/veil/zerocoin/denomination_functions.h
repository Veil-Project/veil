// Copyright (c) 2017-2019 The PIVX developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef VEIL_DENOMINATION_FUNCTIONS_H
#define VEIL_DENOMINATION_FUNCTIONS_H

#include "util.h"
#include "libzerocoin/Denominations.h"
#include "primitives/zerocoin.h"
#include <list>
#include <map>

class CMintMeta;

std::vector<CMintMeta> SelectMintsFromList(const CAmount nValueTarget, CAmount& nSelectedValue,
                                           int nMaxNumberOfSpends,
                                           bool fMinimizeChange,
                                           int& nCoinsReturned,
                                           const std::list<CMintMeta>& listMints,
                                           const std::map<libzerocoin::CoinDenomination, CAmount> mapDenomsHeld,
                                           int& nNeededSpends
);

int calculateChange(
        int nMaxNumberOfSpends,
        bool fMinimizeChange,
        const CAmount nValueTarget,
        const std::map<libzerocoin::CoinDenomination, CAmount>& mapOfDenomsHeld,
        std::map<libzerocoin::CoinDenomination, CAmount>& mapOfDenomsUsed);

void listSpends(const std::vector<CMintMeta>& vSelectedMints);
bool oldest_first (const CMintMeta& first, const CMintMeta&  second);

#endif //VEIL_DENOMINATION_FUNCTIONS_H
