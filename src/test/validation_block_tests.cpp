// Copyright (c) 2019 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <boost/test/unit_test.hpp>

#include <arith_uint256.h>
#include <chainparams.h>
#include <consensus/merkle.h>
#include <consensus/validation.h>
#include <miner.h>
#include <pow.h>
#include <random.h>
#include <test/test_veil.h>
#include <validation.h>
#include <validationinterface.h>
#include <veil/budget.h>
#include <script/standard.h>
#include <key_io.h>

struct RegtestingSetup : public TestingSetup {
    RegtestingSetup() : TestingSetup(CBaseChainParams::REGTEST)
    {
        // Upstream regtest disables retargeting and this test's pre-built
        // block tree assumes constant difficulty, but Veil regtest leaves
        // DGW retargeting enabled. DarkGravityWave's result depends on the
        // ancestor timestamps at each position (window fencepost and the
        // overdue adjustment), so blocks assembled against the genesis tip
        // cannot carry an nBits that matches every position of a 100-deep
        // random tree. Mirror upstream regtest semantics for this suite;
        // this test exercises ProcessNewBlock ordering, not retargeting.
        const_cast<Consensus::Params&>(Params().GetConsensus()).fPowNoRetargeting = true;
    }
    ~RegtestingSetup()
    {
        const_cast<Consensus::Params&>(Params().GetConsensus()).fPowNoRetargeting = false;
    }
};

// Veil: the block builders below diverge from upstream to satisfy Veil's
// consensus rules, which this test's random block tree otherwise trips over:
//  - Veil enforces the serialized height in the coinbase scriptSig and the
//    per-height reward schedule on every block (there is no BIP34 gate), and
//    the non-mainnet block-1 coinbase carries the 15M premine. A template
//    assembled at the genesis tip is therefore only valid at height 1, so
//    Block() rebuilds the coinbase for the block's actual tree position.
//  - Regtest activates the new PoW algos 10 seconds after genesis, so the
//    upstream genesis-time ladder would straddle the legacy/new header-format
//    boundary. All blocks are built as post-update sha256d blocks: contextual
//    checks require exactly one algo bit on post-update PoW blocks, and
//    sha256d is the only algo whose header does not embed nHeight (tree
//    positions would invalidate it) and whose validation involves no
//    RandomX VM or ProgPow DAG machinery. Timestamps step by the sha256d
//    target spacing so median-time-past stays monotonic along every branch.
BOOST_FIXTURE_TEST_SUITE(validation_block_tests, RegtestingSetup)

struct TestSubscriber : public CValidationInterface {
    uint256 m_expected_tip;

    TestSubscriber(uint256 tip) : m_expected_tip(tip) {}

    void UpdatedBlockTip(const CBlockIndex* pindexNew, const CBlockIndex* pindexFork, bool fInitialDownload) override
    {
        BOOST_CHECK_EQUAL(m_expected_tip, pindexNew->GetBlockHash());
    }

    void BlockConnected(const std::shared_ptr<const CBlock>& block, const CBlockIndex* pindex, const std::vector<CTransactionRef>& txnConflicted) override
    {
        BOOST_CHECK_EQUAL(m_expected_tip, block->hashPrevBlock);
        BOOST_CHECK_EQUAL(m_expected_tip, pindex->pprev->GetBlockHash());

        m_expected_tip = block->GetHash();
    }

    void BlockDisconnected(const std::shared_ptr<const CBlock>& block) override
    {
        BOOST_CHECK_EQUAL(m_expected_tip, block->GetHash());

        m_expected_tip = block->hashPrevBlock;
    }
};

std::shared_ptr<CBlock> Block(const uint256& prev_hash, int height)
{
    static int i = 0;
    static uint64_t time = Params().PowUpdateTimestamp();

    CScript pubKey;
    pubKey << i++ << OP_TRUE;

    auto ptemplate = BlockAssembler(Params()).CreateNewBlock(pubKey, true, false, false, CBlockHeader::SHA256D_BLOCK);
    auto pblock = std::make_shared<CBlock>(ptemplate->block);
    pblock->hashPrevBlock = prev_hash;
    time += Params().GetConsensus().nSha256DTargetSpacing;
    pblock->nTime = time;

    // The template was assembled at the genesis tip; rebuild the coinbase for
    // the block's actual position in the tree.
    CMutableTransaction txCoinbase(*pblock->vtx[0]);
    txCoinbase.vin[0].scriptSig = CScript() << height << OP_0;
    CAmount nBlockReward, nFounderPayment, nFoundationPayment, nBudgetPayment;
    veil::Budget().GetBlockRewards(height, nBlockReward, nFounderPayment, nFoundationPayment, nBudgetPayment);
    txCoinbase.vpout[0]->SetValue(nBlockReward);
    pblock->vtx[0] = MakeTransactionRef(std::move(txCoinbase));

    return pblock;
}

