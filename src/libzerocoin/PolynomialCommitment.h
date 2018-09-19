/**
* @file       PolynomialCommitment.h
*
* @brief      PolynomialCommitment class for the Zerocoin library.
*
* @author     Mary Maller, Jonathan Bootle and Gian Piero Dionisio
* @date       April 2018
*
* @copyright  Copyright 2018 The PIVX Developers
* @license    This project is released under the MIT license.
**/

#pragma once
#include "Coin.h"
#include "zkplib.h"

using namespace std;

namespace libzerocoin {

class PolynomialCommitment {
public:
    PolynomialCommitment(){};
    PolynomialCommitment(const ZerocoinParams* ZCp);
    PolynomialCommitment(const ZerocoinParams* ZCp, const CBN_vector Tf, const CBN_vector Trho, const CBigNum U, const CBN_vector tbar, const CBigNum taubar, const CBN_vector xPowersPos, const CBN_vector xPowersNeg);
    void Commit(const CBN_vector tpolynomial);
    void Eval(const CBN_vector xPowersPositive, const CBN_vector xPowersNegative);
    bool Verify(CBigNum& value);     // if Verify==true --> value = t(x)
    // commitments
    CBN_vector Tf;      // ZKP_M1DASH commitments to fVector
    CBN_vector Trho;    // ZKP_M2DASH commitments to rhoVector
    CBigNum U;          // commitment to u
    // evaluation
    CBN_vector tbar;    // length ZKP_NDASH vector computed with fVector, rhoVector, u and  xPowers
    CBigNum taubar;     // value computed with fBlinders, rhoBlinders, uBlinder and xPowers

private:
    const ZerocoinParams* params;
    // evaluation point
    CBN_vector xPowersPos;
    CBN_vector xPowersNeg;

    // state (coefficients vectors + randomness)
    CBN_vector u;           // random blinding vector of len ZKP_NDASH with last element=0
    CBN_matrix fVector;     // ZKP_M1DASH x ZKP_NDASH matrix with coefficients of tpolynomial
    CBN_matrix rhoVector;   // ZKP_M2DASH x ZKP_NDASH matrix with coefficients of tpolynomial
    CBN_vector fBlinders;   // a list of ZKP_M1DASH random values in Z_q
    CBN_vector rhoBlinders; // a list of ZKP_M2DASH random values in Z_q
    CBigNum uBlinder;       // random vlaue in Z_q
};

} /* namespace libzerocoin */
