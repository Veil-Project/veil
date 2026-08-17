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
#include <veil/ringct/anon.h>

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

// Build a bare RingCT anon input with attacker-chosen ring dimensions. GetAnonInfo() reads
// nInputs from the first four bytes of prevout.hash and nRingSize from the next four.
static CTxIn MakeAnonRingTxIn(uint32_t nInputs, uint32_t nRingSize)
{
    uint256 hashPrevout; // zero initialised
    memcpy(hashPrevout.begin(), &nInputs, sizeof(nInputs));
    memcpy(hashPrevout.begin() + 4, &nRingSize, sizeof(nRingSize));

    CTxIn in;
    in.prevout = COutPoint(hashPrevout, COutPoint::ANON_MARKER);
    return in;
}

// GetRingCtInputs sizes a vM(nCols*nRows*33) scratch buffer directly from nRingSize/nInputs.
// With ring dimensions taken unbounded from prevout.hash an attacker can request a multi-terabyte
// allocation (nRingSize 0xFFFFFFFF, nInputs 32 -> ~4.3 TB), so the dimensions must be range checked
// and the call must return empty before that sizing. We assert the reject path for values above the
// permitted maxima and below the permitted minima; we never drive the unbounded allocation itself.
BOOST_AUTO_TEST_CASE(getringctinputs_vector_rejects_out_of_range_dimensions)
{
    BOOST_CHECK(GetRingCtInputs(MakeAnonRingTxIn(1, 0xFFFFFFFF)).empty());          // ring > MAX_RINGSIZE
    BOOST_CHECK(GetRingCtInputs(MakeAnonRingTxIn(1, MIN_RINGSIZE - 1)).empty());    // ring < MIN_RINGSIZE
    BOOST_CHECK(GetRingCtInputs(MakeAnonRingTxIn(MAX_ANON_INPUTS + 1, MIN_RINGSIZE)).empty()); // inputs > max
    BOOST_CHECK(GetRingCtInputs(MakeAnonRingTxIn(0, MIN_RINGSIZE)).empty());        // inputs == 0
}

BOOST_AUTO_TEST_CASE(getringctinputs_bool_rejects_out_of_range_dimensions)
{
    std::vector<std::vector<COutPoint>> vInputs;
    BOOST_CHECK(!GetRingCtInputs(MakeAnonRingTxIn(1, 0xFFFFFFFF), vInputs));
    BOOST_CHECK(vInputs.empty());
    BOOST_CHECK(!GetRingCtInputs(MakeAnonRingTxIn(MAX_ANON_INPUTS + 1, MIN_RINGSIZE), vInputs));
    BOOST_CHECK(vInputs.empty());
}

BOOST_AUTO_TEST_SUITE_END()
