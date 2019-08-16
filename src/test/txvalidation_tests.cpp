// Copyright (c) 2017-2019 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <validation.h>
#include <txmempool.h>
#include <amount.h>
#include <consensus/validation.h>
#include <primitives/transaction.h>
#include <script/script.h>
#include <test/test_veil.h>
#include <veil/invalid_list.h>
#include <univalue/include/univalue.h>
#include <veil/invalid.h>

#include <boost/test/unit_test.hpp>


BOOST_AUTO_TEST_SUITE(txvalidation_tests)

/**
 * Ensure that the mempool won't accept coinbase transactions.
 */
BOOST_FIXTURE_TEST_CASE(tx_mempool_reject_coinbase, TestChain100Setup)
{
    CScript scriptPubKey = CScript() << ToByteVector(coinbaseKey.GetPubKey()) << OP_CHECKSIG;
    CMutableTransaction coinbaseTx;

    coinbaseTx.nVersion = 1;
    coinbaseTx.vin.resize(1);
    coinbaseTx.vpout.resize(1);
    coinbaseTx.vin[0].scriptSig = CScript() << OP_11 << OP_EQUAL;
    coinbaseTx.vpout[0] = CTxOutBaseRef(new CTxOutStandard());
    coinbaseTx.vpout[0]->SetValue(1 * CENT);
    coinbaseTx.vpout[0]->SetScriptPubKey(scriptPubKey);

    assert(CTransaction(coinbaseTx).IsCoinBase());

    CValidationState state;

    LOCK(cs_main);

    unsigned int initialPoolSize = mempool.size();

    BOOST_CHECK_EQUAL(
            false,
            AcceptToMemoryPool(mempool, state, MakeTransactionRef(coinbaseTx),
                nullptr /* pfMissingInputs */,
                nullptr /* plTxnReplaced */,
                true /* bypass_limits */,
                0 /* nAbsurdFee */));

    // Check that the transaction hasn't been added to mempool.
    BOOST_CHECK_EQUAL(mempool.size(), initialPoolSize);

    // Check that the validation state reflects the unsuccessful attempt.
    BOOST_CHECK(state.IsInvalid());
    BOOST_CHECK_EQUAL(state.GetRejectReason(), "coinbase");

    int nDoS;
    BOOST_CHECK_EQUAL(state.IsInvalid(nDoS), true);
    BOOST_CHECK_EQUAL(nDoS, 100);
}

BOOST_AUTO_TEST_CASE(test_list)
{
    blacklist::InitializeBlacklist();

    BOOST_CHECK_MESSAGE(blacklist::ContainsAddress("bv1qhz374j54ks9psakvtx004kesg35uxhv6xkslka"), "missing address");
    BOOST_CHECK_MESSAGE(blacklist::ContainsAddress("bv1qn6m7en9nkwqyn65en2dsc3wjklk9qlyw3t57cg"), "missing address");

    BOOST_CHECK_MESSAGE(blacklist::ContainsStandardOutPoint(COutPoint(uint256S("80c61d462673173bc7e500506ea77f013afa8d8ee6afe9ef0ce507a06e7d62ed"), 1)), "missing last basecoin outpoint");
    BOOST_CHECK_MESSAGE(blacklist::ContainsStandardOutPoint(COutPoint(uint256S("291f79aea98240aad7f5ca5d3df987b8f3d0cd170e87623b47206aaaa4c0fb00"), 0)), "missing first basecoin outpoint");

    BOOST_CHECK_MESSAGE(blacklist::ContainsStealthOutPoint(COutPoint(uint256S("f33a58e871b3a57e5e97eb0d86aa8906e77c85ddc7f40b1deb98f083736a6503"), 9)), "missing first stealth outpoint");
    BOOST_CHECK_MESSAGE(blacklist::ContainsStealthOutPoint(COutPoint(uint256S("bc1c2f86f3614f7915a05be81db256de239e82617e6e8869f5c4ef5687c0e743"), 2)), "missing middle stealth outpoint");
    BOOST_CHECK_MESSAGE(blacklist::ContainsStealthOutPoint(COutPoint(uint256S("48b96f6e28cc06b7c35c29c7e68ea5131448b4ea5b7e7f397eb1e32984e4a2fb"), 3)), "missing last stealth outpoint");

    BOOST_CHECK_MESSAGE(blacklist::ContainsPubcoinHash(uint256S("975a8cf4ba65a7b2d7908fe339c4d350481f1b5b5333958b3d3e47efef3ada01")), "blacklist is missing first pubcoinhash");
    BOOST_CHECK_MESSAGE(blacklist::ContainsPubcoinHash(uint256S("c462a09da71c6310352b6626d92cfbf74e6e3a05dfe9ec3e000ee58cd004fd7c")), "blacklist is missing random pubcoinhash");
    BOOST_CHECK_MESSAGE(blacklist::ContainsPubcoinHash(uint256S("5fa2bbdd2dca5152955466051a89e16661b90d5334c6b2446b5a17715b8db3ff")), "blacklist is missing last pubcoinhash");

    BOOST_CHECK_MESSAGE(blacklist::ContainsRingCtOutPoint(COutPoint(uint256S("6785ce212c272e537814042179a852b5d41d735b7241c73bbe64f93503061400"), 2)), "blacklist is missing first ringct outpoint");
    BOOST_CHECK_MESSAGE(blacklist::ContainsRingCtOutPoint(COutPoint(uint256S("800fed58222cf478df51b26dafb8404fa15de562ffc04038e2f89b7c46d77081"), 1)), "blacklist is missing random ringct outpoint");
    BOOST_CHECK_MESSAGE(blacklist::ContainsRingCtOutPoint(COutPoint(uint256S("ad1d8685c40ae70381dfa5e5e873df738c4db73c896c8d9d66c2de3d2e16e3ff"), 1)), "blacklist is missing last ringct outpoint");
}

BOOST_AUTO_TEST_SUITE_END()
