/**
* @file       ArithmeticCircuit.h
*
* @brief      ArithmeticCircuit class for the Zerocoin library.
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

class ArithmeticCircuit {
public:
    ArithmeticCircuit(const ZerocoinParams* p);
    CBN_matrix A;               // left input wires
    CBN_matrix B;               // right input wires
    CBN_matrix C;               // output wires
    CBN_vector K;               // constraints vector
    // w-Polynomials
    CBN_vector YPowers;
    CBN_vector YDash;
    CBigNum Kconst;
    CBN_vector y_vec_neg;
    void setWireValues(const PrivateCoin& coin);
    static void setPreConstraints(const ZerocoinParams* params, vector<CBN_matrix>& wA, vector<CBN_matrix>& wB, vector<CBN_matrix>& wC, CBN_vector& K);
    static void set_s_poly(const ZerocoinParams* params,
            std::vector< std::vector< std::pair<int, CBigNum> > >& s_a1, std::vector< std::vector< std::pair<int, CBigNum> > >& s_a2,
            std::vector< std::vector< std::pair<int, CBigNum> > >& s_b1, std::vector< std::vector< std::pair<int, CBigNum> > >& s_b2,
            std::vector< std::vector< std::pair<int, CBigNum> > >& s_c1, std::vector< std::vector< std::pair<int, CBigNum> > >& s_c2);
    void setYPoly(const CBigNum& y);
    const CBigNum& getSerialNumber() const { return this->serialNumber; }
    const CBigNum& getRandomness() const { return this->randomness; }
    CBigNum sumWiresDotWs(const int i);    // Evaluate the sums in Equation (1) of the paper
    CBigNum AiDotBiYDash(const int i);     // Evaluate dotProduct(A[i], hadamard(B[i], YDash)
    bool check();                          // perform tests on circuit assignment
    void set_Kconst(CBN_vector& YPowers, const CBigNum serial);
private:
    const ZerocoinParams* params;
    CBigNum serialNumber;       // coin serial number S
    CBigNum randomness;         // coin randomness v
    vector<int> r_bits;               // randomness binary decomposition
    CBigNum y;                        // indeterminate of the polynomial equation
    std::vector< std::vector< std::pair<int, CBigNum> > > s_poly_a1, s_poly_a2;
    std::vector< std::vector< std::pair<int, CBigNum> > > s_poly_b1, s_poly_b2;
    std::vector< std::vector< std::pair<int, CBigNum> > > s_poly_c1, s_poly_c2;
    void set_YPowers();
    void set_YDash();
    };

} /* namespace libzerocoin */
