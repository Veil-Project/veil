#ifndef VEIL_DENOMINATION_FUNCTIONS_H
#define VEIL_DENOMINATION_FUNCTIONS_H

#include "util.h"
#include "libzerocoin/Denominations.h"
#include "primitives/zerocoin.h"
#include <list>
#include <map>

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

#endif //VEIL_DENOMINATION_FUNCTIONS_H
