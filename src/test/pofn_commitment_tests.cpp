// Copyright (c) 2026 The Veil developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test/test_veil.h>

#include <chain.h>
#include <chainparams.h>
#include <consensus/merkle.h>
#include <consensus/validation.h>
#include <primitives/block.h>
#include <primitives/transaction.h>
#include <script/script.h>
#include <streams.h>
#include <validation.h>
#include <veil/proofoffullnode/proofoffullnode.h>
#include <version.h>

#include <boost/test/unit_test.hpp>

// Proof of full node rides on two fields the block hash does not commit to after the PoW update: the
// fProofOfFullNode header byte and the hashPoFN body field. These tests pin down that a relayed copy
// with either of them planted, stripped or garbled is refused before storage as a possible corruption,
// so the honest block behind the same hash is never marked failed, and that a copy of a block the node
// already stores can never be the copy that gets connected.

static CTransactionRef MakeCoinbase()
{
    CMutableTransaction tx;
    tx.vin.resize(1);
    tx.vin[0].prevout.SetNull();
    tx.vin[0].scriptSig = CScript() << 1 << OP_0;
    tx.vpout.push_back(MAKE_OUTPUT<CTxOutStandard>());
    return MakeTransactionRef(std::move(tx));
}

// The shape CTransaction::IsCoinStake looks for: one zerocoin spend input and an empty first output.
static CTransactionRef MakeCoinstakeShape()
{
    CMutableTransaction tx;
    tx.vin.resize(1);
    tx.vin[0].scriptSig = CScript() << OP_ZEROCOINSPEND;
    tx.vpout.push_back(MAKE_OUTPUT<CTxOutStandard>());
    tx.vpout.push_back(MAKE_OUTPUT<CTxOutStandard>(1 * COIN, CScript() << OP_TRUE));
    return MakeTransactionRef(std::move(tx));
}

static CBlock MakeStakeBlock()
{
    CBlock block;
    block.nVersion = NEW_POW_BLOCK_VERSION << BITS_TO_BLOCK_VERSION;
    block.nTime = nPowTimeStampActive + 60;
    block.nBits = 0x207fffff;
    block.fProofOfStake = 1;
    block.vtx.push_back(MakeCoinbase());
    block.vtx.push_back(MakeCoinstakeShape());
    block.hashMerkleRoot = BlockMerkleRoot(block);
    return block;
}

static int DoSScore(const CValidationState& state)
{
    int nDoS = -1;
    state.IsInvalid(nDoS);
    return nDoS;
}

BOOST_FIXTURE_TEST_SUITE(pofn_commitment_tests, BasicTestingSetup)

// Root cause: neither field moves the hash, and both travel on the wire with a stake block.
BOOST_AUTO_TEST_CASE(proof_fields_are_outside_the_hash)
{
    CBlock block = MakeStakeBlock();
    const uint256 hash = block.GetHash();
    block.fProofOfFullNode = 1;
    BOOST_CHECK(block.GetHash() == hash);
    block.hashPoFN = uint256S("ab");
    BOOST_CHECK(block.GetHash() == hash);

    CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
    ss << block;
    CBlock received;
    ss >> received;
    BOOST_CHECK_EQUAL((int)received.fProofOfFullNode, 1);
    BOOST_CHECK(received.hashPoFN == uint256S("ab"));
    BOOST_CHECK(received.GetHash() == hash);
}

// A stake block that claims nothing needs no proof and touches no chain state.
BOOST_AUTO_TEST_CASE(no_claim_needs_no_proof)
{
    CBlock block = MakeStakeBlock();
    CValidationState state;
    BOOST_CHECK(CheckProofOfFullNode(block, state, nullptr));
    BOOST_CHECK(state.IsValid());
}

// The byte planted on a work block is refused, and not held against the hash.
BOOST_AUTO_TEST_CASE(proof_on_a_work_block_is_a_possible_corruption)
{
    CBlock block = MakeStakeBlock();
    block.vtx.pop_back();
    block.fProofOfStake = 0;
    block.fProofOfFullNode = 1;
    BOOST_CHECK(!block.IsProofOfStake());

    CValidationState state;
    BOOST_CHECK(!CheckProofOfFullNode(block, state, nullptr));
    BOOST_CHECK_EQUAL(state.GetRejectReason(), "bad-fullnode-type");
    BOOST_CHECK(state.CorruptionPossible());
}

