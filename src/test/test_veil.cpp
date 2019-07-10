// Copyright (c) 2011-2019 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test/test_veil.h>

#include <chainparams.h>
#include <consensus/consensus.h>
#include <consensus/validation.h>
#include <crypto/sha256.h>
#include <validation.h>
#include <miner.h>
#include <net_processing.h>
#include <pow.h>
#include <ui_interface.h>
#include <streams.h>
#include <rpc/server.h>
#include <rpc/register.h>
#include <script/sigcache.h>

void CConnmanTest::AddNode(CNode& node)
{
    LOCK(g_connman->cs_vNodes);
    g_connman->vNodes.push_back(&node);
}

void CConnmanTest::ClearNodes()
{
    LOCK(g_connman->cs_vNodes);
    for (CNode* node : g_connman->vNodes) {
        delete node;
    }
    g_connman->vNodes.clear();
}

uint256 insecure_rand_seed = GetRandHash();
FastRandomContext insecure_rand_ctx(insecure_rand_seed);

extern bool fPrintToConsole;
extern void noui_connect();

std::ostream& operator<<(std::ostream& os, const uint256& num)
{
    os << num.ToString();
    return os;
}

BasicTestingSetup::BasicTestingSetup(const std::string& chainName)
    : m_path_root(fs::temp_directory_path() / "test_veil" / strprintf("%lu_%i", (unsigned long)GetTime(), (int)(InsecureRandRange(1 << 30))))
{
    SHA256AutoDetect();
    RandomInit();
    ECC_Start();
    SetupEnvironment();
    SetupNetworking();
    InitSignatureCache();
    InitScriptExecutionCache();
    fCheckBlockIndex = true;
    SelectParams(chainName);
    noui_connect();
}

BasicTestingSetup::~BasicTestingSetup()
{
    fs::remove_all(m_path_root);
    ECC_Stop();
}

fs::path BasicTestingSetup::SetDataDir(const std::string& name)
{
    fs::path ret = m_path_root / name;
    fs::create_directories(ret);
    gArgs.ForceSetArg("-datadir", ret.string());
    return ret;
}

TestingSetup::TestingSetup(const std::string& chainName) : BasicTestingSetup(chainName)
{
    SetDataDir("tempdir");
    const CChainParams& chainparams = Params();
        // Ideally we'd move all the RPC tests to the functional testing framework
        // instead of unit tests, but for now we need these here.
        RegisterAllCoreRPCCommands(tableRPC);
        ClearDatadirCache();

        // We have to run a scheduler thread to prevent ActivateBestChain
        // from blocking due to queue overrun.
        threadGroup.create_thread(boost::bind(&CScheduler::serviceQueue, &scheduler));
        GetMainSignals().RegisterBackgroundSignalScheduler(scheduler);

        mempool.setSanityCheck(1.0);
        pblocktree.reset(new CBlockTreeDB(1 << 20, true));
        pcoinsdbview.reset(new CCoinsViewDB(1 << 23, true));
        pcoinsTip.reset(new CCoinsViewCache(pcoinsdbview.get()));
        pzerocoinDB.reset(new CZerocoinDB(1 << 20, true));
        if (!LoadGenesisBlock(chainparams)) {
            throw std::runtime_error("LoadGenesisBlock failed.");
        }
        {
            CValidationState state;
            if (!ActivateBestChain(state, chainparams)) {
                throw std::runtime_error(strprintf("ActivateBestChain failed. (%s)", FormatStateMessage(state)));
            }
        }
        nScriptCheckThreads = 3;
        for (int i=0; i < nScriptCheckThreads-1; i++)
            threadGroup.create_thread(&ThreadScriptCheck);
        g_connman = std::unique_ptr<CConnman>(new CConnman(0x1337, 0x1337)); // Deterministic randomness for tests.
        connman = g_connman.get();
        peerLogic.reset(new PeerLogicValidation(connman, scheduler, /*enable_bip61=*/true));
}

TestingSetup::~TestingSetup()
{
        threadGroup.interrupt_all();
        threadGroup.join_all();
        GetMainSignals().FlushBackgroundCallbacks();
        GetMainSignals().UnregisterBackgroundSignalScheduler();
        g_connman.reset();
        peerLogic.reset();
        UnloadBlockIndex();
        pcoinsTip.reset();
        pcoinsdbview.reset();
        pblocktree.reset();
}

TestChain100Setup::TestChain100Setup() : TestingSetup(CBaseChainParams::REGTEST)
{
    // CreateAndProcessBlock() does not support building SegWit blocks, so don't activate in these tests.
    // TODO: fix the code to support SegWit blocks.
    UpdateVersionBitsParameters(Consensus::DEPLOYMENT_SEGWIT, 0, Consensus::BIP9Deployment::NO_TIMEOUT);
    // Generate a 100-block chain:
    coinbaseKey.MakeNewKey(true);
    CScript scriptPubKey = CScript() <<  ToByteVector(coinbaseKey.GetPubKey()) << OP_CHECKSIG;
    for (int i = 0; i < Params().CoinbaseMaturity(); i++)
    {
        std::vector<CMutableTransaction> noTxns;
        CBlock b = CreateAndProcessBlock(noTxns, scriptPubKey);
        m_coinbase_txns.push_back(b.vtx[0]);
    }
}

