// Copyright (c) 2010 Satoshi Nakamoto
// Copyright (c) 2009-2019 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <chainparams.h>
#include <consensus/merkle.h>

#include <tinyformat.h>
#include <util.h>
#include <utilstrencodings.h>

#include <assert.h>

#include <chainparamsseeds.h>
#include "arith_uint256.h"
#include "key.h"
#include "key_io.h"
#include "tinyformat.h"

static CBlock CreateGenesisBlock(const char* pszTimestamp, const CScript& genesisOutputScript, uint32_t nTime, uint32_t nNonce, uint32_t nBits, int32_t nVersion, const CAmount& genesisReward)
{
    CMutableTransaction txNew;
    txNew.nVersion = 1;

    // Mining Reward
    txNew.vin.resize(1);
    txNew.vin[0].prevout.SetNull();
    txNew.vpout.resize(1);
    txNew.vin[0].scriptSig = CScript() << 486604799 << CScriptNum(4) << std::vector<unsigned char>((const unsigned char*)pszTimestamp, (const unsigned char*)pszTimestamp + strlen(pszTimestamp));

    OUTPUT_PTR<CTxOutStandard> outCoinbase = MAKE_OUTPUT<CTxOutStandard>();
    outCoinbase->scriptPubKey = genesisOutputScript;
    outCoinbase->nValue = genesisReward;
    txNew.vpout[0] = (std::move(outCoinbase));

    CBlock genesis;
    genesis.SetNull();
    genesis.nTime    = nTime;
    genesis.nBits    = nBits;
    genesis.nNonce   = nNonce;
    genesis.nVersion = nVersion;
    genesis.vtx.push_back(MakeTransactionRef(std::move(txNew)));
    genesis.hashPrevBlock.SetNull();
    genesis.hashMerkleRoot = BlockMerkleRoot(genesis);
    genesis.hashWitnessMerkleRoot = BlockWitnessMerkleRoot(genesis);
    genesis.hashPoFN = uint256();
    genesis.mapAccumulatorHashes = genesis.mapAccumulatorHashes;
    genesis.hashVeilData = genesis.GetVeilDataHash(); // This has to be done after both merkle roots and the map accumulatorHashes have been assigned

    // Use this to mine new genesis block
    arith_uint256 hashTarget = arith_uint256().SetCompact(genesis.nBits);
    uint256 thash;
    while (true)
    {
        thash = genesis.GetPoWHash();
        if (UintToArith256(thash) <= hashTarget)
            break;
        if ((genesis.nNonce & 0xF) == 0) {
            //printf("nonce %08X: hash = %s (target = %s)\n", genesis.nNonce, thash.ToString().c_str(), hashTarget.ToString().c_str());
        }
        ++genesis.nNonce;
        if (genesis.nNonce == 0)
        {
            printf("NONCE WRAPPED, incrementing time\n");
            ++genesis.nTime;
        }
    }

//    printf("genesis block hash: %s\n", genesis.GetHash().GetHex().c_str());
//    printf("genesis nonce: %d\n", genesis.nNonce);
//    printf("genesis merkle root: %s\n", genesis.hashMerkleRoot.GetHex().c_str());
//    printf("genesis witness merkle root: %s\n", genesis.hashWitnessMerkleRoot.GetHex().c_str());
//    printf("genesis veil data hash: %s\n", genesis.hashVeilData.GetHex().c_str());

    return genesis;
}

/**
 * Build the genesis block. Note that the output of its generation
 * transaction cannot be spent since it did not originally exist in the
 * database.
 *
 * CBlock(hash=000000000019d6, ver=1, hashPrevBlock=00000000000000, hashMerkleRoot=4a5e1e, nTime=1231006505, nBits=1d00ffff, nNonce=2083236893, vtx=1)
 *   CTransaction(hash=4a5e1e, ver=1, vin.size=1, vout.size=1, nLockTime=0)
 *     CTxIn(COutPoint(000000, -1), coinbase 04ffff001d0104455468652054696d65732030332f4a616e2f32303039204368616e63656c6c6f72206f6e206272696e6b206f66207365636f6e64206261696c6f757420666f722062616e6b73)
 *     CTxOut(nValue=50.00000000, scriptPubKey=0x5F1DF16B2B704C8A578D0B)
 *   vMerkleTree: 4a5e1e
 */