// A claim with no proof behind it is refused before any recompute, so it can never pass by matching
// the empty result of a recompute that could not run.
BOOST_AUTO_TEST_CASE(a_claim_without_a_proof_is_refused_before_any_recompute)
{
    CBlock block = MakeStakeBlock();
    block.fProofOfFullNode = 1;
    BOOST_CHECK(block.hashPoFN.IsNull());

    CValidationState state;
    BOOST_CHECK(!CheckProofOfFullNode(block, state, nullptr));
    BOOST_CHECK_EQUAL(state.GetRejectReason(), "bad-fullnode-hash");
    BOOST_CHECK(state.CorruptionPossible());
}

// The same byte at the header stage: the work versus full node conflict is a possible corruption too.
BOOST_AUTO_TEST_CASE(header_conflict_is_a_possible_corruption)
{
    CBlock block = MakeStakeBlock();
    block.vtx.pop_back();
    block.fProofOfStake = 0;
    block.fProofOfFullNode = 1;

    CValidationState state;
    BOOST_CHECK(!CheckBlock(block, state, Params().GetConsensus(), /*fSkipComputation=*/true, /*fCheckPOW=*/true, /*fCheckMerkleRoot=*/false));
    BOOST_CHECK_EQUAL(state.GetRejectReason(), "PoW and PoFN conflict");
    BOOST_CHECK(state.CorruptionPossible());
}

BOOST_AUTO_TEST_SUITE_END()

struct PofnRegtestSetup : public TestingSetup {
    PofnRegtestSetup() : TestingSetup(CBaseChainParams::REGTEST) {}
};

BOOST_FIXTURE_TEST_SUITE(pofn_commitment_genesis_tests, PofnRegtestSetup)

// A proof planted on the block after genesis: there are no ancestors to prove, the generator says so
// instead of computing an undefined modulo, and the block is refused as unverifiable with no score.
BOOST_AUTO_TEST_CASE(planted_proof_on_top_of_genesis_is_unverifiable)
{
    LOCK(cs_main);
    const CBlockIndex* pindexGenesis = chainActive.Genesis();
    BOOST_REQUIRE(pindexGenesis);

    CBlock block = MakeStakeBlock();
    block.hashPrevBlock = pindexGenesis->GetBlockHash();
    block.hashPoFN = uint256S("ab");
    BOOST_CHECK(veil::GetFullNodeHash(block, pindexGenesis) == uint256());

    CValidationState state;
    BOOST_CHECK(!CheckProofOfFullNode(block, state, pindexGenesis));
    BOOST_CHECK_EQUAL(state.GetRejectReason(), "bad-fullnode-unverifiable");
    BOOST_CHECK(state.CorruptionPossible());
    BOOST_CHECK_EQUAL(DoSScore(state), 0);
}

// A parent indexed from its header alone, the shape a reorg fetch can leave behind, has no ancestor
// data to sample. The block is refused as unverifiable, without blaming the peer that sent it.
BOOST_AUTO_TEST_CASE(a_parent_without_data_makes_the_proof_unverifiable_without_blame)
{
    const CBlockIndex* pindexGenesis = chainActive.Genesis();
    BOOST_REQUIRE(pindexGenesis);

    CBlockHeader parent;
    parent.nVersion = NEW_POW_BLOCK_VERSION << BITS_TO_BLOCK_VERSION;
    parent.hashPrevBlock = pindexGenesis->GetBlockHash();
    parent.nTime = std::max<uint32_t>(nPowTimeStampActive, pindexGenesis->nTime) + 60;
    parent.nBits = 0x207fffff;
    parent.fProofOfStake = 1;

    CValidationState stateHeader;
    const CBlockIndex* pindexParent = nullptr;
    BOOST_REQUIRE_MESSAGE(ProcessNewBlockHeaders({parent}, stateHeader, Params(), &pindexParent), FormatStateMessage(stateHeader));
    BOOST_REQUIRE(pindexParent);
    BOOST_REQUIRE_EQUAL(pindexParent->nChainTx, 0U);

    CBlock block = MakeStakeBlock();
    block.hashPrevBlock = pindexParent->GetBlockHash();
    block.nTime = parent.nTime + 60;
    block.hashPoFN = uint256S("ab");

    LOCK(cs_main);
    CValidationState state;
    BOOST_CHECK(!CheckProofOfFullNode(block, state, pindexParent));
    BOOST_CHECK_EQUAL(state.GetRejectReason(), "bad-fullnode-unverifiable");
    BOOST_CHECK(state.CorruptionPossible());
    BOOST_CHECK_EQUAL(DoSScore(state), 0);
}

