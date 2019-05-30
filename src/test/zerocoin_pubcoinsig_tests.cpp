// Copyright (c) 2019 The Veil developers
// Copyright (c) 2019 The PIVX developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#include <test/test_veil.h>
#include "libzerocoin/Denominations.h"
#include "libzerocoin/PubcoinSignature.h"
#include "libzerocoin/bignum.h"
#include "streams.h"

#include <boost/test/unit_test.hpp>
#include <iostream>

using namespace libzerocoin;

BOOST_AUTO_TEST_SUITE(zerocoin_pubcoinsig_tests)

std::string zerocoinModulus = "25195908475657893494027183240048398571429282126204032027777137836043662020707595556264018525880784"
                              "4069182906412495150821892985591491761845028084891200728449926873928072877767359714183472702618963750149718246911"
                              "6507761337985909570009733045974880842840179742910064245869181719511874612151517265463228221686998754918242243363"
                              "7259085141865462043576798423387184774447920739934236584823824281198163815010674810451660377306056201619676256133"
                              "8441436038339044149526344321901146575444541784240209246165157233507787077498171257724679629263863563732899121548"
                              "31438167899885040445364023527381951378636564391212010397122822120720357";

BOOST_AUTO_TEST_CASE(checkpubcoinsig_test)
{
    RandomInit();
    ECC_Start();

    CBigNum bnTrustedModulus = 0;
    if (!bnTrustedModulus)
        bnTrustedModulus.SetDec(zerocoinModulus);
    libzerocoin::ZerocoinParams zerocoinParams = libzerocoin::ZerocoinParams(bnTrustedModulus);

    libzerocoin::PrivateCoin coin(&zerocoinParams, CoinDenomination::ZQ_TEN, true);
    PublicCoin pubCoin = coin.getPublicCoin();
    auto bnPubcoin = pubCoin.getValue();

    //! Expect Pass: Standard scenario
    Commitment C1(&zerocoinParams.serialNumberSoKCommitmentGroup, bnPubcoin);
    libzerocoin::PubcoinSignature pubcoinSig_valid(&zerocoinParams, bnPubcoin, C1);
    std::string strError;
    bool fVerified = pubcoinSig_valid.Verify(C1.getCommitmentValue(), strError);
    strError = std::string("Pubcoin Signature Failed to Verify: ") + strError;
    BOOST_CHECK_MESSAGE(fVerified, strError);

    //! Expect Fail: pubcoin + serialparams.grouporder (same commitment value, but to different pubcoin value)
    auto pubcoinSig = PubcoinSignature(&zerocoinParams, bnPubcoin+zerocoinParams.serialNumberSoKCommitmentGroup.groupOrder, C1);
    auto C1_mod = Commitment(&zerocoinParams.serialNumberSoKCommitmentGroup, bnPubcoin+zerocoinParams.serialNumberSoKCommitmentGroup.groupOrder, C1.getRandomness());
    BOOST_CHECK(C1.getCommitmentValue() == C1_mod.getCommitmentValue());
    fVerified = pubcoinSig.Verify(C1.getCommitmentValue(), strError);
    strError = std::string("Pubcoin Signature Failed to Verify: ") + strError;
    BOOST_CHECK_MESSAGE(!fVerified, "Pubcoin sig passed testing even though it did not have the pubcoin that was committed to");

    //! Expect Fail: empty pubcoin
    CDataStream ss(0, 0);
    ss << uint8_t(1) << CBigNum(0) << C1.getRandomness();
    ss >> pubcoinSig;
    fVerified = pubcoinSig.Verify(C1.getCommitmentValue(), strError);
    BOOST_CHECK_MESSAGE(!fVerified , "Pubcoin sig passed testing even though it has 0 pubcoin value");

    //! Expect Fail: empty C1.randomness
    ss.clear();
    ss << uint8_t(1) << bnPubcoin << CBigNum(0);
    ss >> pubcoinSig;
    fVerified = pubcoinSig.Verify(C1.getCommitmentValue(), strError);
    BOOST_CHECK_MESSAGE(!fVerified , "Pubcoin sig passed testing even though it has 0 c1.r value");

    //! Expect Fail: commitment to different pubcoin
    libzerocoin::PrivateCoin coin2(&zerocoinParams, CoinDenomination::ZQ_TEN, true);
    Commitment C1_alt(&zerocoinParams.serialNumberSoKCommitmentGroup, coin2.getPublicCoin().getValue());
    pubcoinSig = PubcoinSignature(&zerocoinParams, bnPubcoin, C1_alt);
    fVerified = pubcoinSig.Verify(C1_alt.getCommitmentValue(), strError);
    BOOST_CHECK_MESSAGE(!fVerified , "Pubcoin sig passed testing even though the sig is made to a commitment to a different pubcoin");
}

BOOST_AUTO_TEST_SUITE_END()