//
// Create a new block with just given transactions, coinbase paying to
// scriptPubKey, and try to add it to the current chain.
//
CBlock
TestChain100Setup::CreateAndProcessBlock(const std::vector<CMutableTransaction>& txns, const CScript& scriptPubKey)
{
    const CChainParams& chainparams = Params();
    std::unique_ptr<CBlockTemplate> pblocktemplate = BlockAssembler(chainparams).CreateNewBlock(scriptPubKey);
    CBlock& block = pblocktemplate->block;

    // Replace mempool-selected txns with just coinbase plus passed-in txns:
    block.vtx.resize(1);
    for (const CMutableTransaction& tx : txns)
        block.vtx.push_back(MakeTransactionRef(tx));
    // IncrementExtraNonce creates a valid coinbase and merkleRoot
    {
        LOCK(cs_main);
        unsigned int extraNonce = 0;
        IncrementExtraNonce(&block, chainActive.Tip(), extraNonce);
    }

    while (!CheckProofOfWork(block.GetPoWHash(), block.nBits, chainparams.GetConsensus())) ++block.nNonce;

    std::shared_ptr<const CBlock> shared_pblock = std::make_shared<const CBlock>(block);
    ProcessNewBlock(chainparams, shared_pblock, true, nullptr);

    CBlock result = block;
    return result;
}

TestChain100Setup::~TestChain100Setup()
{
}


CTxMemPoolEntry TestMemPoolEntryHelper::FromTx(const CMutableTransaction &tx) {
    return FromTx(MakeTransactionRef(tx));
}

CTxMemPoolEntry TestMemPoolEntryHelper::FromTx(const CTransactionRef& tx)
{
    return CTxMemPoolEntry(tx, nFee, nTime, nHeight,
                           spendsCoinbase, sigOpCost, lp);
}

/**
 * @returns a real block (5175dceed294cd997426e69de1b05b52dee54b08680426aad5c447ed6a51267c)
 *      with 4 txs.
 */
CBlock getBlock13b8a()
{
    CBlock block;
    CDataStream stream(ParseHex("000000203c1e9b53f51268fe2495998013d98b5555df390c57094c3ee6bfd5a1779326a34cc2f499ec350781590935fe547b4cfb13435e2a7af98257961413872027e3489ccd2d5c505e151baccf6dfd00000401000000000000010000000000000000000000000000000000000000000000000000000000000000ffffffff5202fa03049ccd2d5c084fffcb30030000004031333132303036386263636136373835396336373231353734323133633566343261326164393431376338316334396434353565363535343831346532346138ffffffff010100f2052a010000001976a914a36855abd93399fcc23439e90dce46724a95e47488ac020001f903000002212dc44b6d2d8025d6d7478f027c4cf108374b70a9a7e279bc3f7dc02c1b6ce4000000006a473044022079452f09481109cdae8e4393501196a0add5bde6cfba9e6dfa4e5458815ad3e3022029caa8cf1f4ade26b7d41d75b8e85b58140a79abbc9b532f126172eabfd5daea0121037f39b9394898b513c636c8f9b9f40d56fca507a14134c3e38cd500b2b4d825c0feffffffb06036b22c9c170507be59de50ed00966a3c8816c4bcd1432509a0a3c08af8f40000000000feffffff0201d0712413000000001600141e637ffb9b35cac1e8e5e93718e6060a29c5459e01b193a13602000000160014bda85634f5553109fa4f0e1ffb749bd2f16370ae000247304402205784dbc4c516dcb13e9ce141c8d70bab3fab677d0b87b5139da44de41fe13017022074338abff6741b1c5db8b8216d31a9f1f55abab576f47af349e761e81f9f7e1a0121031973c9306961a7e44b3d74335b5126f577dbec6b0ebf4dc8d7b0cc1ab7cb584d020001f90300000157d3399445cbbe4579b5f7a7eae8ba3ddafcb586cca8d83da40107c0b7cda1100100000000feffffff0201d1ede00000000000160014e68182aa200bb61ce41130b0f0c61781ced2a7c8016f8c220000000000160014565bd813cb0982ed013d475af5dc8dffd77b1e1d0247304402206efa660daa099c9a4e34c21a867bbf367083b683c46a59f587ce19737e95914702204ba42f2e830731afdb9885235d7fe9a878a00834db0bc8023524aea30d7417be0121039715b88d8c2d8f584d44863f9ba32ad8257d67d8cf057491b2b91d1de1ecc961020001f903000001ad6d9c9f200c47b159fc27d122d66b6c88b4b792ef4bdd99c2beaa9834660c620000000000feffffff020131f42e3e000000001600149e75fcfd67340807dfe4799a23ab29a064939ddd010b11d01f000000001600148c44c596f4239d8146654cf8b32a5bec090723780247304402205a9c94f63d4718a857133471dd343ed4911927c84d7fe6fb700b0f6864badf69022057ea4a2e98b2c1650183be31de5039bed31b4640d853ae8e4b4ca6ac5a7c487f012102a5704ae27b12a1375e67e0fa2b462e668acb20a7049b3664465d883ed9c5742a040a000000000000006abac087e57075c2865cf0aec5e24141cebc0ded60118e52d2214fa1ce4e66046400000000000000122ca34e8969515b4bf72501341f43a3ac3e82eef0a56c320c14ea9d7f2be28ae803000000000000a4a3e9c2325b0514a503ae21aa7df257f0b63fe21a6fcf79d8c5921119c2f38a102700000000000000000000000000000000000000000000000000000000000000000000000000008622732b65835fc4391f3aa7dd9fad6bf22f0ba69b8ea510f55091f8a3748dd68622732b65835fc4391f3aa7dd9fad6bf22f0ba69b8ea510f55091f8a3748dd6"), SER_NETWORK, PROTOCOL_VERSION);
    stream >> block;
    return block;
}
