// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2019 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_CHAINPARAMS_H
#define BITCOIN_CHAINPARAMS_H

#include <chainparamsbase.h>
#include <consensus/params.h>
#include <primitives/block.h>
#include <protocol.h>
#include <libzerocoin/Params.h>

#include <memory>
#include <vector>

static const uint32_t CHAIN_NO_GENESIS = 444444;
static const uint32_t CHAIN_NO_STEALTH_SPEND = 444445; // used hardened

struct SeedSpec6 {
    uint8_t addr[16];
    uint16_t port;
};

typedef std::map<int, uint256> MapCheckpoints;

struct CCheckpointData {
    MapCheckpoints mapCheckpoints;
};

/**
 * Holds various statistics on transactions within a chain. Used to estimate
 * verification progress during chain sync.
 *
 * See also: CChainParams::TxData, GuessVerificationProgress.
 */
struct ChainTxData {
    int64_t nTime;    //!< UNIX timestamp of last known number of transactions
    int64_t nTxCount; //!< total number of transactions between genesis and that timestamp
    double dTxRate;   //!< estimated number of transactions per second after that timestamp
};

/**
 * CChainParams defines various tweakable parameters of a given instance of the
 * Bitcoin system. There are three: the main network on which people trade goods
 * and services, the public test network which gets reset from time to time and
 * a regression test mode which is intended for private networks only. It has
 * minimal difficulty to ensure that blocks can be found instantly.
 */
class CChainParams
{
public:
    enum Base58Type {
        PUBKEY_ADDRESS,
        SCRIPT_ADDRESS,
        SECRET_KEY,
        EXT_PUBLIC_KEY,
        EXT_SECRET_KEY,

        STEALTH_ADDRESS,
        BASE_ADDRESS, //Used in Bech32
        EXT_KEY_HASH,
        EXT_ACC_HASH,
        EXT_PUBLIC_KEY_BTC,
        EXT_SECRET_KEY_BTC,
        PUBKEY_ADDRESS_256,
        SCRIPT_ADDRESS_256,
        STAKE_ONLY_PKADDR,
        MAX_BASE58_TYPES
    };

    const Consensus::Params& GetConsensus() const { return consensus; }
    const CMessageHeader::MessageStartChars& MessageStart() const { return pchMessageStart; }
    int GetDefaultPort() const { return nDefaultPort; }

    const CBlock& GenesisBlock() const { return genesis; }
    /** Default value for -checkmempool and -checkblockindex argument */
    bool DefaultConsistencyChecks() const { return fDefaultConsistencyChecks; }
    /** Policy: Filter transactions that do not match well-defined patterns */
    bool RequireStandard() const { return fRequireStandard; }
    /** The max amount added to a block reward from funds sent to the network reward address **/
    CAmount MaxNetworkReward() const { return nMaxNetworkReward; }
    int MaxConsecutivePoWBlocks() const { return nMaxPoWBlocks; }
    int ConsecutivePoWHeight() const { return nConsecutivePoWHeight; }
    /** The address to send funds to to increase block rewards **/
    std::string NetworkRewardAddress() const { return strNetworkRewardAddress; }
    uint64_t PruneAfterHeight() const { return nPruneAfterHeight; }
    /** Make miner stop after a block is found. In RPC, don't return until nGenProcLimit blocks are generated */
    bool MineBlocksOnDemand() const { return fMineBlocksOnDemand; }
    /** Return the BIP70 network string (main, test or regtest) */
    std::string NetworkIDString() const { return strNetworkID; }
    /** Return true if the fallback fee is by default enabled for this network */
    bool IsFallbackFeeEnabled() const { return m_fallback_fee_enabled; }
    /** Return the list of hostnames to look up for DNS seeds */
    const std::vector<std::string>& DNSSeeds() const { return vSeeds; }
    const std::vector<unsigned char>& Base58Prefix(Base58Type type) const { return base58Prefixes[type]; }
    const std::vector<unsigned char>& Bech32Prefix(Base58Type type) const { return bech32Prefixes[type]; }
    const std::string& Bech32HRPStealth() const { return bech32_hrp_stealth; }
    const std::string& Bech32HRPBase() const {return bech32_hrp_base; }
    const std::vector<SeedSpec6>& FixedSeeds() const { return vFixedSeeds; }
    const CCheckpointData& Checkpoints() const { return checkpointData; }
    const ChainTxData& TxData() const { return chainTxData; }
    void UpdateVersionBitsParameters(Consensus::DeploymentPos d, int64_t nStartTime, int64_t nTimeout);

    bool IsBech32Prefix(const std::vector<unsigned char> &vchPrefixIn) const;
    bool IsBech32Prefix(const std::vector<unsigned char> &vchPrefixIn, CChainParams::Base58Type &rtype) const;
    bool IsBech32Prefix(const char *ps, size_t slen, CChainParams::Base58Type &rtype) const;

    /** BIP32, BIP39, BIP44 **/
    uint32_t BIP44ID() const { return nBIP44ID; }
    uint32_t BIP32_RingCT_Account() const { return nRingCTAccount; }
    uint32_t BIP32_Zerocoin_Account() const { return nZerocoinAccount; }

