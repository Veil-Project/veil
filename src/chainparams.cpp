// Copyright (c) 2010 Satoshi Nakamoto
// Copyright (c) 2009-2019 The Bitcoin Core developers
// Copyright (c) 2018-2020 The Veil developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <chainparams.h>
#include <consensus/merkle.h>

#include <tinyformat.h>
#include <util.h>
#include <utilstrencodings.h>

#include <assert.h>

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
        thash = genesis.GetX16RTPoWHash();
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

// Adjustment made to relect the proper balance of the accumulators of the the overspend exploits
int CChainParams::Zerocoin_OverSpendAdjustment(libzerocoin::CoinDenomination denom) const
{
    if (strNetworkID == "main") {
        switch (denom) {
            case libzerocoin::ZQ_TEN:
                return -(1 + 2325 + 1015);
            case libzerocoin::ZQ_ONE_HUNDRED:
                return 1;
            case libzerocoin::ZQ_ONE_THOUSAND:
                return 2325;
            case libzerocoin::ZQ_TEN_THOUSAND:
                return 1015;
            default:
                return 0;
        }
    }

    return 0;
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
        consensus.powLimitRandomX = uint256S("0000ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff");
        consensus.powLimitProgPow = uint256S("0000000fffffffffffffffffffffffffffffffffffffffffffffffffffffffff");
        consensus.powLimitSha256 = uint256S("0000ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff");

        consensus.nPowTargetSpacing = 120; // alternate PoW/PoS every one minute

        // ProgPow, RandomX, Sha256d
        consensus.nProgPowTargetSpacing = 172;
        consensus.nRandomXTargetSpacing = 600;
        consensus.nSha256DTargetSpacing = 1200;

        consensus.nDgwPastBlocks = 60; // number of blocks to average in Dark Gravity Wave
        consensus.nDgwPastBlocks_old  = 30; // number of blocks to average in Dark Gravity Wave
        consensus.fPowAllowMinDifficultyBlocks = false;
        consensus.fPowNoRetargeting = false;
        consensus.nRuleChangeActivationThreshold = 84; // 70% of confirmation window
        consensus.nMinerConfirmationWindow = 120; // 2 hours at 1 block per minute

        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].bit = 27;
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
        consensus.vDeployments[Consensus::DEPLOYMENT_POS_WEIGHT].nTimeout = 1576226440;

        consensus.vDeployments[Consensus::DEPLOYMENT_ZC_LIMP].bit = 3;
        consensus.vDeployments[Consensus::DEPLOYMENT_ZC_LIMP].nStartTime = 1556347500;
        consensus.vDeployments[Consensus::DEPLOYMENT_ZC_LIMP].nTimeout = 1579805817;

        // The best chain should have at least this much work.
        consensus.nMinimumChainWork = uint256S("0x000000000000000000000000000000000000000000000000000000005442ac21");

        // By default assume that the signatures in ancestors of this block are valid.
        consensus.defaultAssumeValid = uint256S("0x4e280b14b8bf62cc6dbb9db87ad2e3a8cb0be9265790b89ffb1c053fd47e6543"); //1012533

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

        /** Timestamp when to switch to ProgPow, RandomX, Sha256D. UTC based **/
        nPowUpdateTimestamp = 1604163600; // Saturday, 31 October 2020, 5:00:00 PM GMT
        /// Used by block.h for serialization
        nPowTimeStampActive = 1604163600;

        nHeightKIenforce = 1212090;
        nTimeKIfork = 1621180800;

        int nTimeStart = 1540413025;
        uint32_t nNonce = 3492319;
        genesis = CreateGenesisBlock(nTimeStart, nNonce, 0x1e0ffff0, 1, 50 * COIN);
        consensus.hashGenesisBlock = genesis.GetHash();
        assert(genesis.hashWitnessMerkleRoot == uint256S("0xa6d192b185dc382a8d7e7dbb5f7a212a54cb93b94e6b9e08869d9169c04993b0"));
        assert(consensus.hashGenesisBlock == uint256S("0x051be91d426dfff0a2a3b8895a0726d997c2749c501b581dd739687e706d7f0b"));
        assert(genesis.hashMerkleRoot == uint256S("0xa6d192b185dc382a8d7e7dbb5f7a212a54cb93b94e6b9e08869d9169c04993b0"));
        assert(genesis.hashVeilData == uint256S("0x8b7f273daa09d2d0fa6abeb27a2a87a4ee6c947ac04931f4f3b6b83f1cf7ad3f"));

        vSeeds.emplace_back("node01.veil-project.com");
        vSeeds.emplace_back("node02.veil-project.com");
        vSeeds.emplace_back("node03.veil-project.com");
        vSeeds.emplace_back("node04.veil-project.com");
        vSeeds.emplace_back("node05.veil-project.com");
        vSeeds.emplace_back("node06.veil-project.com");
        vSeeds.emplace_back("node07.veil-project.com");
        vSeeds.emplace_back("node08.veil-project.com");
        vSeeds.emplace_back("node09.veil-project.com");
        vSeeds.emplace_back("node10.veil-project.com"); // Codeofalltrades seeder
        vSeeds.emplace_back("node11.veil-project.com"); // CaveSpectre seeder
        // single point DNS failure backups
        vSeeds.emplace_back("veilseed.codeofalltrades.com");       // Codeofalltrades seeder
        vSeeds.emplace_back("veilseed.veil-stats.com");            // Codeofalltrades seeder
        vSeeds.emplace_back("veil-seed.pontificatingnobody.com");  // CaveSpectre seeder

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
        bech32Prefixes[STEALTH_ADDRESS].assign("sv",&"sv"[2]);
        bech32Prefixes[BASE_ADDRESS].assign("bv", &"bv"[2]);
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
                { 101000, uint256S("0x42145acdde948865d73a8b318fea09b9e9cb826f93007c7a21b4f103822da86d")},
                { 175500, uint256S("0xf0db2fb676587ccd8e65f509b970b782d2095e1010939bab4a6d59debd633aa8")},
                { 248600, uint256S("0x18903b67287bb5f0fa95c3ab75af2fcf9e483be3d64d3e112b9bc52acb71a2b6")},
                { 320000, uint256S("0xb8007b911602d6f79afe8f0f3a65f04182a19441e90d9c5ce9ce0e53a80073b5")},
                { 337000, uint256S("0x2933365852ca6fffa51a584efe419a1948d65cd186013e406d52377c3dde1890")},
                { 436000, uint256S("0x961c466e63f228b86a242dad09d9a7a0ea0cf26a89aa213b1154fb84a8cb0bd1")},
                { 526000, uint256S("0x96afd32946736010de56510550691a36bf6b3cb116583cdc6ba7b3809ca10665")},
                { 616000, uint256S("0x5bb5720555003d74166fdc57899af4c36719558d20bee6472852c16e5a0c2e86")},
                { 706000, uint256S("0xf98ece86f185e15af9a9d6b554c54f36f0b8b19d11e5e98ffe3b7578a0c8e2f9")},
                { 840000, uint256S("0x9b46be33e4a84456e7c4e4785bad8646cb7cf1b6192adfd4c2916e768254f621")},
            }
        };

        chainTxData = ChainTxData{
            // Data from rpc: getchaintxstats 4096 4e280b14b8bf62cc6dbb9db87ad2e3a8cb0be9265790b89ffb1c053fd47e6543 (height 1012533)
            /* nTime    */ 1608561453,
            /* nTxCount */ 2308996,
            /* dTxRate  */ 0.0353
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
        nZerocoinRequiredStakeDepthV2 = 1000; //The required confirmations for a zerocoin to be stakable
        nHeightPoSStart = 1500;
        nKernelModulus = 100;
        nCoinbaseMaturity = 100;
        nProofOfFullNodeRounds = 4;
        nLastPOWBlock = 2000000;
        nHeightSupplyCreationStop = 9816000; //Should create very close to 300m coins at this time
        nTimeEnforceWeightReduction = 1548619029; //Stake weight must be reduced for higher denominations
        nHeightProtocolBumpEnforcement = 86350; // 50 blocks before superblock
        nHeightCheckDenom = 321700;
        nHeightLightZerocoin = 335975;
        nValueBlacklist = (282125 + 60540) * COIN;
        nHeightEnforceBlacklist = 336413;
        nPreferredMintsPerBlock = 70; //Miner will not include more than this many mints per block
        nPreferredMintsPerTx = 15; //Do not consider a transaction as standard that includes more than this many mints

        /** RingCT/Stealth **/
        nDefaultRingSize = 11;

        nMaxHeaderRequestWithoutPoW = 50;
    }
};

