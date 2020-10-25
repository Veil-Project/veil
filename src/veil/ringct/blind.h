// Copyright (c) 2017-2019 The Particl Core developers
// Copyright (c) 2019 The Veil developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#ifndef VEIL_BLIND_H
#define VEIL_BLIND_H

#include <secp256k1.h>
#include <inttypes.h>
#include <vector>

#include <amount.h>

extern secp256k1_context *secp256k1_ctx_blind;

int SelectRangeProofParameters(uint64_t nValueIn, int &exponent, int &nBits);

bool GetRangeProofInfo(const std::vector<uint8_t> &vRangeproof, int &rexp, int &rmantissa, CAmount &min_value, CAmount &max_value);

void ECC_Start_Blinding();
void ECC_Stop_Blinding();

#endif //VEIL_BLIND_H
