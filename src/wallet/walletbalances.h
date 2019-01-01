// Copyright (c) 2017-2019 The Particl Core developers
// Copyright (c) 2019 The Veil developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef VEIL_WALLETBALANCES_H
#define VEIL_WALLETBALANCES_H

#include <amount.h>

class BalanceList
{
public:
    void Clear()
    {
        nVeil = 0;
        nVeilUnconf = 0;
        nVeilImmature = 0;
        nVeilWatchOnly = 0;
        nVeilWatchOnlyUnconf = 0;

        nCT = 0;
        nCTUnconf = 0;
        nCTImmature = 0;

        nRingCT = 0;
        nRingCTUnconf = 0;
        nRingCTImmature = 0;

        nZerocoin = 0;
        nZerocoinUnconf = 0;
        nZerocoinImmature = 0;
    };

    CAmount nVeil = 0;
    CAmount nVeilUnconf = 0;
    CAmount nVeilImmature = 0;
    CAmount nVeilWatchOnly = 0;
    CAmount nVeilWatchOnlyUnconf = 0;

    CAmount nCT = 0;
    CAmount nCTUnconf = 0;
    CAmount nCTImmature = 0;

    CAmount nRingCT = 0;
    CAmount nRingCTUnconf = 0;
    CAmount nRingCTImmature = 0;

    CAmount nZerocoin = 0;
    CAmount nZerocoinUnconf = 0;
    CAmount nZerocoinImmature = 0;
};

#endif //VEIL_WALLETBALANCES_H