    /** Zerocoin **/
    libzerocoin::ZerocoinParams* Zerocoin_Params() const;
    std::string Zerocoin_Modulus() const { return zerocoinModulus; }
    int Zerocoin_MaxSpendsPerTransaction() const { return nMaxZerocoinSpendsPerTransaction; }
    CAmount Zerocoin_MintFee() const { return nMinZerocoinMintFee; }
    int Zerocoin_MintRequiredConfirmations() const { return nMintRequiredConfirmations; }
    int Zerocoin_RequiredAccumulation() const { return nRequiredAccumulation; }
    int Zerocoin_DefaultSpendSecurity() const { return nDefaultSecurityLevel; }
    int Zerocoin_RequiredStakeDepth() const { return nZerocoinRequiredStakeDepth; }
    int Zerocoin_RequiredStakeDepthV2() const { return nZerocoinRequiredStakeDepthV2; }
    int Zerocoin_OverSpendAdjustment(libzerocoin::CoinDenomination denom) const;
    CAmount ValueBlacklisted() const { return nValueBlacklist; }
    int Zerocoin_PreferredMintsPerBlock() const { return nPreferredMintsPerBlock; }
    int Zerocoin_PreferredMintsPerTransaction() const { return nPreferredMintsPerTx; }

    /** RingCT and Stealth **/
    int DefaultRingSize() const { return nDefaultRingSize; }

    /** Consensus params **/
    int LAST_POW_BLOCK() const { return nLastPOWBlock; }
    int HeightPoSStart() const { return nHeightPoSStart; }
    int KernelModulus() const { return nKernelModulus; }
    int CoinbaseMaturity() const { return nCoinbaseMaturity; }
    int HeightSupplyCreationStop() const { return nHeightSupplyCreationStop; }
    int ProofOfFullNodeRounds() const {return nProofOfFullNodeRounds; }
    int EnforceWeightReductionTime() const { return nTimeEnforceWeightReduction; }
    int HeightProtocolBumpEnforcement() const { return nHeightProtocolBumpEnforcement; }
    int MaxHeaderRequestWithoutPoW() const { return nMaxHeaderRequestWithoutPoW; }
    int BIP9Period() const { return consensus.nMinerConfirmationWindow; }
    int HeightCheckDenom() const { return nHeightCheckDenom; }
    int HeightLightZerocoin() const { return nHeightLightZerocoin; }
    int HeightEnforceBlacklist() const { return nHeightEnforceBlacklist; }

    uint32_t PowUpdateTimestamp() const { return nPowUpdateTimestamp; }

protected:
    CChainParams() {}

    Consensus::Params consensus;
    CMessageHeader::MessageStartChars pchMessageStart;
    int nDefaultPort;
    CAmount nMaxNetworkReward;
    int nMaxPoWBlocks;
    int nConsecutivePoWHeight;
    std::string strNetworkRewardAddress;
    uint64_t nPruneAfterHeight;
    std::vector<std::string> vSeeds;
    std::vector<unsigned char> base58Prefixes[MAX_BASE58_TYPES];
    std::vector<unsigned char> bech32Prefixes[MAX_BASE58_TYPES];
    std::string bech32_hrp_stealth;
    std::string bech32_hrp_base;
    std::string strNetworkID;
    uint32_t nBIP44ID;
    uint32_t nRingCTAccount;
    uint32_t nZerocoinAccount;
    CBlock genesis;
    std::vector<SeedSpec6> vFixedSeeds;
    bool fDefaultConsistencyChecks;
    bool fRequireStandard;
    bool fMineBlocksOnDemand;
    CCheckpointData checkpointData;
    ChainTxData chainTxData;
    bool m_fallback_fee_enabled;

    // zerocoin
    std::string zerocoinModulus;
    int nMaxZerocoinSpendsPerTransaction;
    CAmount nMinZerocoinMintFee;
    int nMintRequiredConfirmations;
    int nRequiredAccumulation;
    int nDefaultSecurityLevel;
    CAmount nValueBlacklist;
    int nPreferredMintsPerBlock;
    int nPreferredMintsPerTx;

    //RingCT/Stealth
    int nDefaultRingSize;

    //Proof of Stake/Consensus
    int64_t nBudget_Fee_Confirmations;
    int nZerocoinRequiredStakeDepth;
    int nZerocoinRequiredStakeDepthV2;
    int nHeightPoSStart;
    int nKernelModulus;
    int nLastPOWBlock;
    int nCoinbaseMaturity;
    int nProofOfFullNodeRounds;
    int nHeightSupplyCreationStop;

    //Time and height enforcements
    int nTimeEnforceWeightReduction;
    int nHeightProtocolBumpEnforcement; // the height a new protobump is enforced
    int nHeightCheckDenom;
    int nHeightLightZerocoin;
    int nHeightEnforceBlacklist;

    //Settings that are not chain critical, but should not be edited unless the person changing understands the consequence
    int nMaxHeaderRequestWithoutPoW;

    uint32_t nPowUpdateTimestamp;
};

/**
 * Creates and returns a std::unique_ptr<CChainParams> of the chosen chain.
 * @returns a CChainParams* of the chosen chain.
 * @throws a std::runtime_error if the chain is not supported.
 */
std::unique_ptr<CChainParams> CreateChainParams(const std::string& chain);

/**
 * Return the currently selected parameters. This won't change after app
 * startup, except for unit tests.
 */
const CChainParams &Params();

/**
 * Sets the params returned by Params() to those for the given BIP70 chain name.
 * @throws std::runtime_error when the chain is not supported.
 */
void SelectParams(const std::string& chain);

/**
 * Allows modifying the Version Bits regtest parameters.
 */
void UpdateVersionBitsParameters(Consensus::DeploymentPos d, int64_t nStartTime, int64_t nTimeout);

#endif // BITCOIN_CHAINPARAMS_H