static CBlock CreateGenesisBlock(uint32_t nTime, uint32_t nNonce, uint32_t nBits, int32_t nVersion, const CAmount& genesisReward)
{
    const char* pszTimestamp = "NPR 01/Jan/2019 NASA Probe Sends Pictures Of An Object 4 Billion Miles From The Sun";
    const CScript genesisOutputScript = CScript() << ParseHex("04678afdb0fe5548271967f1a67130b7105cd6a828e03909a67962e0ea1f61deb649f6bc3f4cef38c4f35504e51ec112de5c384df7ba0b8d578a4c702b6bf11d5f") << OP_CHECKSIG;
    return CreateGenesisBlock(pszTimestamp, genesisOutputScript, nTime, nNonce, nBits, nVersion, genesisReward);
}

bool CChainParams::IsBech32Prefix(const std::vector<unsigned char> &vchPrefixIn) const
{
    for (auto &hrp : bech32Prefixes)  {
        if (vchPrefixIn == hrp) {
            return true;
        }
    }

    return false;
};

bool CChainParams::IsBech32Prefix(const std::vector<unsigned char> &vchPrefixIn, CChainParams::Base58Type &rtype) const
{
    for (size_t k = 0; k < MAX_BASE58_TYPES; ++k) {
        auto &hrp = bech32Prefixes[k];
        if (vchPrefixIn == hrp) {
            rtype = static_cast<CChainParams::Base58Type>(k);
            return true;
        }
    }

    return false;
};

bool CChainParams::IsBech32Prefix(const char *ps, size_t slen, CChainParams::Base58Type &rtype) const
{
    for (size_t k = 0; k < MAX_BASE58_TYPES; ++k)
    {
        const auto &hrp = bech32Prefixes[k];
        size_t hrplen = hrp.size();
        if (hrplen > 0
            && slen > hrplen
            && strncmp(ps, (const char*)&hrp[0], hrplen) == 0)
        {
            rtype = static_cast<CChainParams::Base58Type>(k);
            return true;
        };
    };

    return false;
};

void CChainParams::UpdateVersionBitsParameters(Consensus::DeploymentPos d, int64_t nStartTime, int64_t nTimeout)
{
    consensus.vDeployments[d].nStartTime = nStartTime;
    consensus.vDeployments[d].nTimeout = nTimeout;
}

libzerocoin::ZerocoinParams* CChainParams::Zerocoin_Params() const
{
    assert(this);
    static CBigNum bnDecModulus = 0;
    if (!bnDecModulus)
        bnDecModulus.SetDec(zerocoinModulus);
    static libzerocoin::ZerocoinParams ZCParamsDec = libzerocoin::ZerocoinParams(bnDecModulus);

    return &ZCParamsDec;
}

/**
 * Main network
 */
/**
 * What makes a good checkpoint block?
 * + Is surrounded by blocks with reasonable timestamps
 *   (no blocks before with a timestamp after, none after with
 *    timestamp before)
 * + Contains no strange transactions
 */

