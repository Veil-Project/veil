// Copyright (c) 2026 The Veil developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// Regression tests for the PoS block-template placeholder crash: while a
// proof-of-stake template is being assembled, vtx[1] holds a null
// CTransactionRef (see BlockAssembler::CreateNewBlock), and
// CBlock::IsProofOfStake() is reached transitively through PowType() /
// IsProofOfWork() before the real coinstake is assigned. Without the null
// check these dereference a null shared_ptr and crash the staking thread.

#include <test/test_veil.h>

#include <primitives/block.h>
#include <primitives/transaction.h>

#include <boost/test/unit_test.hpp>

BOOST_FIXTURE_TEST_SUITE(pos_placeholder_tests, BasicTestingSetup)

BOOST_AUTO_TEST_CASE(null_coinstake_placeholder_is_not_pos)
{
    CBlock block;

    // Empty block and single-entry block: the size check alone protects.
    BOOST_CHECK(!block.IsProofOfStake());
    block.vtx.emplace_back(); // null coinbase slot
    BOOST_CHECK(!block.IsProofOfStake());

    // The template-assembly state: two entries, vtx[1] a null placeholder.
    // Before the fix this dereferenced null instead of returning false.
    block.vtx.emplace_back();
    BOOST_REQUIRE_EQUAL(block.vtx.size(), 2U);
    BOOST_REQUIRE(!block.vtx[1]);
    BOOST_CHECK(!block.IsProofOfStake());
    BOOST_CHECK(block.IsProofOfWork());

    // The call chain that actually crashed in CreateNewBlock: PowType()
    // reaches IsProofOfStake() through the Is*() classifiers.
    (void)block.PowType();
    BOOST_CHECK(!block.IsProgPow());
    BOOST_CHECK(!block.IsRandomX());
    BOOST_CHECK(!block.IsSha256D());

    // A real (non-coinstake) transaction in slot 1 is still not a coinstake.
    CMutableTransaction mtx;
    mtx.nVersion = 1;
    mtx.vin.resize(1);
    block.vtx[1] = MakeTransactionRef(std::move(mtx));
    BOOST_CHECK(!block.IsProofOfStake());
}

BOOST_AUTO_TEST_SUITE_END()
