#include "chainparams.h"
#include "libzerocoin/ArithmeticCircuit.h"
#include "libzerocoin/PolynomialCommitment.h"
#include "libzerocoin/Bulletproofs.h"
#include "libzerocoin/SerialNumberSoK_small.h"
#include "libzerocoin/SerialNumberSignatureOfKnowledge.h"
#include <boost/test/unit_test.hpp>
#include <iostream>
#include <time.h>
#include <random.h>


using namespace libzerocoin;

#define COLOR_STR_NORMAL  "\033[0m"
#define COLOR_BOLD        "\033[1m"
#define COLOR_STR_GREEN   "\033[32m"
#define COLOR_STR_RED     "\033[31m"
#define COLOR_CYAN        "\033[0;36m"
#define COLOR_MAGENTA     "\u001b[35m"

std::string colorNormal(COLOR_STR_NORMAL);
std::string colorBold(COLOR_BOLD);
std::string colorGreen(COLOR_STR_GREEN);
std::string colorRed(COLOR_STR_RED);
std::string colorCyan(COLOR_CYAN);
std::string colorMagenta(COLOR_MAGENTA);

// Global test counters
uint32_t    zNumTests        = 0;
uint32_t    zSuccessfulTests = 0;

std::string Pass(bool fReverseTest = false)
{
    return fReverseTest ? "[FAIL (good)]" : "[PASS]";
}

std::string Fail(bool fReverseTest = false)
{
    return fReverseTest ? "[PASS (when it shouldn't!)]" : "[FAIL]";
}


// Parameters ----------------------------------------------------------------------------------------

bool Test_generators(IntegerGroupParams SoKGroup)
{
    zNumTests++;
    std::cout << "- Testing generators...";
    for(unsigned int i=0; i<512; i++) {
        if ( SoKGroup.gis[i].pow_mod(SoKGroup.groupOrder,SoKGroup.modulus) != CBigNum(1)) {
            std::cout << colorRed << Fail() << std::endl;
            std::cout << "gis[" << i << "] ** q != 1" << colorNormal << std::endl;
            return false;
        }
    }
    std::cout << colorGreen << Pass() << colorNormal << std::endl;
    zSuccessfulTests++;
    return true;
}


bool parameters_tests()
{
    std::cout << colorBold << "*** parameters_tests ***" << std::endl;
    std::cout << "------------------------" << colorNormal << std::endl;

    bool finalResult = true;

    SelectParams(CBaseChainParams::MAIN);
    ZerocoinParams *ZCParams = Params().Zerocoin_Params();
    (void)ZCParams;

    finalResult = finalResult & Test_generators(ZCParams->serialNumberSoKCommitmentGroup);

    std::cout << std::endl;

    return finalResult;
}

// ---------------------------------------------------------------------------------------------------
// Arithmetic Circuit --------------------------------------------------------------------------------
bool Test_multGates(ArithmeticCircuit ac, CBigNum q)
{
    zNumTests++;
    // If multiplication gates hold this should be true
    std::cout << "- Testing A times B equals C...";
    for(unsigned int i=0; i<ZKP_M; i++) for(unsigned int j=0; j<ZKP_N; j++) {
            if(ac.A[i][j].mul_mod(ac.B[i][j], q) != ac.C[i][j]) {
                std::cout << colorRed << Fail() << std::endl;
                std::cout << "Hadamard Test failed at i=" << i << ", j=" << j << colorNormal << std::endl;
                return false;
            }
        }

    std::cout << colorGreen << Pass()  << colorNormal << std::endl;
    zSuccessfulTests++;
    return true;
}

bool Test_cfinalLog(ArithmeticCircuit ac, CBigNum q, CBigNum a, CBigNum b, bool fReverseTest = false)
{
    zNumTests++;
    // If circuit correctly evaluates (a^serial)*(b^randomness) this should be true
    std::cout << "- Testing C_final equals Logarithm";
    if (fReverseTest)
        std::cout << colorMagenta << " with wrong assignment" << colorNormal;
    std::cout << "...";
    CBigNum logarithm =
            a.pow_mod(ac.getSerialNumber(),q).mul_mod(
                    b.pow_mod(ac.getRandomness(),q),q);
    CBigNum Cfinal = ac.C[ZKP_M-1][0];
    bool test = (logarithm == Cfinal);
    if (test == fReverseTest) {
        std::cout << colorRed << Fail(fReverseTest) << colorNormal << std::endl;
        return false;
    }

    std::cout << colorGreen << Pass(fReverseTest) << colorNormal << std::endl;
    zSuccessfulTests++;
    return true;
}

