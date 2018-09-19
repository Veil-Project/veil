/**
* @file       PolynomialCommitment.cpp
*
* @brief      PolynomialCommitment class for the Zerocoin library.
*
* @author     Mary Maller, Jonathan Bootle and Gian Piero Dionisio
* @date       April 2018
*
* @copyright  Copyright 2018 The PIVX Developers
* @license    This project is released under the MIT license.
**/

#include "PolynomialCommitment.h"

using namespace libzerocoin;

PolynomialCommitment::PolynomialCommitment(const ZerocoinParams* ZCp):
        Tf(ZKP_M1DASH),
        Trho(ZKP_M2DASH),
        tbar(ZKP_NDASH),
        params(ZCp),
        xPowersPos(ZKP_M2DASH*ZKP_NDASH+1),
        xPowersNeg(ZKP_M1DASH*ZKP_NDASH+1),
        u(ZKP_NDASH),
        fVector(ZKP_M1DASH, CBN_vector(ZKP_NDASH)),
        rhoVector(ZKP_M2DASH, CBN_vector(ZKP_NDASH)),
        fBlinders(ZKP_M1DASH),
        rhoBlinders(ZKP_M2DASH)
{};

// Constructor for SoK Verification
PolynomialCommitment::PolynomialCommitment(const ZerocoinParams* ZCp, const CBN_vector Tf, const CBN_vector Trho, const CBigNum U,
        const CBN_vector tbar, const CBigNum taubar, const CBN_vector xPowersPos, const CBN_vector xPowersNeg):
            Tf(Tf),
            Trho(Trho),
            U(U),
            tbar(tbar),
            taubar(taubar),
            params(ZCp),
            xPowersPos(xPowersPos),
            xPowersNeg(xPowersNeg)
{};


void PolynomialCommitment::Commit(const CBN_vector tpolynomial)
{
    const CBigNum q = params->serialNumberSoKCommitmentGroup.groupOrder;
    const CBigNum p = params->serialNumberSoKCommitmentGroup.modulus;
    const int n = ZKP_NDASH;
    const int m1 = ZKP_M1DASH;
    const int m2 = ZKP_M2DASH;
    // --------------------------------- **** POLY-COMMIT **** ----------------------------------
    // ------------------------------------------------------------------------------------------
    // Algorithm used by the signer to commit to the coefficients of a polynomial equation
    // @param   tpolynomial :  A (m1+m2)n+1 array coefficients of a polynomial equation
    // @init    commitments :  pc = [Tf, Trho, U]
    // @int     state       :  st = [u, fVector, rhoVector, fBlinders, rhoBlinders, uBlinder]

    // chose a random array u and set last component to 0
    random_vector_mod(u,q);
    u[n-1] = CBigNum(0);

    // Compute fVector  --  t_{i,j}' in the original paper
    for(int i=0; i<m1; i++) for(int j=0; j<n; j++)
        fVector[i][j] = tpolynomial[i*n+j];

    // Compute rhoVector  --  t_{i,j}'' in the original paper
    for(int i=0; i<m2; i++) for(int j=0; j<n; j++)
        rhoVector[i][j] = tpolynomial[(i+m1)*n+j+1];
    for(int j=1; j<n; j++) {
        rhoVector[0][j] -= u[j-1];
        rhoVector[0][j] %= q;
    }

    // Set random values to blind the value in the final commitment
    random_vector_mod(fBlinders,q);
    random_vector_mod(rhoBlinders,q);
    uBlinder = CBigNum::randBignum(q);

    // Commit to the top-half of the matrix
    for(int i=0; i<m1; i++)
        Tf[i] = pedersenCommitment(params,fVector[i],fBlinders[i]);

    // Commit to the bottom-half of the matrix
    for(int i=0; i<m2; i++)
        Trho[i] = pedersenCommitment(params,rhoVector[i],rhoBlinders[i]);

    // Commit to the blinders
    U = pedersenCommitment(params,u,uBlinder);
}


void PolynomialCommitment::Eval(const CBN_vector xPowersPositive, const CBN_vector xPowersNegative)
{
    const CBigNum q = params->serialNumberSoKCommitmentGroup.groupOrder;
    const int n = ZKP_NDASH;
    const int m1 = ZKP_M1DASH;
    const int m2 = ZKP_M2DASH;
    // ---------------------------------- **** POLY-EVAL **** -----------------------------------
    // ------------------------------------------------------------------------------------------
    // Algorithm used by the signer to produce a proof of evaluation (show that a committed
    // polynomial evaluates to a specific value at a known point x).
    // @param   xPowersPos :  a list of M2DASH x NDASH + 1 precomputed positive powers of x
    // @param   xPowersNeg :  a list of M1DASH x NDASH + 1 precomputed negative powers of x
    // @init    evaluation :  pe = [tbar, taubar]
    xPowersPos = xPowersPositive;
    xPowersNeg = xPowersNegative;


    // Compute tbar
    CBigNum tbarSum;
    for(int j=0; j<n; j++) {
        tbarSum = CBigNum(0);
        for(int i=0; i<m1; i++)
            tbarSum = ( tbarSum + xPowersNeg[(m1-i)*n].mul_mod(fVector[i][j],q) ) % q;
        for(int i=0; i<m2; i++)
            tbarSum = ( tbarSum + xPowersPos[i*n+1].mul_mod(rhoVector[i][j],q) ) % q;
        tbarSum = ( tbarSum + xPowersPos[2].mul_mod(u[j],q) ) % q;
        tbar[j] = tbarSum;
    }

    // Compute taubar
    taubar = CBigNum(0);
    for(int i=0; i<m1; i++)
        taubar = ( taubar + xPowersNeg[(m1-i)*n].mul_mod(fBlinders[i],q) ) % q;
    for(int i=0; i<m2; i++)
        taubar = ( taubar + xPowersPos[i*n+1].mul_mod(rhoBlinders[i],q) ) % q;
    taubar = ( taubar + xPowersPos[2].mul_mod(uBlinder,q) ) % q;

}



bool PolynomialCommitment::Verify(CBigNum& value)
{
    const CBigNum q = params->serialNumberSoKCommitmentGroup.groupOrder;
    const CBigNum p = params->serialNumberSoKCommitmentGroup.modulus;
    const int n = ZKP_NDASH;
    const int m1 = ZKP_M1DASH;
    const int m2 = ZKP_M2DASH;

    // --------------------------------- **** POLY-VERIFY **** ----------------------------------
    // ------------------------------------------------------------------------------------------
    // Algorithm used by the verifier to verify the proof of polynomial evaluation
    // @param(ref)  value :  if the algorithm returns true the value t(x) is saved into 'value'
    // @return      bool  :  result of the verification

    // Commit to tbar with randomness taubar
    CBigNum C = pedersenCommitment(params, tbar, taubar);

    // Find powers of commitments to Tf, Trho and U
    CBigNum test = CBigNum(1);
    CBigNum T;

    for(int i=0; i<m1; i++) {
        T = Tf[i].pow_mod(xPowersNeg[(m1-i)*n],p);
        test = test.mul_mod(T,p);
    }

    for(int i=0; i<m2; i++) {
        T = Trho[i].pow_mod(xPowersPos[i*n+1],p);
        test = test.mul_mod(T,p);
    }

    T = U.pow_mod(xPowersPos[2],p);
    test = test.mul_mod(T,p);

    // Perform the test
    if( C != test ) {
        LogPrintf("Polynomial Commitment Verification false.\nC=%s\ntest=%s",C.ToString(),test.ToString());
        return false;
    }

    // Compute v
    value = dotProduct(tbar, xPowersPos, q, n);

    return true;
}