class CMainParams : public CChainParams {
public:
    CMainParams() {
        strNetworkID = "main";
        consensus.nSubsidyHalvingInterval = 210000;
        consensus.BIP16Exception = uint256S("0x00000000000002dc756eebf4f49723ed8d30cc28a5f108eb94b1ba88ac4f9c22");
        consensus.BIP34Height = 227931;
        consensus.BIP34Hash = uint256S("0x000000000000024b89b42a942fe0d9fea3bb44ab7bd1b19115dd6a759c0808b8");
        consensus.BIP65Height = 388381; // 000000000000000004c2b624ed5d7756c508d90fd0da2c7c679febfa6c4735f0
        consensus.BIP66Height = 363725; // 00000000000000000379eaa19dce8c9b722d46ae6a57c2f1a988119488b50931

        consensus.powLimit = uint256S("7fffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff");
        consensus.nPowTargetSpacing = 120; // alternate PoW/PoS every one minute
        consensus.nDgwPastBlocks = 30; // number of blocks to average in Dark Gravity Wave
        consensus.fPowAllowMinDifficultyBlocks = false;
        consensus.fPowNoRetargeting = false;
        consensus.nRuleChangeActivationThreshold = 1080; // 75% of confirmation window
        consensus.nMinerConfirmationWindow = 1440; // 1 day at 1 block per minute
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].bit = 28;
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].nStartTime = 1199145601; // January 1, 2008
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].nTimeout = 1230767999; // December 31, 2008

        // Deployment of BIP68, BIP112, and BIP113.
        consensus.vDeployments[Consensus::DEPLOYMENT_CSV].bit = 0;
        consensus.vDeployments[Consensus::DEPLOYMENT_CSV].nStartTime = 1462060800; // May 1st, 2016
        consensus.vDeployments[Consensus::DEPLOYMENT_CSV].nTimeout = 1493596800; // May 1st, 2017

        // Deployment of SegWit (BIP141, BIP143, and BIP147)
        consensus.vDeployments[Consensus::DEPLOYMENT_SEGWIT].bit = 1;
        consensus.vDeployments[Consensus::DEPLOYMENT_SEGWIT].nStartTime = 1479168000; // November 15th, 2016.
        consensus.vDeployments[Consensus::DEPLOYMENT_SEGWIT].nTimeout = 1510704000; // November 15th, 2017.

        consensus.vDeployments[Consensus::DEPLOYMENT_POS_WEIGHT].bit = 2;
        consensus.vDeployments[Consensus::DEPLOYMENT_POS_WEIGHT].nStartTime = 1548161817;
        consensus.vDeployments[Consensus::DEPLOYMENT_POS_WEIGHT].nTimeout = 1579805817;

        // The best chain should have at least this much work.
        consensus.nMinimumChainWork = uint256S("0x0000000000000000000000000000000000000000000000000000000000000000");

        // By default assume that the signatures in ancestors of this block are valid.
        consensus.defaultAssumeValid = uint256S("0x0000000000000000002e63058c023a9a1de233554f28c7b21380b6c9003f36a8"); //534292

        consensus.nMinRCTOutputDepth = 12;

        /**
         * The message start string is designed to be unlikely to occur in normal data.
         * The characters are rarely used upper ASCII, not valid as UTF-8, and produce
         * a large 32-bit integer with any alignment.
         */
        pchMessageStart[0] = 0xb6;
        pchMessageStart[1] = 0xcf;
        pchMessageStart[2] = 0xd0;
        pchMessageStart[3] = 0xa3;
        nDefaultPort = 58810;
        nPruneAfterHeight = 100000;

        int nTimeStart = 1540413025;
        arith_uint256 nBits;
        nBits.SetCompact(0x1e0ffff0);
        uint32_t nNonce = 3492319;
        genesis = CreateGenesisBlock(nTimeStart, nNonce, 0x1e0ffff0, 1, 50 * COIN);
        consensus.hashGenesisBlock = genesis.GetHash();
        assert(genesis.hashWitnessMerkleRoot == uint256S("0xa6d192b185dc382a8d7e7dbb5f7a212a54cb93b94e6b9e08869d9169c04993b0"));
        assert(consensus.hashGenesisBlock == uint256S("0x051be91d426dfff0a2a3b8895a0726d997c2749c501b581dd739687e706d7f0b"));
        assert(genesis.hashMerkleRoot == uint256S("0xa6d192b185dc382a8d7e7dbb5f7a212a54cb93b94e6b9e08869d9169c04993b0"));
        assert(genesis.hashVeilData == uint256S("0x8b7f273daa09d2d0fa6abeb27a2a87a4ee6c947ac04931f4f3b6b83f1cf7ad3f"));

        vSeeds.emplace_back("veilseed.presstab.pw");
        vSeeds.emplace_back("veil.seed.fuzzbawls.pw"); // Fuzzbawls seeder - supports x1, x5, x9
        vSeeds.emplace_back("veil.seed2.fuzzbawls.pw"); // Fuzzbawls seeder - supports x1, x5, x9

        base58Prefixes[PUBKEY_ADDRESS] = std::vector<unsigned char>(1,70);
        base58Prefixes[SCRIPT_ADDRESS] = std::vector<unsigned char>(1,5);
        base58Prefixes[SECRET_KEY] =     std::vector<unsigned char>(1,128);
        base58Prefixes[STEALTH_ADDRESS] ={0x84}; // v
        base58Prefixes[EXT_PUBLIC_KEY] = {0x04, 0x88, 0xB2, 0x1E};
        base58Prefixes[EXT_SECRET_KEY] = {0x04, 0x88, 0xAD, 0xE4};
        nBIP44ID = 0x800002ba;
        nRingCTAccount = 20000;
        nZerocoinAccount = 100000;

        //sv for "stealth veil" & bv for "basecoin veil"
        bech32Prefixes[STEALTH_ADDRESS].assign("sv","sv"+2);
        bech32Prefixes[BASE_ADDRESS].assign("bv", "bv"+2);
        bech32_hrp_stealth = "sv";
        bech32_hrp_base = "bv";

        fDefaultConsistencyChecks = false;
        fRequireStandard = true;
        fMineBlocksOnDemand = false;

        checkpointData = {
            {
                { 95, uint256S("0x09d5711299f02d411ae2b49e0e5ca351af747eb8b5644867c078cbeeadc02626")},
                { 280, uint256S("0x53e66a0f8f4139db93a1f38403012c95bbabef7620d61ae25fed7277e868477f")},
                { 1600, uint256S("0xb9f631a0b74b062baa8a01958b66058e8437ed751900ed84165543ec0ed312b5")},
                { 1880, uint256S("0x862c43c183583b364d8d2a35f9d1ca9198d844c1b972aab06c30520b59f6e4f6")},
                { 12500, uint256S("0xa36df367e933c731c59caf5b99a7b0a0d893858fead77e6248e01f44f3c621d7")},
                { 29000, uint256S("0xb1f7b8cc4669ba57c341c3dd49da16d174f9c2a0673c5f3556225b9f8bb4454e")},
                { 36000, uint256S("0x24d1d2662203f225bb16e9535928dd2493033c1ef10124f241d9a6f36d9bf242")},
                { 52000, uint256S("0x96867cbf3f54e5dbdc19d237d264df6734eaea5975e30db41922aa3c14bd64c0")},
            }
        };

        chainTxData = ChainTxData{
            // Data from rpc: getchaintxstats 4096 0000000000000000002e63058c023a9a1de233554f28c7b21380b6c9003f36a8
            /* nTime    */ 1549688281,
            /* nTxCount */ 166433,
            /* dTxRate  */ 0.0558
        };

        /* disable fallback fee on mainnet */
        m_fallback_fee_enabled = false;

        nMaxNetworkReward = 10 * COIN;
        strNetworkRewardAddress = "3AxxVeiLxxxxVeiLxxxxVeiLxxxwy7FAkd";
        nMaxPoWBlocks = 5;
        nConsecutivePoWHeight = 15000;

        /** Zerocoin */
        zerocoinModulus = "25195908475657893494027183240048398571429282126204032027777137836043662020707595556264018525880784"
                          "4069182906412495150821892985591491761845028084891200728449926873928072877767359714183472702618963750149718246911"
                          "6507761337985909570009733045974880842840179742910064245869181719511874612151517265463228221686998754918242243363"
                          "7259085141865462043576798423387184774447920739934236584823824281198163815010674810451660377306056201619676256133"
                          "8441436038339044149526344321901146575444541784240209246165157233507787077498171257724679629263863563732899121548"
                          "31438167899885040445364023527381951378636564391212010397122822120720357";
        nMaxZerocoinSpendsPerTransaction = 20; // Assume about 6.5kb each
        nMinZerocoinMintFee = 1 * CENT; //high fee required for zerocoin mints
        nMintRequiredConfirmations = 20; //the maximum amount of confirmations until accumulated in 19
        nRequiredAccumulation = 1;
        nDefaultSecurityLevel = 100; //full security level for accumulators
        nZerocoinRequiredStakeDepth = 200; //The required confirmations for a zerocoin to be stakable
        nHeightPoSStart = 1500;
        nKernelModulus = 100;
        nCoinbaseMaturity = 100;
        nProofOfFullNodeRounds = 4;
        nLastPOWBlock = 2000000;
        nHeightSupplyCreationStop = 9816000; //Should create very close to 300m coins at this time
        nTimeEnforceWeightReduction = 1548619029; //Stake weight must be reduced for higher denominations
        nHeightProtocolBumpEnforcement = 86350; // 50 blocks before superblock

        /** RingCT/Stealth **/
        nDefaultRingSize = 11;

        nMaxHeaderRequestWithoutPoW = 50;
    }
};

