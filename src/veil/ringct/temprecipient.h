// Copyright (c) 2017-2019 The Particl Core developers
// Copyright (c) 2018-2019 Veil developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef VEIL_TEMPRECIPIENT_H
#define VEIL_TEMPRECIPIENT_H

#include <script/script.h>
#include <amount.h>
#include <secp256k1/include/secp256k1_rangeproof.h>
#include <script/standard.h>
#include <key.h>
#include <veil/ringct/extkey.h>

class CTempRecipient
{
public:
    CTempRecipient() : nType(0), nAmount(0), nAmountSelected(0), fSubtractFeeFromAmount(false) {SetNull();};
    CTempRecipient(CAmount nAmount_, bool fSubtractFeeFromAmount_, CScript scriptPubKey_)
        : nAmount(nAmount_), nAmountSelected(nAmount_), fSubtractFeeFromAmount(fSubtractFeeFromAmount_), scriptPubKey(scriptPubKey_) {SetNull();};

    void SetNull()
    {
        fNonceSet = false; // if true use nonce and vData from CTempRecipient
        fScriptSet = false;
        fChange = false;
        nChildKey = 0;
        nStealthPrefix = 0;
        fSplitBlindOutput = false;
        fExemptFeeSub = false;
        fZerocoin = false;
        fZerocoinMint = false;
    };

    void SetAmount(CAmount nValue)
    {
        nAmount = nValue;
        nAmountSelected = nValue;
    };

    bool ApplySubFee(CAmount nFee, size_t nSubtractFeeFromAmount, bool &fFirst);

    uint8_t nType;
    CAmount nAmount;            // If fSubtractFeeFromAmount, nAmount = nAmountSelected - feeForOutput
    CAmount nAmountSelected;
    bool fSubtractFeeFromAmount;
    bool fSplitBlindOutput;
    bool fExemptFeeSub;         // Value too low to sub fee when blinded value split into two outputs
    bool fZerocoin;
    bool fZerocoinMint;
    CTxDestination address;
    bool isMine;
    CScript scriptPubKey;
    std::vector<uint8_t> vData;
    std::vector<uint8_t> vBlind;
    std::vector<uint8_t> vRangeproof;
    secp256k1_pedersen_commitment commitment;
    uint256 nonce;

    // TODO: range proof parameters, try to keep similar for fee
    // Allow an overwrite of the parameters.
    bool fOverwriteRangeProofParams = false;
    uint64_t min_value;
    int ct_exponent;
    int ct_bits;

    CKey sEphem;
    CPubKey pkTo;
    int n;
    std::string sNarration;
    bool fScriptSet;
    bool fChange;
    bool fLastBlindDummy;
    bool fNonceSet;
    uint32_t nChildKey; // update later
    uint32_t nStealthPrefix;
};

#endif //VEIL_TEMPRECIPIENT_H