/**
 * Testnet (v4)
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
        consensus.powLimitRandomX = uint256S("0000ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff");
        consensus.powLimitProgPow = uint256S("0000000fffffffffffffffffffffffffffffffffffffffffffffffffffffffff");
        consensus.powLimitSha256 = uint256S("0000ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff");

        consensus.nPowTargetSpacing = 120; // alternate PoW/PoS every one minute

        // ProgPow, RandomX, Sha256d
        consensus.nProgPowTargetSpacing = 172;
        consensus.nRandomXTargetSpacing = 600;
        consensus.nSha256DTargetSpacing = 1200;

        consensus.nDgwPastBlocks = 60; // number of blocks to average in Dark Gravity Wave
        consensus.nDgwPastBlocks_old = 60; // number of blocks to average in Dark Gravity Wave
        consensus.fPowAllowMinDifficultyBlocks = true;
        consensus.fPowNoRetargeting = false;
        consensus.nRuleChangeActivationThreshold = 15; // 75% for testchains
        consensus.nMinerConfirmationWindow = 20; // 20 minutes
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].bit = 27;
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
        consensus.vDeployments[Consensus::DEPLOYMENT_POS_WEIGHT].nStartTime = Consensus::BIP9Deployment::ALWAYS_ACTIVE;
        consensus.vDeployments[Consensus::DEPLOYMENT_POS_WEIGHT].nTimeout = Consensus::BIP9Deployment::NO_TIMEOUT;

        consensus.vDeployments[Consensus::DEPLOYMENT_ZC_LIMP].bit = 3;
        consensus.vDeployments[Consensus::DEPLOYMENT_ZC_LIMP].nStartTime = Consensus::BIP9Deployment::ALWAYS_ACTIVE;
        consensus.vDeployments[Consensus::DEPLOYMENT_ZC_LIMP].nTimeout = Consensus::BIP9Deployment::NO_TIMEOUT;

        // The best chain should have at least this much work.
        consensus.nMinimumChainWork = uint256S("0x0000000000000000000000000000000000000000000000000000000000000000");

        // By default assume that the signatures in ancestors of this block are valid.
        consensus.defaultAssumeValid = uint256S("0xe95fc76c6c9016e8ed2e4e4a2641dfc91dbf6bad4df659f664d8f7614bc010c0"); //103000

        consensus.nMinRCTOutputDepth = 12;

        pchMessageStart[0] = 0xa8;
        pchMessageStart[1] = 0xd1;
        pchMessageStart[2] = 0xa7;
        pchMessageStart[3] = 0xc4;
        nDefaultPort = 58811;
        nPruneAfterHeight = 1000;

        /** Timestamp when to switch to ProgPow, RandomX, Sha256D. UTC based **/
        nPowUpdateTimestamp = 1602806399; // Tuesday, 15 October 2020, 11:59:59 PM GMT
        nPowTimeStampActive = 1602806399; // Used by block.h for serialization

        nHeightKIenforce = 594157;
        nTimeKIfork = 1621180800;

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

        vSeeds.emplace_back("testnode01.veil-project.com");
        vSeeds.emplace_back("testnode02.veil-project.com");
        vSeeds.emplace_back("testnode03.veil-project.com");
        vSeeds.emplace_back("testnode04.veil-project.com");
        vSeeds.emplace_back("testnode05.veil-project.com");
        vSeeds.emplace_back("testnode06.veil-project.com");
        vSeeds.emplace_back("testnode07.veil-project.com");
        vSeeds.emplace_back("testnode08.veil-project.com"); // Codeofalltrades seeder
        vSeeds.emplace_back("testnode09.veil-project.com"); // CaveSpectre seeder
        // single point DNS failure backups
        vSeeds.emplace_back("veilseedtestnet.codeofalltrades.com");     // Codeofalltrades seeder
        vSeeds.emplace_back("veil-seed-test.pontificatingnobody.com");  // CaveSpectre seeder

        base58Prefixes[PUBKEY_ADDRESS] = std::vector<unsigned char>(1,111);
        base58Prefixes[SCRIPT_ADDRESS] = std::vector<unsigned char>(1,196);
        base58Prefixes[SECRET_KEY] =     std::vector<unsigned char>(1,239);
        base58Prefixes[STEALTH_ADDRESS]    = {0x84}; // v
        base58Prefixes[EXT_PUBLIC_KEY] = {0x04, 0x35, 0x87, 0xCF};
        base58Prefixes[EXT_SECRET_KEY] = {0x04, 0x35, 0x83, 0x94};

        bech32Prefixes[STEALTH_ADDRESS].assign("tps",&"tps"[3]);
        bech32Prefixes[BASE_ADDRESS].assign("tv", &"tv"[2]);
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
                    { 1, uint256S("0x918ebe520f7666375d7e4dbb0c269f675440b96b0413ab92bbf28b85126197cd")},
                    { 95, uint256S("0x1c1d4a474a167a3d474ad7ebda5dfc5560445f885519cb98595aab6f818b1f6f")}
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
        nMintRequiredConfirmations = 10; //the maximum amount of confirmations until accumulated in 19
        nRequiredAccumulation = 1;
        nDefaultSecurityLevel = 100; //full security level for accumulators
        nZerocoinRequiredStakeDepth = 10; //The required confirmations for a zerocoin to be stakable
        nZerocoinRequiredStakeDepthV2 = 10; //The required confirmations for a zerocoin to be stakable
        nHeightPoSStart = 100;
        nKernelModulus = 10;
        nCoinbaseMaturity = 10;
        nProofOfFullNodeRounds = 4;
        nLastPOWBlock = 2000000;
        nHeightSupplyCreationStop = 9816000; //Should create very close to 300m coins at this time
        nTimeEnforceWeightReduction = 1548849600; //Stake weight must be reduced for higher denominations (GMT): Wednesday, January 30, 2019 12:00:00 PM

        nHeightLightZerocoin = 9428;
        nHeightEnforceBlacklist = 0;

        /** RingCT/Stealth **/
        nDefaultRingSize = 11;

        nMaxHeaderRequestWithoutPoW = 50;
        nPreferredMintsPerBlock = 70; //Miner will not include more than this many mints per block
        nPreferredMintsPerTx = 15; //Do not consider a transaction as standard that includes more than this many mints
    }
};