bool Test_arithConstraints(ArithmeticCircuit ac, CBigNum q, bool fReverseTest = false)
{
    zNumTests++;
    // Checking that the expressions in Equation (2) of the paper hold
    std::cout << "- Testing the Arithmetic Constraints (eq. 2)";
    if (fReverseTest)
        std::cout << colorMagenta << " with wrong assignment" << colorNormal;
    std::cout << "...";
    bool test = true;
    unsigned int last_index = 0;
    for(unsigned int i=0; i<4*ZKP_SERIALSIZE-2; i++) {
        if (ac.sumWiresDotWs(i) != ac.K[i] % q) {
            test = false;
            last_index = i;
            break;
        }
    }

    if (test == fReverseTest) {
        std::cout << colorRed << Fail(fReverseTest) << std::endl;
        std::cout << "Arithmetic Constraints Test failed at i=" << last_index << colorNormal << std::endl;
        return false;
    }

    std::cout << colorGreen << Pass(fReverseTest)  << colorNormal << std::endl;
    zSuccessfulTests++;
    return true;
}


bool arithmetic_circuit_tests()
{
    std::cout << colorBold << "*** arithmetic_circuit_tests ***" << std::endl;
    std::cout << "--------------------------------" << colorNormal << std::endl;

    bool finalResult = true;

    SelectParams(CBaseChainParams::MAIN);
    ZerocoinParams *ZCParams = Params().Zerocoin_Params();
    (void)ZCParams;

    CBigNum a = ZCParams->coinCommitmentGroup.g;
    CBigNum b = ZCParams->coinCommitmentGroup.h;
    CBigNum q = ZCParams->serialNumberSoKCommitmentGroup.groupOrder;

    // mint a coin
    PrivateCoin coin(ZCParams, CoinDenomination::ZQ_TEN, true);
    // get random Y
    CBigNum Y = CBigNum::randBignum(q);
    ArithmeticCircuit circuit(ZCParams);
    circuit.setWireValues(coin);
    circuit.setYPoly(Y);

    finalResult = finalResult & Test_multGates(circuit, q);
    finalResult = finalResult & Test_cfinalLog(circuit, q, a, b);
    finalResult = finalResult & Test_arithConstraints(circuit, q);

    // !TODO: rewrite this test case.
    // Checking that the expressions in Equation (3) of the paper hold
    // (circuit.sumWiresDotWPoly() == circuit.Kconst)

    // New circuit with random assignment
    ArithmeticCircuit newCircuit(circuit);
    for(unsigned int i=0; i<ZKP_M; i++) {
        random_vector_mod(newCircuit.A[i], q);
        random_vector_mod(newCircuit.B[i], q);
        for(unsigned int j=0; j<ZKP_N; j++)
            newCircuit.C[i][j] = newCircuit.A[i][j].mul_mod(newCircuit.B[i][j],q);
    }

    // If circuit correctly evaluates (a^serial)*(b^randomness) we have a problem
    finalResult = finalResult & Test_cfinalLog(newCircuit, q, a, b, true);

    // Checking that the expressions in Equation (2) of the paper does not hold
    finalResult = finalResult & Test_arithConstraints(newCircuit, q, true);

    // !TODO: rewrite this test case.
    // Checking that the expressions in Equation (3) of the does not paper hold

    std::cout << std::endl;

    return finalResult;
}


// ---------------------------------------------------------------------------------------------------
// Polynomial Commitment -----------------------------------------------------------------------------

// Evaluate tpolynomial at x
CBigNum eval_tpoly(CBN_vector tpoly, CBN_vector xPowersPos, CBN_vector xPowersNeg, CBigNum q)
{
    CBigNum sum = CBigNum(0);
    for(unsigned int i=0; i<=ZKP_NDASH*ZKP_M1DASH; i++)
        sum = ( sum + tpoly[i].mul_mod(xPowersNeg[ZKP_NDASH*ZKP_M1DASH-i],q) ) % q;
    for(unsigned int i=ZKP_NDASH*ZKP_M1DASH+1; i<=ZKP_NDASH*(ZKP_M1DASH+ZKP_M2DASH); i++)
        sum = ( sum + tpoly[i].mul_mod(xPowersPos[i-ZKP_NDASH*ZKP_M1DASH],q) ) % q;
    return sum;
}