/**
 * Testnet (v3)
 */
class CTestNetParams : public CChainParams {
public:
    CTestNetParams() {
        strNetworkID = "test";
        consensus.nSubsidyHalvingInterval = 210000;
        consensus.BIP16Exception = uint256S("0x00000000dd30457c001f4095d208cc1296b0eed002427aa599874af7a432b105");
        consensus.BIP34Height = 21111;
        consensus.BIP34Hash = uint256S("0x0000000023b3a96d3484e5abb3755c413e7d41500f8e2a5c3f0dd01299cd8ef8");
        consensus.BIP65Height = 581885; // 00000000007f6655f22f98e72ed80d8b06dc761d5da09df0fa1dc4be4f861eb6
        consensus.BIP66Height = 330776; // 000000002104c8c45e99a8853285a3b592602a3ccde2b832481da85e9e4ba182
        consensus.powLimit = uint256S("0000ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff");
        consensus.nPowTargetSpacing = 120; // alternate PoW/PoS every one minute
        consensus.nDgwPastBlocks = 60; // number of blocks to average in Dark Gravity Wave
        consensus.fPowAllowMinDifficultyBlocks = true;
        consensus.fPowNoRetargeting = false;
        consensus.nRuleChangeActivationThreshold = 84; // 75% for testchains
        consensus.nMinerConfirmationWindow = 120; // 2 hours
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].bit = 28;
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].nStartTime = 1199145601; // January 1, 2008
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].nTimeout = 1230767999; // December 31, 2008

        // Deployment of BIP68, BIP112, and BIP113.
        consensus.vDeployments[Consensus::DEPLOYMENT_CSV].bit = 0;
        consensus.vDeployments[Consensus::DEPLOYMENT_CSV].nStartTime = 1456790400; // March 1st, 2016
        consensus.vDeployments[Consensus::DEPLOYMENT_CSV].nTimeout = 1493596800; // May 1st, 2017

        // Deployment of SegWit (BIP141, BIP143, and BIP147)
        consensus.vDeployments[Consensus::DEPLOYMENT_SEGWIT].bit = 1;
        consensus.vDeployments[Consensus::DEPLOYMENT_SEGWIT].nStartTime = 1462060800; // May 1st 2016
        consensus.vDeployments[Consensus::DEPLOYMENT_SEGWIT].nTimeout = 1493596800; // May 1st 2017

        consensus.vDeployments[Consensus::DEPLOYMENT_POS_WEIGHT].bit = 2;
        consensus.vDeployments[Consensus::DEPLOYMENT_POS_WEIGHT].nStartTime = 1548269817;
        consensus.vDeployments[Consensus::DEPLOYMENT_POS_WEIGHT].nTimeout = 1579805817;

        // The best chain should have at least this much work.
        consensus.nMinimumChainWork = uint256S("0x0000000000000000000000000000000000000000000000000000000000000000");

        // By default assume that the signatures in ancestors of this block are valid.
        consensus.defaultAssumeValid = uint256S("0x0000000000000037a8cd3e06cd5edbfe9dd1dbcc5dacab279376ef7cfc2b4c75"); //1354312

        consensus.nMinRCTOutputDepth = 12;

        pchMessageStart[0] = 0xa8;
        pchMessageStart[1] = 0xd1;
        pchMessageStart[2] = 0xa7;
        pchMessageStart[3] = 0xc4;
        nDefaultPort = 58811;
        nPruneAfterHeight = 1000;

        int nTimeStart = 1548379385;
        uint32_t nNonce = 4234676;
        genesis = CreateGenesisBlock(nTimeStart, nNonce, 0x1e0ffff0, 1, 50 * COIN);
        consensus.hashGenesisBlock = genesis.GetHash();
        assert(consensus.hashGenesisBlock == uint256S("0xec7d8a93639c6bbf10954c71a2db69617bd90db72b353321927081836939df7a"));
        assert(genesis.hashMerkleRoot == uint256S("0xa6d192b185dc382a8d7e7dbb5f7a212a54cb93b94e6b9e08869d9169c04993b0"));
        assert(genesis.hashWitnessMerkleRoot == uint256S("0xa6d192b185dc382a8d7e7dbb5f7a212a54cb93b94e6b9e08869d9169c04993b0"));
        assert(genesis.hashVeilData == uint256S("0x8b7f273daa09d2d0fa6abeb27a2a87a4ee6c947ac04931f4f3b6b83f1cf7ad3f"));

        vFixedSeeds.clear();
        vSeeds.clear();

        vSeeds.emplace_back("veilseedtestnet.presstab.pw");
        vSeeds.emplace_back("veil-testnet.seed.fuzzbawls.pw"); // Fuzzbawls seeder - supports x1, x5, x9
        vSeeds.emplace_back("veil-testnet.seed2.fuzzbawls.pw"); // Fuzzbawls seeder - supports x1, x5, x9
        vSeeds.emplace_back("45.34.187.118", "45.34.187.118"); // blondfrogs single IP

        base58Prefixes[PUBKEY_ADDRESS] = std::vector<unsigned char>(1,111);
        base58Prefixes[SCRIPT_ADDRESS] = std::vector<unsigned char>(1,196);
        base58Prefixes[SECRET_KEY] =     std::vector<unsigned char>(1,239);
        base58Prefixes[STEALTH_ADDRESS]    = {0x84}; // v
        base58Prefixes[EXT_PUBLIC_KEY] = {0x04, 0x35, 0x87, 0xCF};
        base58Prefixes[EXT_SECRET_KEY] = {0x04, 0x35, 0x83, 0x94};

        bech32Prefixes[STEALTH_ADDRESS].assign("tps","tps"+3);
        bech32Prefixes[BASE_ADDRESS].assign("tv", "tv"+2);
        nBIP44ID = 0x80000001;
        nRingCTAccount = 20000;
        nZerocoinAccount = 100000;

        bech32_hrp_stealth = "tps";
        bech32_hrp_base = "tv";

        fDefaultConsistencyChecks = false;
        fRequireStandard = false;
        fMineBlocksOnDemand = false;

        nMaxPoWBlocks = 5;
        nConsecutivePoWHeight = 15000;

        checkpointData = {
            {
//                {42, uint256S("37cf752dfcda2c1c3f1a1a3ff3b7ef47c40d0e06f8c16e058a3b04cfa1566acb")},
//                {1600, uint256S("66959ef58a234711c64c5f609f6b44a597b02ccfedea6557e1ee08fe792fb747")},
//                {3000, uint256S("eac7585e03c690e7b12da0d330e7dc8cdb60c24d300f50712dd7b6e3583f5cad")},
//                {3300, uint256S("0f273634343a8aeebe80305a21663749288f351e59da263242dd7846ce375ee3")},
//                {4400, uint256S("6455521644d55f58000a32d285017f288d05388c868d4fc40bbdce70a4b1debd")},
//                {7000, uint256S("4894c80ccfc672a9e91d5ebfb5b5f1510a96486e3c0792dff77fcb642ef59f23")},
//                {8000, uint256S("ae3dbf201952e2e382759381f9dcdea4c97c8c03199605963b35de4b448d0c8d")},
//                {8300, uint256S("30e86f6e98cebf5d6306648056e4a1c6e653109223965736260b9d47efaa9be3")},
//                {8500, uint256S("7e4bcfd3a4c4efd0724d8b8c2c3964844ca1a76d3c61af96d3293c5a685913b1")},
//                {8700, uint256S("74f188b3dc86c00f97fdd216af7a4f416042b67ac0b9b899deb5b46cdb4b1f50")},
//                {9100, uint256S("cbc788271338f927d9c1499d0c24489b518496c704028595d247971cb7795446")},
            }
        };

        chainTxData = ChainTxData{
            // Data from rpc: getchaintxstats 4096 0000000000000037a8cd3e06cd5edbfe9dd1dbcc5dacab279376ef7cfc2b4c75
            /* nTime    */ 1546202591,
            /* nTxCount */ 15397,
            /* dTxRate  */ 0.0034
        };

        /* enable fallback fee on testnet */
        m_fallback_fee_enabled = true;

        strNetworkRewardAddress = "tv1qhzkv6xdc7zpfx9ldsrqpk84hkcf36kclsyyeeh";
        nMaxNetworkReward = 10 * COIN;

        /** Zerocoin */
        zerocoinModulus = "25195908475657893494027183240048398571429282126204032027777137836043662020707595556264018525880784"
                          "4069182906412495150821892985591491761845028084891200728449926873928072877767359714183472702618963750149718246911"
                          "6507761337985909570009733045974880842840179742910064245869181719511874612151517265463228221686998754918242243363"
                          "7259085141865462043576798423387184774447920739934236584823824281198163815010674810451660377306056201619676256133"
                          "8441436038339044149526344321901146575444541784240209246165157233507787077498171257724679629263863563732899121548"
                          "31438167899885040445364023527381951378636564391212010397122822120720357";
        nMaxZerocoinSpendsPerTransaction = 20; // Assume about 6.5kb each
        nMinZerocoinMintFee = 1 * CENT; //high fee required for zerocoin mints
        nMintRequiredConfirmations = 20; //the maximum amount of confirmations until accumulated in 19
        nRequiredAccumulation = 1;
        nDefaultSecurityLevel = 100; //full security level for accumulators
        nZerocoinRequiredStakeDepth = 10; //The required confirmations for a zerocoin to be stakable
        nHeightPoSStart = 100;
        nKernelModulus = 10;
        nCoinbaseMaturity = 10;
        nProofOfFullNodeRounds = 4;
        nLastPOWBlock = 2000000;
        nHeightSupplyCreationStop = 9816000; //Should create very close to 300m coins at this time
        nTimeEnforceWeightReduction = 1548849600; //Stake weight must be reduced for higher denominations (GMT): Wednesday, January 30, 2019 12:00:00 PM

        /** RingCT/Stealth **/
        nDefaultRingSize = 11;

        nMaxHeaderRequestWithoutPoW = 50;
    }
};