class CDevNetParams : public CChainParams {
public:
    CDevNetParams() {
        strNetworkID = "dev";
        consensus.nSubsidyHalvingInterval = 210000;
        consensus.BIP16Exception = uint256S("0x00000000dd30457c001f4095d208cc1296b0eed002427aa599874af7a432b105");
        consensus.BIP34Height = 21111;
        consensus.BIP34Hash = uint256S("0x0000000023b3a96d3484e5abb3755c413e7d41500f8e2a5c3f0dd01299cd8ef8");
        consensus.BIP65Height = 581885; // 00000000007f6655f22f98e72ed80d8b06dc761d5da09df0fa1dc4be4f861eb6
        consensus.BIP66Height = 330776; // 000000002104c8c45e99a8853285a3b592602a3ccde2b832481da85e9e4ba182
        consensus.powLimit = uint256S("0000ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff");
        consensus.powLimitRandomX = uint256S("0000ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff");
        consensus.powLimitProgPow = uint256S("0000000fffffffffffffffffffffffffffffffffffffffffffffffffffffffff");
        consensus.powLimitSha256 = uint256S("0000ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff");
        consensus.nPowTargetSpacing = 120; // alternate PoW/PoS every one minute

        // ProgPow, RandomX, Sha256d
        consensus.nProgPowTargetSpacing = 172;
        consensus.nRandomXTargetSpacing = 600;
        consensus.nSha256DTargetSpacing = 1200;

        consensus.nDgwPastBlocks = 60; // number of blocks to average in Dark Gravity Wave
        consensus.nDgwPastBlocks_old = 60; // number of blocks to average in Dark Gravity Wave
        consensus.fPowAllowMinDifficultyBlocks = true;
        consensus.fPowNoRetargeting = false;
        consensus.nRuleChangeActivationThreshold = 15; // 75% for testchains
        consensus.nMinerConfirmationWindow = 20; // 20 minutes
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].bit = 27;
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].nStartTime = 1199145601; // January 1, 2008
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].nTimeout = 1230767999; // December 31, 2008

        // Deployment of BIP68, BIP112, and BIP113.
        consensus.vDeployments[Consensus::DEPLOYMENT_CSV].bit = 0;
        consensus.vDeployments[Consensus::DEPLOYMENT_CSV].nStartTime = Consensus::BIP9Deployment::ALWAYS_ACTIVE;
        consensus.vDeployments[Consensus::DEPLOYMENT_CSV].nTimeout = Consensus::BIP9Deployment::NO_TIMEOUT;

        // Deployment of SegWit (BIP141, BIP143, and BIP147)
        consensus.vDeployments[Consensus::DEPLOYMENT_SEGWIT].bit = 1;
        consensus.vDeployments[Consensus::DEPLOYMENT_SEGWIT].nStartTime = Consensus::BIP9Deployment::ALWAYS_ACTIVE;
        consensus.vDeployments[Consensus::DEPLOYMENT_SEGWIT].nTimeout = Consensus::BIP9Deployment::NO_TIMEOUT;

        consensus.vDeployments[Consensus::DEPLOYMENT_POS_WEIGHT].bit = 2;
        consensus.vDeployments[Consensus::DEPLOYMENT_POS_WEIGHT].nStartTime = Consensus::BIP9Deployment::ALWAYS_ACTIVE;
        consensus.vDeployments[Consensus::DEPLOYMENT_POS_WEIGHT].nTimeout = Consensus::BIP9Deployment::NO_TIMEOUT;

        consensus.vDeployments[Consensus::DEPLOYMENT_ZC_LIMP].bit = 3;
        consensus.vDeployments[Consensus::DEPLOYMENT_ZC_LIMP].nStartTime = Consensus::BIP9Deployment::ALWAYS_ACTIVE;
        consensus.vDeployments[Consensus::DEPLOYMENT_ZC_LIMP].nTimeout = Consensus::BIP9Deployment::NO_TIMEOUT;

        // The best chain should have at least this much work.
        consensus.nMinimumChainWork = uint256S("0x0000000000000000000000000000000000000000000000000000000000000000");

        // By default assume that the signatures in ancestors of this block are valid.
        consensus.defaultAssumeValid = uint256S("0xe054229317f002436b1bb67b5e72b442299bcd5bd6cc5740b4ea6c6e5efba583");

        consensus.nMinRCTOutputDepth = 12;

        pchMessageStart[0] = 0xa8;
        pchMessageStart[1] = 0xd1;
        pchMessageStart[2] = 0xa7;
        pchMessageStart[3] = 0xc4;
        nDefaultPort = 58816;
        nPruneAfterHeight = 1000;

        /** Timestamp when to switch to ProgPow, RandomX, Sha256D. UTC based **/
        nPowUpdateTimestamp = 1584372883; // Mon Mar 16 2020 09:34:43
        nPowTimeStampActive = 1584372883; // Used by block.h for serialization

        nHeightKIenforce = 0;
        nTimeKIfork = 4776508800;

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
        vSeeds.emplace_back("veil-devnet-seed.asoftwaresolution.com");  // Blondfrogs seeder
        vSeeds.emplace_back("devnode01.veil-project.com");
        vSeeds.emplace_back("devnode02.veil-project.com");
        vSeeds.emplace_back("devnode03.veil-project.com");
        vSeeds.emplace_back("devnode04.veil-project.com"); 
        vSeeds.emplace_back("devnode05.veil-project.com"); // Codeofalltrades seeder
        vSeeds.emplace_back("devnode06.veil-project.com"); // CaveSpectre seeder
        // single point DNS failure backups
        vSeeds.emplace_back("veil-devnet-seed.codeofalltrades.com");     // Codeofalltrades seeder
        vSeeds.emplace_back("veil-seed-dev.pontificatingnobody.com");  // CaveSpectre seeder


        base58Prefixes[PUBKEY_ADDRESS] = std::vector<unsigned char>(1,111);
        base58Prefixes[SCRIPT_ADDRESS] = std::vector<unsigned char>(1,196);
        base58Prefixes[SECRET_KEY] =     std::vector<unsigned char>(1,239);
        base58Prefixes[STEALTH_ADDRESS]    = {0x84}; // v
        base58Prefixes[EXT_PUBLIC_KEY] = {0x04, 0x35, 0x87, 0xCF};
        base58Prefixes[EXT_SECRET_KEY] = {0x04, 0x35, 0x83, 0x94};

        bech32Prefixes[STEALTH_ADDRESS].assign("tps",&"tps"[3]);
        bech32Prefixes[BASE_ADDRESS].assign("tv", &"tv"[2]);
        nBIP44ID = 0x80000001;
        nRingCTAccount = 20000;
        nZerocoinAccount = 100000;

        bech32_hrp_stealth = "tps";
        bech32_hrp_base = "tv";

        fDefaultConsistencyChecks = false;
        fRequireStandard = false;
        fMineBlocksOnDemand = false;

        nMaxPoWBlocks = 20;
        nConsecutivePoWHeight = 15000;

        checkpointData = {
            {
//                    { 1, uint256S("0x46a540411202d9e304187c50377ec5b5baecf1adb040f1629c2daec50b493f8b")},
//                    { 17, uint256S("0xe054229317f002436b1bb67b5e72b442299bcd5bd6cc5740b4ea6c6e5efba583")}
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
        nMintRequiredConfirmations = 10; //the maximum amount of confirmations until accumulated in 19
        nRequiredAccumulation = 1;
        nDefaultSecurityLevel = 100; //full security level for accumulators
        nZerocoinRequiredStakeDepth = 10; //The required confirmations for a zerocoin to be stakable
        nZerocoinRequiredStakeDepthV2 = 10; //The required confirmations for a zerocoin to be stakable
        nHeightPoSStart = 100;
        nKernelModulus = 10;
        nCoinbaseMaturity = 10;
        nProofOfFullNodeRounds = 4;
        nLastPOWBlock = 2000000;
        nHeightSupplyCreationStop = 9816000; //Should create very close to 300m coins at this time
        nTimeEnforceWeightReduction = 1548849600; //Stake weight must be reduced for higher denominations (GMT): Wednesday, January 30, 2019 12:00:00 PM

        nHeightLightZerocoin = 1000;
        nHeightEnforceBlacklist = 0;

        /** RingCT/Stealth **/
        nDefaultRingSize = 11;

        nMaxHeaderRequestWithoutPoW = 50;
        nPreferredMintsPerBlock = 70; //Miner will not include more than this many mints per block
        nPreferredMintsPerTx = 15; //Do not consider a transaction as standard that includes more than this many mints
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
        consensus.powLimitRandomX = uint256S("0000ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff");
        consensus.powLimitProgPow = uint256S("0000000fffffffffffffffffffffffffffffffffffffffffffffffffffffffff");
        consensus.powLimitSha256 = uint256S("7fffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff");

        consensus.nPowTargetSpacing = 120; // alternate PoW/PoS every one minute

        // ProgPow, RandomX, Sha256d
        consensus.nProgPowTargetSpacing = 172;
        consensus.nRandomXTargetSpacing = 600;
        consensus.nSha256DTargetSpacing = 1200;

        consensus.nDgwPastBlocks = 60; // number of blocks to average in Dark Gravity Wave
        consensus.nDgwPastBlocks_old = 60; // number of blocks to average in Dark Gravity Wave
        consensus.fPowAllowMinDifficultyBlocks = true;
        consensus.fPowNoRetargeting = false;
        consensus.nRuleChangeActivationThreshold = 108; // 75% for testchains
        consensus.nMinerConfirmationWindow = 144; // Faster than normal for regtest (144 instead of 2016)
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].bit = 27;
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].nStartTime = 0;
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].nTimeout = Consensus::BIP9Deployment::NO_TIMEOUT;
        consensus.vDeployments[Consensus::DEPLOYMENT_CSV].bit = 0;
        consensus.vDeployments[Consensus::DEPLOYMENT_CSV].nStartTime = 0;
        consensus.vDeployments[Consensus::DEPLOYMENT_CSV].nTimeout = Consensus::BIP9Deployment::NO_TIMEOUT;
        consensus.vDeployments[Consensus::DEPLOYMENT_SEGWIT].bit = 1;
        consensus.vDeployments[Consensus::DEPLOYMENT_SEGWIT].nStartTime = Consensus::BIP9Deployment::ALWAYS_ACTIVE;
        consensus.vDeployments[Consensus::DEPLOYMENT_SEGWIT].nTimeout = Consensus::BIP9Deployment::NO_TIMEOUT;

        // The best chain should have at least this much work.
        consensus.nMinimumChainWork = uint256S("0x0000000000000000000000000000000000000000000000000000000000000000");

        // By default assume that the signatures in ancestors of this block are valid.
        consensus.defaultAssumeValid = uint256S("0x0000000000000000000000000000000000000000000000000000000000000000");

        consensus.nMinRCTOutputDepth = 12;

        pchMessageStart[0] = 0xfa;
        pchMessageStart[1] = 0xbf;
        pchMessageStart[2] = 0xb5;
        pchMessageStart[3] = 0xda;
        nDefaultPort = 58821;

        nPruneAfterHeight = 1000;
        nConsecutivePoWHeight = 1000000;
        nLastPOWBlock = 2000000;
        nHeightSupplyCreationStop = 9816000;

        // These need to be set before the genesis block is checked
        /** Timestamp when to switch to ProgPow, RandomX, Sha256D. UTC based **/
        nPowUpdateTimestamp = 1597617382; // On from that start
        /// Used by block.h for serialization
        nPowTimeStampActive = nPowUpdateTimestamp;

        nHeightKIenforce = 100;
        nTimeKIfork = 4776508800;

        genesis = CreateGenesisBlock(1597617372, 3962663, 0x207fffff, 1, 50 * COIN);
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
        base58Prefixes[STEALTH_ADDRESS]    = {0x84}; // v
        base58Prefixes[EXT_PUBLIC_KEY] = {0x04, 0x35, 0x87, 0xCF};
        base58Prefixes[EXT_SECRET_KEY] = {0x04, 0x35, 0x83, 0x94};

        bech32Prefixes[STEALTH_ADDRESS].assign("tps", &"tps"[3]);
        bech32Prefixes[BASE_ADDRESS].assign("tv", &"tv"[2]);
        nBIP44ID = 0x80000001;
        nRingCTAccount = 20000;
        nZerocoinAccount = 100000;

        bech32_hrp_stealth = "tps";
        bech32_hrp_base = "tv";

        fDefaultConsistencyChecks = false;
        fRequireStandard = false;
        fMineBlocksOnDemand = false;

        /* enable fallback fee on regtest */
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
        nMintRequiredConfirmations = 10; //the maximum amount of confirmations until accumulated in 19
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

        nHeightLightZerocoin = 500;
        nZerocoinRequiredStakeDepthV2 = 10; //The required confirmations for a zerocoin to be stakable
        nHeightEnforceBlacklist = 0;

        nMaxHeaderRequestWithoutPoW = 50;
        nPreferredMintsPerBlock = 70; //Miner will not include more than this many mints per block
        nPreferredMintsPerTx = 15; //Do not consider a transaction as standard that includes more than this many mints
    }
};

