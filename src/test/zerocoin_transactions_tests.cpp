// Copyright (c) 2017-2018 The PIVX developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "test_veil.h"
#include "libzerocoin/Denominations.h"
#include "amount.h"
#include "chainparams.h"
#include "consensus/params.h"
#include "wallet/coincontrol.h"
//nclude "libzerocoin/ZerocoinDefines.h"
//#include "main.h"
#include "wallet/wallet.h"
#include "wallet/walletdb.h"
#include "txdb.h"
#include <boost/test/unit_test.hpp>
#include <iostream>
#include "libzerocoin/ParamGeneration.h"
#include "libzerocoin/Denominations.h"
#include "libzerocoin/Coin.h"

using namespace libzerocoin;
/*
CBigNum GetTestModulus()
{
    static CBigNum testModulus(0);

    // TODO: should use a hard-coded RSA modulus for testing
    if (!testModulus) {
        CBigNum p, q;

        // Note: we are NOT using safe primes for testing because
        // they take too long to generate. Don't do this in real
        // usage. See the paramgen utility for better code.
        p = CBigNum::generatePrime(1024, false);
        q = CBigNum::generatePrime(1024, false);
        testModulus = p * q;
    }

    return testModulus;
}*/
BOOST_FIXTURE_TEST_SUITE(zerocoin_transaction, BasicTestingSetup)

//static CWallet cWallet("unlocked.dat");

BOOST_AUTO_TEST_CASE(spend_nsequence)
{
    CTxIn in;
    in.nSequence = libzerocoin::CoinDenomination::ZQ_TEN;
    in.nSequence |= CTxIn::SEQUENCE_LOCKTIME_DISABLE_FLAG;

    CAmount nAmount = (in.nSequence & CTxIn::SEQUENCE_LOCKTIME_MASK);
    BOOST_CHECK_MESSAGE(nAmount == 10, "nSequence did not properly decode for 10");

    in.nSequence = libzerocoin::CoinDenomination::ZQ_ONE_HUNDRED;
    in.nSequence |= CTxIn::SEQUENCE_LOCKTIME_DISABLE_FLAG;
    nAmount = (in.nSequence & CTxIn::SEQUENCE_LOCKTIME_MASK);
    BOOST_CHECK_MESSAGE(nAmount == 100, "nSequence did not properly decode for 100");

    in.nSequence = libzerocoin::CoinDenomination::ZQ_ONE_THOUSAND;
    in.nSequence |= CTxIn::SEQUENCE_LOCKTIME_DISABLE_FLAG;
    nAmount = (in.nSequence & CTxIn::SEQUENCE_LOCKTIME_MASK);
    BOOST_CHECK_MESSAGE(nAmount == 1000, "nSequence did not properly decode for 1000");

    in.nSequence = libzerocoin::CoinDenomination::ZQ_TEN_THOUSAND;
    in.nSequence |= CTxIn::SEQUENCE_LOCKTIME_DISABLE_FLAG;
    nAmount = (in.nSequence & CTxIn::SEQUENCE_LOCKTIME_MASK);
    BOOST_CHECK_MESSAGE(nAmount == 10000, "nSequence did not properly decode for 10000");
}

BOOST_AUTO_TEST_CASE(zerocoin_spend_test)
{

/*
    SelectParams(CBaseChainParams::MAIN);
    ZerocoinParams *ZCParams = new ZerocoinParams(GetTestModulus());
    (void)ZCParams;

    //cWallet.zpivTracker = unique_ptr<CzPIVTracker>(new CzPIVTracker(cWallet.strWalletFile));
    CMutableTransaction tx;
    CWalletTx* wtx = new CWalletTx(&cWallet, tx);
    bool fMintChange=true;
    bool fMinimizeChange=true;
    std::vector<CZerocoinSpend> vSpends;
    std::vector<CZerocoinMint> vMints;
    CAmount nAmount = COIN;
    int nSecurityLevel = 100;

    CZerocoinSpendReceipt receipt;
    cWallet.SpendZerocoin(nAmount, nSecurityLevel, *wtx, receipt, vMints, fMintChange, fMinimizeChange);

    BOOST_CHECK_MESSAGE(receipt.GetStatus() == ZPIV_TRX_FUNDS_PROBLEMS, "Failed Invalid Amount Check");

    nAmount = 1;
    CZerocoinSpendReceipt receipt2;
    cWallet.SpendZerocoin(nAmount, nSecurityLevel, *wtx, receipt2, vMints, fMintChange, fMinimizeChange);

    // if using "wallet.dat", instead of "unlocked.dat" need this
    /// BOOST_CHECK_MESSAGE(vString == "Error: Wallet locked, unable to create transaction!"," Locked Wallet Check Failed");

    BOOST_CHECK_MESSAGE(receipt2.GetStatus() == ZPIV_TRX_FUNDS_PROBLEMS, "Failed Invalid Amount Check");
*/
}

BOOST_AUTO_TEST_SUITE_END()