/**
 * Regression test
 */
class CRegTestParams : public CChainParams {
public:
    CRegTestParams() {
        strNetworkID = "regtest";
        consensus.nSubsidyHalvingInterval = 150;
        consensus.BIP16Exception = uint256();
        consensus.BIP34Height = 100000000; // BIP34 has not activated on regtest (far in the future so block v1 are not rejected in tests)
        consensus.BIP34Hash = uint256();
        consensus.BIP65Height = 1351; // BIP65 activated on regtest (Used in rpc activation tests)
        consensus.BIP66Height = 1251; // BIP66 activated on regtest (Used in rpc activation tests)
        consensus.powLimit = uint256S("7fffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff");
        consensus.nPowTargetSpacing = 120; // alternate PoW/PoS every one minute
        consensus.nDgwPastBlocks = 60; // number of blocks to average in Dark Gravity Wave
        consensus.fPowAllowMinDifficultyBlocks = true;
        consensus.fPowNoRetargeting = true;
        consensus.nRuleChangeActivationThreshold = 108; // 75% for testchains
        consensus.nMinerConfirmationWindow = 144; // Faster than normal for regtest (144 instead of 2016)
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].bit = 28;
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].nStartTime = 0;
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].nTimeout = Consensus::BIP9Deployment::NO_TIMEOUT;
        consensus.vDeployments[Consensus::DEPLOYMENT_CSV].bit = 0;
        consensus.vDeployments[Consensus::DEPLOYMENT_CSV].nStartTime = 0;
        consensus.vDeployments[Consensus::DEPLOYMENT_CSV].nTimeout = Consensus::BIP9Deployment::NO_TIMEOUT;
        consensus.vDeployments[Consensus::DEPLOYMENT_SEGWIT].bit = 1;
        consensus.vDeployments[Consensus::DEPLOYMENT_SEGWIT].nStartTime = Consensus::BIP9Deployment::ALWAYS_ACTIVE;
        consensus.vDeployments[Consensus::DEPLOYMENT_SEGWIT].nTimeout = Consensus::BIP9Deployment::NO_TIMEOUT;

        // The best chain should have at least this much work.
        consensus.nMinimumChainWork = uint256S("0x00");

        // By default assume that the signatures in ancestors of this block are valid.
        consensus.defaultAssumeValid = uint256S("0x00");

        pchMessageStart[0] = 0xfa;
        pchMessageStart[1] = 0xbf;
        pchMessageStart[2] = 0xb5;
        pchMessageStart[3] = 0xda;
        nDefaultPort = 58821;
        nPruneAfterHeight = 1000;

        genesis = CreateGenesisBlock(1296688602, 3962663, 0x207fffff, 1, 50 * COIN);
        consensus.hashGenesisBlock = genesis.GetHash();