BOOST_AUTO_TEST_SUITE_END()

BOOST_FIXTURE_TEST_SUITE(pofn_commitment_chain_tests, TestChain100Setup)

// On a real chain the honest proof recomputes and passes, one garbled byte is refused as a possible
// corruption, and stripping the proof from the same block leaves a block with no claim, which passes
// here and simply earns no full node fee share later.
BOOST_AUTO_TEST_CASE(garbled_proof_on_a_real_chain_is_a_possible_corruption)
{
    LOCK(cs_main);
    // Any mined ancestor is enough for a genuine recompute, the exact height does not matter.
    const CBlockIndex* pindexTip = chainActive.Tip();
    BOOST_REQUIRE(pindexTip);
    BOOST_REQUIRE(pindexTip->nHeight >= 1);

    CBlock block = MakeStakeBlock();
    block.hashPrevBlock = pindexTip->GetBlockHash();
    block.fProofOfFullNode = 1;
    block.hashPoFN = veil::GetFullNodeHash(block, pindexTip);
    BOOST_REQUIRE(block.hashPoFN != uint256());

    CValidationState state;
    BOOST_CHECK(CheckProofOfFullNode(block, state, pindexTip));
    BOOST_CHECK(state.IsValid());

    CBlock garbled = block;
    *garbled.hashPoFN.begin() ^= 0x01;
    CValidationState stateGarbled;
    BOOST_CHECK(!CheckProofOfFullNode(garbled, stateGarbled, pindexTip));
    BOOST_CHECK_EQUAL(stateGarbled.GetRejectReason(), "bad-fullnode-hash");
    BOOST_CHECK(stateGarbled.CorruptionPossible());
    BOOST_CHECK_EQUAL(DoSScore(stateGarbled), 100);
    BOOST_CHECK(garbled.GetHash() == block.GetHash());

    CBlock stripped = block;
    stripped.fProofOfFullNode = 0;
    stripped.hashPoFN = uint256();
    CValidationState stateStripped;
    BOOST_CHECK(CheckProofOfFullNode(stripped, stateStripped, pindexTip));
    BOOST_CHECK(stripped.GetHash() == block.GetHash());
}

// AcceptBlock returns early for a hash it already has data for, before any body check. A second copy
// of such a block must therefore never be the copy that gets connected: only the stored copy passed
// the checks. The block is put back into the "stored, not connected" state a node sits in between
// AcceptBlock and ActivateBestChain, then a copy with the unhashed byte planted is fed in.
BOOST_AUTO_TEST_CASE(a_second_copy_of_a_stored_block_is_never_the_one_connected)
{
    const CBlock honest = CreateAndProcessBlock({}, CScript() << OP_TRUE);
    CBlockIndex* pindex = nullptr;
    {
        LOCK(cs_main);
        pindex = LookupBlockIndex(honest.GetHash());
        BOOST_REQUIRE(pindex);
        BOOST_REQUIRE(chainActive.Tip() == pindex);

        CValidationState state;
        BOOST_REQUIRE(InvalidateBlock(state, Params(), pindex));
        BOOST_REQUIRE(chainActive.Tip() != pindex);
        ResetBlockFailureFlags(pindex);
        BOOST_REQUIRE(!(pindex->nStatus & BLOCK_FAILED_MASK));
        BOOST_REQUIRE(pindex->nStatus & BLOCK_HAVE_DATA);
    }

    auto garbled = std::make_shared<CBlock>(honest);
    garbled->fProofOfFullNode = 1;
    BOOST_CHECK(garbled->GetHash() == honest.GetHash());

    bool fNewBlock = true;
    ProcessNewBlock(Params(), garbled, /*fForceProcessing=*/true, &fNewBlock);
    BOOST_CHECK(!fNewBlock);

    LOCK(cs_main);
    BOOST_CHECK(!(pindex->nStatus & BLOCK_FAILED_MASK));
    BOOST_CHECK(chainActive.Tip() == pindex);
}

BOOST_AUTO_TEST_SUITE_END()