std::shared_ptr<CBlock> FinalizeBlock(std::shared_ptr<CBlock> pblock)
{
    pblock->hashMerkleRoot = BlockMerkleRoot(*pblock);
    pblock->hashWitnessMerkleRoot = BlockWitnessMerkleRoot(*pblock);

    // Regtest skips PoW verification in CheckBlockHeader, but grind the
    // sha256d nonce anyway so the headers are honestly mined against nBits.
    arith_uint256 bnTarget;
    bnTarget.SetCompact(pblock->nBits);
    while (UintToArith256(pblock->GetSha256DPoWHash()) > bnTarget) {
        ++(pblock->nNonce64);
    }

    return pblock;
}

// construct a valid block
const std::shared_ptr<const CBlock> GoodBlock(const uint256& prev_hash, int height)
{
    return FinalizeBlock(Block(prev_hash, height));
}

// construct an invalid block (but with a valid header)
const std::shared_ptr<const CBlock> BadBlock(const uint256& prev_hash, int height)
{
    auto pblock = Block(prev_hash, height);

    CMutableTransaction coinbase_spend;
    coinbase_spend.vin.push_back(CTxIn(COutPoint(pblock->vtx[0]->GetHash(), 0), CScript(), 0));
    coinbase_spend.vpout.push_back(pblock->vtx[0]->vpout[0]);

    CTransactionRef tx = MakeTransactionRef(coinbase_spend);
    pblock->vtx.push_back(tx);

    auto ret = FinalizeBlock(pblock);
    return ret;
}

void BuildChain(const uint256& root, int height, int remaining, const unsigned int invalid_rate, const unsigned int branch_rate, const unsigned int max_size, std::vector<std::shared_ptr<const CBlock>>& blocks)
{
    if (remaining <= 0 || blocks.size() >= max_size) return;

    bool gen_invalid = InsecureRandRange(100) < invalid_rate;
    bool gen_fork = InsecureRandRange(100) < branch_rate;

    const std::shared_ptr<const CBlock> pblock = gen_invalid ? BadBlock(root, height) : GoodBlock(root, height);
    blocks.push_back(pblock);
    if (!gen_invalid) {
        BuildChain(pblock->GetHash(), height + 1, remaining - 1, invalid_rate, branch_rate, max_size, blocks);
    }

    if (gen_fork) {
        blocks.push_back(GoodBlock(root, height));
        BuildChain(blocks.back()->GetHash(), height + 1, remaining - 1, invalid_rate, branch_rate, max_size, blocks);
    }
}

BOOST_AUTO_TEST_CASE(processnewblock_signals_ordering)
{
    // build a large-ish chain that's likely to have some forks
    std::vector<std::shared_ptr<const CBlock>> blocks;
    while (blocks.size() < 50) {
        blocks.clear();
        BuildChain(Params().GenesisBlock().GetHash(), 1, 100, 15, 10, 500, blocks);
    }

    bool ignored;
    CValidationState state;
    std::vector<CBlockHeader> headers;
    std::transform(blocks.begin(), blocks.end(), std::back_inserter(headers), [](std::shared_ptr<const CBlock> b) { return b->GetBlockHeader(); });

    // Process all the headers so we understand the toplogy of the chain
    BOOST_CHECK(ProcessNewBlockHeaders(headers, state, Params()));

    // Connect the genesis block and drain any outstanding events
    ProcessNewBlock(Params(), std::make_shared<CBlock>(Params().GenesisBlock()), true, &ignored);
    SyncWithValidationInterfaceQueue();

    // subscribe to events (this subscriber will validate event ordering)
    const CBlockIndex* initial_tip = nullptr;
    {
        LOCK(cs_main);
        initial_tip = chainActive.Tip();
    }
    TestSubscriber sub(initial_tip->GetBlockHash());
    RegisterValidationInterface(&sub);

    // create a bunch of threads that repeatedly process a block generated above at random
    // this will create parallelism and randomness inside validation - the ValidationInterface
    // will subscribe to events generated during block validation and assert on ordering invariance
    boost::thread_group threads;
    for (int i = 0; i < 10; i++) {
        threads.create_thread([&blocks]() {
            bool ignored;
            for (int i = 0; i < 1000; i++) {
                auto block = blocks[InsecureRandRange(blocks.size() - 1)];
                ProcessNewBlock(Params(), block, true, &ignored);
            }

            // to make sure that eventually we process the full chain - do it here
            for (auto block : blocks) {
                if (block->vtx.size() == 1) {
                    bool processed = ProcessNewBlock(Params(), block, true, &ignored);
                    assert(processed);
                }
            }
        });
    }

    threads.join_all();
    while (GetMainSignals().CallbacksPending() > 0) {
        UninterruptibleSleep(std::chrono::milliseconds{100});
    }

    UnregisterValidationInterface(&sub);

    BOOST_CHECK_EQUAL(sub.m_expected_tip, chainActive.Tip()->GetBlockHash());
}

BOOST_AUTO_TEST_SUITE_END()
