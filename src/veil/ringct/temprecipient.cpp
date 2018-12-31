// Copyright (c) 2017-2019 The Particl Core developers
// Copyright (c) 2018-2019 Veil developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <veil/ringct/temprecipient.h>

bool CTempRecipient::ApplySubFee(CAmount nFee, size_t nSubtractFeeFromAmount, bool &fFirst)
{
    if (nType != OUTPUT_DATA) {
        nAmount = nAmountSelected;
        if (fSubtractFeeFromAmount && !fExemptFeeSub) {
            nAmount -= nFee / nSubtractFeeFromAmount; // Subtract fee equally from each selected recipient

            if (fFirst) { // first receiver pays the remainder not divisible by output count
                fFirst = false;
                nAmount -= nFee % nSubtractFeeFromAmount;
            }
            return true;
        }
    }
    return false;
};
