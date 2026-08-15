// Copyright (c) 2026 The Veil developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test/test_veil.h>

#include <amount.h>
#include <coins.h>
#include <consensus/tx_verify.h>
#include <consensus/validation.h>
#include <primitives/transaction.h>
#include <uint256.h>

#include <cstring>
#include <vector>

#include <boost/test/unit_test.hpp>

BOOST_FIXTURE_TEST_SUITE(ringct_anon_tests, BasicTestingSetup)

// Build a single-input transaction whose only input is a RingCT anon input with the
// given declared ring-input count and scriptData stack. This lets us drive the anon
// input structural guard in Consensus::CheckTxInputs with no valid crypto: an anon
// input is defined solely by prevout.n == ANON_MARKER, and GetAnonInfo() reads nInputs
// from the first four bytes of prevout.hash.
static CMutableTransaction MakeAnonInputTx(uint32_t nInputs, const std::vector<std::vector<uint8_t>>& stack)
{
    uint256 hashPrevout; // zero initialised
    memcpy(hashPrevout.begin(), &nInputs, sizeof(nInputs));

    CTxIn in;
    in.prevout = COutPoint(hashPrevout, COutPoint::ANON_MARKER);
    in.scriptData.stack = stack;

    CMutableTransaction mtx;
    mtx.vin.push_back(in);
    return mtx;
}

static bool CheckInputsRejects(const CMutableTransaction& mtx, std::string& reason)
{
    CCoinsView coinsDummy;
    CCoinsViewCache coins(&coinsDummy);

    CValidationState state;
    CAmount nFee = 0, nValueIn = 0, nValueOut = 0;
    bool ok = Consensus::CheckTxInputs(CTransaction(mtx), state, coins, /*nSpendHeight=*/1,
                                       nFee, nValueIn, nValueOut, /*test_accept=*/false);
    reason = state.GetRejectReason();
    return !ok;
}

// An anon input carrying an empty scriptData.stack must be rejected, not indexed.
// Regression for the unauthenticated out-of-bounds read (empty stack -> stack[0]).
BOOST_AUTO_TEST_CASE(anon_input_empty_stack_rejected)
{
    CMutableTransaction mtx = MakeAnonInputTx(/*nInputs=*/2, /*stack=*/{});
    std::string reason;
    BOOST_CHECK(CheckInputsRejects(mtx, reason));
    BOOST_CHECK_EQUAL(reason, "bad-anonin-scriptdata");
}

// An anon input whose key-image blob length does not equal nInputs*33 must be rejected.
BOOST_AUTO_TEST_CASE(anon_input_bad_keyimage_size_rejected)
{
    // nInputs = 2 requires 66 key-image bytes; provide 10.
    CMutableTransaction mtx = MakeAnonInputTx(2, {std::vector<uint8_t>(10, 0)});
    std::string reason;
    BOOST_CHECK(CheckInputsRejects(mtx, reason));
    BOOST_CHECK_EQUAL(reason, "bad-anonin-scriptdata");
}

// A huge attacker-declared nInputs must be rejected before any key-image indexing.
// Regression for the unbounded out-of-bounds read during block validation.
BOOST_AUTO_TEST_CASE(anon_input_huge_ninputs_rejected)
{
    CMutableTransaction mtx = MakeAnonInputTx(0xFFFFFFFF, {std::vector<uint8_t>(33, 0)});
    std::string reason;
    BOOST_CHECK(CheckInputsRejects(mtx, reason));
    BOOST_CHECK_EQUAL(reason, "bad-anonin-scriptdata");
}

BOOST_AUTO_TEST_SUITE_END()