bool Test_polyVerify1(PolynomialCommitment pc, CBigNum &val, bool fReverseTest = false)
{
    zNumTests++;
    // Poly-Verify: For honest prover, verifier should be satisfied
    std::cout << "- Testing PolyVerify";
    if (fReverseTest)
        std::cout << colorMagenta << " for dishonest prover" << colorNormal;
    std::cout << "...";
    // val = t(x) if proofs checks out
    bool test = (pc.Verify(val));
    if (test == fReverseTest) {
        std::cout << colorRed << Fail(fReverseTest) << colorNormal << std::endl;
        return false;
    }

    std::cout << colorGreen << Pass(fReverseTest)  << colorNormal << std::endl;
    zSuccessfulTests++;
    return true;
}

bool Test_polyVerify2(CBigNum val, CBN_vector tpoly,
                      CBN_vector xpos, CBN_vector xneg, CBigNum q)
{
    zNumTests++;
    // Poly-Verify: For honest prover, verifier is able to compute t(x)
    std::cout << "- Testing t(x) == dotProduct(tbar,xPowersPos)...";
    CBigNum tx = eval_tpoly(tpoly, xpos, xneg, q);
    if (val != tx) {
        std::cout << colorRed << Fail() << colorNormal << std::endl;
        return false;
    }

    std::cout << colorGreen << Pass()  << colorNormal << std::endl;
    zSuccessfulTests++;
    return true;
}

bool polynomial_commitment_tests()
{
    std::cout << colorBold << "*** polynomial_commitment_tests ***" << std::endl;
    std::cout << "-----------------------------------" << colorNormal << std::endl;

    bool finalResult = true;
    SelectParams(CBaseChainParams::MAIN);
    ZerocoinParams *ZCParams = Params().Zerocoin_Params();
    (void)ZCParams;

    CBigNum q = ZCParams->serialNumberSoKCommitmentGroup.groupOrder;
    CBigNum p = ZCParams->serialNumberSoKCommitmentGroup.modulus;

    // generate a random tpolynomial with 0 constant term
    CBN_vector tpoly(ZKP_NDASH*(ZKP_M1DASH+ZKP_M2DASH)+1);
    for(unsigned int i=0; i<tpoly.size(); i++)
        tpoly[i] = CBigNum::randBignum(q);
    tpoly[ZKP_M1DASH*ZKP_NDASH] = CBigNum(0);

    // generate a random evaluation point x in R and compute powers
    CBigNum x = CBigNum::randBignum(q);
    CBN_vector xPowersPositive(ZKP_M2DASH*ZKP_NDASH+1);
    CBN_vector xPowersNegative(ZKP_M1DASH*ZKP_NDASH+1);
    xPowersPositive[0] = xPowersNegative[0] = CBigNum(1);
    xPowersPositive[1] = x;
    xPowersNegative[1] = x.pow_mod(-1,q);
    for(unsigned int i=2; i<ZKP_M2DASH*ZKP_NDASH+1; i++)
        xPowersPositive[i] = x.pow_mod(i,q);
    for(unsigned int i=2; i<ZKP_M1DASH*ZKP_NDASH+1; i++)
        xPowersNegative[i] = x.pow_mod(-(int)i,q);

    // Poly-Commit and Poly-Evaluate
    PolynomialCommitment polyCommitment(ZCParams);
    polyCommitment.Commit(tpoly);
    polyCommitment.Eval(xPowersPositive, xPowersNegative);

    // Polynomial  evaluation
    CBigNum val;

    finalResult = finalResult & Test_polyVerify1(polyCommitment, val);
    finalResult = finalResult & Test_polyVerify2(val, tpoly, xPowersPositive, xPowersNegative, q);

    // Create copies of the polynomial commitment and mess things up
    PolynomialCommitment newPolyComm1(polyCommitment);
    PolynomialCommitment newPolyComm2(polyCommitment);
    PolynomialCommitment newPolyComm3(polyCommitment);
    random_vector_mod(newPolyComm1.tbar, q);
    random_vector_mod(newPolyComm2.Tf, q);
    random_vector_mod(newPolyComm3.Trho, q);

    // Poly-Verify: For dishonest prover, verifier should fail the test
    finalResult = finalResult & Test_polyVerify1(newPolyComm1, val, true);
    finalResult = finalResult & Test_polyVerify1(newPolyComm2, val, true);
    finalResult = finalResult & Test_polyVerify1(newPolyComm3, val, true);

    std::cout << std::endl;

    return finalResult;
}

