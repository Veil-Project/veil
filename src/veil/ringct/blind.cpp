// Copyright (c) 2017-2019 The Particl developers
// Copyright (c) 2020 The Veil developers
// Distributed under the MIT/X11 software license, see the accompanying
// file license.txt or http://www.opensource.org/licenses/mit-license.php.

#include <veil/ringct/blind.h>

#include <assert.h>
#include <secp256k1_rangeproof.h>

#include <support/allocators/secure.h>
#include <random.h>
#include <util.h>

static secp256k1_context* secp256k1_blind_context = NULL;

static int CountLeadingZeros(uint64_t nValueIn) {
    int nZeros = 0;

    for (size_t i = 0; i < 64; ++i, nValueIn >>= 1)
    {
        if ((nValueIn & 1))
            break;
        nZeros++;
    };

    return nZeros;
};

static int CountTrailingZeros(uint64_t nValueIn) {
    int nZeros = 0;

    uint64_t mask = ((uint64_t)1) << 63;
    for (size_t i = 0; i < 64; ++i, nValueIn <<= 1)
    {
        if ((nValueIn & mask))
            break;
        nZeros++;
    };

    return nZeros;
};

static int64_t ipow(int64_t base, int exp) {
    int64_t result = 1;
    while (exp)
    {
        if (exp & 1)
            result *= base;
        exp >>= 1;
        base *= base;
    };
    return result;
};


int SelectRangeProofParameters(uint64_t nValueIn, uint64_t &minValue, int &exponent, int &nBits) {
    int nLeadingZeros = CountLeadingZeros(nValueIn);
    int nTrailingZeros = CountTrailingZeros(nValueIn);

    size_t nBitsReq = 64 - nLeadingZeros - nTrailingZeros;

    // TODO: drop low value bits to fee

    if (nValueIn == 0)
    {
        exponent = GetRandInt(5);
        if (GetRandInt(10) == 0) // sometimes raise the exponent
            nBits += GetRandInt(5);
        return 0;
    };


    uint64_t nTest = nValueIn;
    size_t nDiv10; // max exponent
    for (nDiv10 = 0; nTest % 10 == 0; nDiv10++, nTest /= 10) ;


    // TODO: how to pick best?

    int eMin = nDiv10 / 2;
    exponent = eMin + GetRandInt(nDiv10-eMin);


    nTest = nValueIn / ipow(10, exponent);

    nLeadingZeros = CountLeadingZeros(nTest);
    nTrailingZeros = CountTrailingZeros(nTest);

    nBitsReq = 64 - nTrailingZeros;


    if (nBitsReq > 32)
    {
        nBits = nBitsReq;
    };

    // make multiple of 4
    while (nBits < 63 && nBits % 4 != 0)
        nBits++;

    return 0;
};

bool GetRangeProofInfo(const std::vector<uint8_t> &vRangeproof, int &rexp, int &rmantissa, CAmount &min_value, CAmount &max_value)
{
    int ret = secp256k1_rangeproof_info(secp256k1_ctx_blind,
                                        &rexp, &rmantissa, (uint64_t*) &min_value, (uint64_t*) &max_value,
                                        &vRangeproof[0], vRangeproof.size());
    return ret == 1;
};