//        assert(consensus.hashGenesisBlock == uint256S("0x0b229468d80839ed5162523e375f8da1d84adae0889745500625ea8a098b3f1d"));
//        assert(genesis.hashMerkleRoot == uint256S("0x5891ed0f483b598260f3cb95b2d13c4bf20bbc2ad44160e0c84a5fb1477402e3"));

        vFixedSeeds.clear(); //!< Regtest mode doesn't have any fixed seeds.
        vSeeds.clear();      //!< Regtest mode doesn't have any DNS seeds.

        fDefaultConsistencyChecks = true;
        fRequireStandard = false;
        fMineBlocksOnDemand = true;

        checkpointData = {
            {
            //    {0, uint256S("5372a455dfe82eb03a8eb7470ce8b256d151b3f43f698478e8958a67e2ef72da")},
            }
        };

        chainTxData = ChainTxData{
            0,
            0,
            0
        };

        base58Prefixes[PUBKEY_ADDRESS] = std::vector<unsigned char>(1,111);
        base58Prefixes[SCRIPT_ADDRESS] = std::vector<unsigned char>(1,196);
        base58Prefixes[SECRET_KEY] =     std::vector<unsigned char>(1,239);
        base58Prefixes[EXT_PUBLIC_KEY] = {0x04, 0x35, 0x87, 0xCF};
        base58Prefixes[EXT_SECRET_KEY] = {0x04, 0x35, 0x83, 0x94};

        bech32_hrp_stealth = "bcrt";

        /* enable fallback fee on regtest */
        m_fallback_fee_enabled = true;

        strNetworkRewardAddress = "2N9sWUmygPRy1c14eFWt8FzA8YF4JgA6j6a";

        /** Zerocoin */
        zerocoinModulus = "25195908475657893494027183240048398571429282126204032027777137836043662020707595556264018525880784"
                          "4069182906412495150821892985591491761845028084891200728449926873928072877767359714183472702618963750149718246911"
                          "6507761337985909570009733045974880842840179742910064245869181719511874612151517265463228221686998754918242243363"
                          "7259085141865462043576798423387184774447920739934236584823824281198163815010674810451660377306056201619676256133"
                          "8441436038339044149526344321901146575444541784240209246165157233507787077498171257724679629263863563732899121548"
                          "31438167899885040445364023527381951378636564391212010397122822120720357";
        nMaxZerocoinSpendsPerTransaction = 7; // Assume about 20kb each
        nMinZerocoinMintFee = 1 * CENT; //high fee required for zerocoin mints
        nMintRequiredConfirmations = 20; //the maximum amount of confirmations until accumulated in 19
        nRequiredAccumulation = 1;
        nDefaultSecurityLevel = 100; //full security level for accumulators
        nZerocoinRequiredStakeDepth = 400; //The required confirmations for a zerocoin to be stakable
        nTimeEnforceWeightReduction = 1548849600; //Stake weight must be reduced for higher denominations (GMT): Wednesday, January 30, 2019 12:00:00 PM

        nMaxHeaderRequestWithoutPoW = 50;
    }
};

static std::unique_ptr<CChainParams> globalChainParams;

const CChainParams &Params() {
    assert(globalChainParams);
    return *globalChainParams;
}

std::unique_ptr<CChainParams> CreateChainParams(const std::string& chain)
{
    if (chain == CBaseChainParams::MAIN)
        return std::unique_ptr<CChainParams>(new CMainParams());
    else if (chain == CBaseChainParams::TESTNET)
        return std::unique_ptr<CChainParams>(new CTestNetParams());
    else if (chain == CBaseChainParams::REGTEST)
        return std::unique_ptr<CChainParams>(new CRegTestParams());
    throw std::runtime_error(strprintf("%s: Unknown chain %s.", __func__, chain));
}

void SelectParams(const std::string& network)
{
    SelectBaseParams(network);
    globalChainParams = CreateChainParams(network);
}

void UpdateVersionBitsParameters(Consensus::DeploymentPos d, int64_t nStartTime, int64_t nTimeout)
{
    globalChainParams->UpdateVersionBitsParameters(d, nStartTime, nTimeout);
}