static std::unique_ptr<CChainParams> globalChainParams;

const CChainParams &Params() {
    assert(globalChainParams);
    return *globalChainParams;
}

// Not called prior to Algo change fork
int64_t CChainParams::GetDwgPastBlocks(const CBlockIndex* pindex, const int nPowType, const bool fProofOfStake) const
{
    assert(pindex->GetBlockTime() >= Params().PowUpdateTimestamp()); // Shouldn't be called if we're not active on the new PoW system
    if (fProofOfStake)
        return consensus.nDgwPastBlocks * 2; // count twice as many blocks
    return consensus.nDgwPastBlocks;
}

// Not called prior to Algo change fork
int64_t CChainParams::GetTargetSpacing(const CBlockIndex* pindex, const int nPoWType, const bool fProofOfStake) const
{
    assert(pindex->GetBlockTime() >= Params().PowUpdateTimestamp()); // Shouldn't be called if we're not active on the new PoW system
    if (nPoWType & CBlockHeader::PROGPOW_BLOCK)
        return consensus.nProgPowTargetSpacing;
    if (nPoWType & CBlockHeader::RANDOMX_BLOCK)
        return consensus.nRandomXTargetSpacing;
    if (nPoWType & CBlockHeader::SHA256D_BLOCK)
        return consensus.nSha256DTargetSpacing;
    if (fProofOfStake)
        return consensus.nPowTargetSpacing/2; // Special case - actual block spacing
    return consensus.nPowTargetSpacing;
}

bool CChainParams::CheckKIenforced(const CBlockIndex* pindex) const
{
    if (pindex->nHeight >= Params().HeightKIenforce()) {
        return true;
    }
    return false;
}

bool CChainParams::CheckKIenforced(int nSpendHeight) const
{
    if (nSpendHeight >= Params().HeightKIenforce()) {
        return true;
    }
    return false;
}


std::unique_ptr<CChainParams> CreateChainParams(const std::string& chain)
{
    if (chain == CBaseChainParams::MAIN)
        return std::unique_ptr<CChainParams>(new CMainParams());
    else if (chain == CBaseChainParams::TESTNET)
        return std::unique_ptr<CChainParams>(new CTestNetParams());
    else if (chain == CBaseChainParams::DEVNET)
        return std::unique_ptr<CChainParams>(new CDevNetParams());
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