// ---------------------------------------------------------------------------------------------------
// Inner Product Argument ----------------------------------------------------------------------------

// !TODO: Adapt to bulletproofs class
/*
BOOST_AUTO_TEST_CASE(inner_product_argument_tests)
{
    std::cout << "*** inner_product_argument_tests ***" << std::endl;
    std::cout << "------------------------------------" << std::endl;
    SelectParams(CBaseChainParams::MAIN);
    ZerocoinParams *ZCParams = Params().Zerocoin_Params(false);
    (void)ZCParams;
    CBigNum q = ZCParams->serialNumberSoKCommitmentGroup.groupOrder;
    CBigNum p = ZCParams->serialNumberSoKCommitmentGroup.modulus;
    // Get random y in Z_q and (N+PADS)-vectors
    CBigNum y = CBigNum::randBignum(q);
    CBN_vector a_sets(ZKP_N+ZKP_PADS);
    CBN_vector b_sets(ZKP_N+ZKP_PADS);
    random_vector_mod(a_sets, q);
    random_vector_mod(b_sets, q);
    // Inner-product PROVE
    InnerProductArgument innerProduct(ZCParams);
    innerProduct.Prove(y, a_sets, b_sets);
    // compute ck_inner sets
    pair<CBN_vector, CBN_vector> resultSets = innerProduct.ck_inner_gen(ZCParams, y);
    CBN_vector ck_inner_g = resultSets.first;
    CBN_vector ck_inner_h = resultSets.second;
    // Compute commitment A to a_sets under ck_inner_g
    CBigNum A = CBigNum(1);
    for(unsigned int i=0; i<a_sets.size(); i++)
        A = A.mul_mod(ck_inner_g[i].pow_mod(a_sets[i], p), p);
    // Compute commitment B to b_sets under ck_inner_h
    CBigNum B = CBigNum(1);
    for(unsigned int i=0; i<b_sets.size(); i++)
        B = B.mul_mod(ck_inner_h[i].pow_mod(b_sets[i], p), p);
    // Inner product z = <a,b>
    CBigNum z = dotProduct(a_sets, b_sets, q);
    // random_z != z
    CBigNum random_z = CBigNum::randBignum(q);
    while( random_z == dotProduct(a_sets, b_sets, q) ) random_z = CBigNum::randBignum(q);
    // innerProductVerify
    std::cout << "- Testing innerProductVerify..." << std::endl;
    bool res = innerProduct.Verify(ZCParams, y, A, B, z);
    BOOST_CHECK_MESSAGE(res,"InnerProduct:: Verification failed\n");
    // Inner product z != <a, b>  (z = 0, z = 1, z = random)
    std::cout << "- Testing innerProductVerify for dishonest prover..." << std::endl;
    BOOST_CHECK_MESSAGE( !innerProduct.Verify(ZCParams, y, A, B, CBigNum(0)),
            "InnerProduct:: Verification returned TRUE[1] for dishonest prover\n");
    BOOST_CHECK_MESSAGE( !innerProduct.Verify(ZCParams, y, A, B, CBigNum(1)),
            "InnerProduct:: Verification returned TRUE[2] for dishonest prover\n");
    BOOST_CHECK_MESSAGE( !innerProduct.Verify(ZCParams, y, A, B, random_z),
            "InnerProduct:: Verification returned TRUE[3] for dishonest prover\n");
    std::cout << std::endl;
}
*/

// ---------------------------------------------------------------------------------------------------
// Signature Of Knowledge ----------------------------------------------------------------------------

void printTime(clock_t start_time, int nProofs)
{
    clock_t total_time = clock() - start_time;
    double passed = total_time*1000.0/CLOCKS_PER_SEC;
    std::string strPerProof = std::to_string(passed/nProofs) + " msec per proof";
    std::cout << colorCyan << "\t(" << passed << " msec " << (nProofs ? strPerProof.c_str() : "") << ")"  << colorNormal << std::endl;
}

bool Test_batchVerify(std::vector<SerialNumberSoKProof> proofs, bool fReverseTest = false)
{
    zNumTests++;
    // verify the signature of the received SoKs
    std::cout << "- Verifying the Signatures of Knowledge";
    if (fReverseTest)
        std::cout << colorMagenta << " for dishonest prover" << colorNormal;
    std::cout << "...";

    if (SerialNumberSoKProof::BatchVerify(proofs) == fReverseTest) {
        std::cout << colorRed << Fail(fReverseTest) << colorNormal;
        return false;
    }

    std::cout << colorGreen << Pass(fReverseTest) << colorNormal;
    zSuccessfulTests++;
    return true;
}

bool batch_signature_of_knowledge_tests(unsigned int start, unsigned int end, unsigned int step)
{
    if (end < start || step < 1) {
        std::cout << "wrong range for batch_signature_of_knowledge_tests";
        return false;
    }

    std::cout << colorBold <<  "*** batch_signature_of_knowledge_tests ***" << std::endl;
    std::cout << "------------------------------------------" << colorNormal <<  std::endl;
    std::cout << "starting size of the list: " << start << std::endl;
    std::cout << "ending size of the list: " << end << std::endl;
    std::cout << "step increment: " << step << std::endl;

    bool finalResult = true;
    SelectParams(CBaseChainParams::MAIN);
    ZerocoinParams *ZCParams = Params().Zerocoin_Params();
    (void)ZCParams;

    for(unsigned int k=start; k<=end; k=k+step) {

        // create k random message hashes
        std::vector<uint256> msghashList;
        for(unsigned int i=0; i<k; i++) {
            CBigNum rbn = CBigNum::randBignum(256);
            msghashList.push_back(rbn.getuint256());
        }

        // mint k coins
        std::vector<PrivateCoin> coinList;
        for(unsigned int i=0; i<k; i++) {
            PrivateCoin newCoin(ZCParams, CoinDenomination::ZQ_TEN);
            coinList.push_back(newCoin);
        }

        // commit to these coins
        std::vector<Commitment> commitmentList;
        for(unsigned int i=0; i<k; i++) {
            const CBigNum newCoin_value = coinList[i].getPublicCoin().getValue();
            Commitment commitment(&(ZCParams->serialNumberSoKCommitmentGroup), newCoin_value);
            commitmentList.push_back(commitment);
        }

        // WRONG (random) assignments
        // random messages
        std::vector<uint256> msghashList2;
        for(unsigned int i=0; i<k; i++) {
            CBigNum rbn = CBigNum::randBignum(256);
            msghashList2.push_back(rbn.getuint256());
        }
        // random coins
        std::vector<PrivateCoin> coinList2;
        for(unsigned int i=0; i<k; i++) {
            PrivateCoin newCoin(ZCParams, CoinDenomination::ZQ_TEN);
            coinList2.push_back(newCoin);
        }
        // commit to these coins
        std::vector<Commitment> commitmentList2;
        for(unsigned int i=0; i<k; i++) {
            const CBigNum newCoin_value = coinList2[i].getPublicCoin().getValue();
            Commitment commitment(&(ZCParams->serialNumberSoKCommitmentGroup), newCoin_value);
            commitmentList2.push_back(commitment);
        }


        std::cout << "- Creating array of " << k << " Signatures of Knowledge...";

        // create k signatures of knowledge
        std::vector<SerialNumberSoK_small> sigList;

        clock_t start_time = clock();

        for(unsigned int i=0; i<k; i++) {
            SerialNumberSoK_small sigOfKnowledge(ZCParams, coinList[i], commitmentList[i], msghashList[i]);
            sigList.push_back(sigOfKnowledge);
        }

        printTime(start_time, 0);
        start_time = clock();
        std::cout << "- Packing and serializing the Signatures..." << std::endl;

        // pack the signatures of knowledge (honest prover)
        std::vector<SerialNumberSoKProof> proofs;
        for(unsigned int i=0; i<k; i++) {
            SerialNumberSoKProof proof(sigList[i], coinList[i].getSerialNumber(),
                                       commitmentList[i].getCommitmentValue(), msghashList[i]);
            proofs.push_back(proof);
        }

        // pack the signatures of knowledge (wrong msghash)
        std::vector<SerialNumberSoKProof> proofs2;
        for(unsigned int i=0; i<k; i++) {
            SerialNumberSoKProof proof(sigList[i], coinList[i].getSerialNumber(),
                                       commitmentList[i].getCommitmentValue(), msghashList2[i]);
            proofs2.push_back(proof);
        }

        // pack the signatures of knowledge (wrong commitment)
        std::vector<SerialNumberSoKProof> proofs3;
        for(unsigned int i=0; i<k; i++) {
            SerialNumberSoKProof proof(sigList[i], coinList[i].getSerialNumber(),
                                       commitmentList2[i].getCommitmentValue(), msghashList[i]);
            proofs3.push_back(proof);
        }

        // pack the signatures of knowledge (wrong coin and commitment)
        std::vector<SerialNumberSoKProof> proofs4;
        for(unsigned int i=0; i<k; i++) {
            SerialNumberSoKProof proof(sigList[i], coinList2[i].getSerialNumber(),
                                       commitmentList2[i].getCommitmentValue(), msghashList[i]);
            proofs4.push_back(proof);
        }

        // serialize the proofs to a CDataStream object.
        std::vector<CDataStream> serializedProofs(proofs.size(), CDataStream(SER_NETWORK, PROTOCOL_VERSION));
        std::vector<CDataStream> serializedProofs2(proofs2.size(), CDataStream(SER_NETWORK, PROTOCOL_VERSION));
        std::vector<CDataStream> serializedProofs3(proofs3.size(), CDataStream(SER_NETWORK, PROTOCOL_VERSION));
        std::vector<CDataStream> serializedProofs4(proofs4.size(), CDataStream(SER_NETWORK, PROTOCOL_VERSION));
        for(unsigned int i=0; i<serializedProofs.size(); i++) {
            serializedProofs[i] << proofs[i];
            serializedProofs2[i] << proofs2[i];
            serializedProofs3[i] << proofs3[i];
            serializedProofs4[i] << proofs4[i];
        }

        std::cout << "- Unserializing the Signatures of Knowledge..." << std::endl;

        // unserialize the CDataStream object into a fresh SoK object
        std::vector<SerialNumberSoKProof> newproofs(serializedProofs.size(), SerialNumberSoKProof(ZCParams));
        std::vector<SerialNumberSoKProof> newproofs2(serializedProofs.size(), SerialNumberSoKProof(ZCParams));
        std::vector<SerialNumberSoKProof> newproofs3(serializedProofs.size(), SerialNumberSoKProof(ZCParams));
        std::vector<SerialNumberSoKProof> newproofs4(serializedProofs.size(), SerialNumberSoKProof(ZCParams));
        for(unsigned int i=0; i<serializedProofs.size(); i++) {
            serializedProofs[i] >> newproofs[i];
            serializedProofs2[i] >> newproofs2[i];
            serializedProofs3[i] >> newproofs3[i];
            serializedProofs4[i] >> newproofs4[i];
        }

        start_time = clock();
        finalResult = finalResult & Test_batchVerify(newproofs);
        printTime(start_time, newproofs.size());
        start_time = clock();
        finalResult = finalResult & Test_batchVerify(newproofs2, true);
        printTime(start_time, newproofs2.size());
        start_time = clock();
        finalResult = finalResult & Test_batchVerify(newproofs3, true);
        printTime(start_time, newproofs3.size());
        start_time = clock();
        finalResult = finalResult & Test_batchVerify(newproofs4, true);
        printTime(start_time, newproofs4.size());
    }
    std::cout << std::endl;
    return finalResult;
}


BOOST_AUTO_TEST_SUITE(zerocoin_zkp_tests)

BOOST_AUTO_TEST_CASE(bulletproofs_tests)
{
    std::cout << std::endl;
    RandomInit();
    ECC_Start();
    BOOST_CHECK(parameters_tests());
    BOOST_CHECK(arithmetic_circuit_tests());
    BOOST_CHECK(polynomial_commitment_tests());
    BOOST_CHECK(batch_signature_of_knowledge_tests(20, 20, 1));
    std::cout << std::endl << zSuccessfulTests << " out of " << zNumTests << " tests passed." << std::endl << std::endl;
}
BOOST_AUTO_TEST_SUITE_END()