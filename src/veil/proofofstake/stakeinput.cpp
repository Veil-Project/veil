// Copyright (c) 2017-2019 The PIVX developers
// Copyright (c) 2019-2022 The Veil developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <tinyformat.h>
#include "veil/zerocoin/accumulators.h"
#include "veil/zerocoin/mintmeta.h"
#include "chain.h"
#include "chainparams.h"
#include "consensus/consensus.h"
#include "wallet/deterministicmint.h"
#include "validation.h"
#include "stakeinput.h"
#include "veil/proofofstake/kernel.h"
#include "veil/ringct/anon.h"
#include "veil/ringct/blind.h"
#ifdef ENABLE_WALLET
#include "wallet/coincontrol.h"
#include "wallet/wallet.h"
#include "veil/ringct/anonwallet.h"
#endif

// Based on https://stackoverflow.com/a/23000588
int fast_log16(uint64_t value)
{
    // Round value up to the nearest 2^k - 1
    value |= value >> 1;
    value |= value >> 2;
    value |= value >> 4;
    value |= value >> 8;
    value |= value >> 16;
    value |= value >> 32;
    // value & 0x1111... reduces the max index of the leftmost 1 down to a multiple of 4
    // There are now only 16 possible values
    // Multiply by 0x0111... results in a unique value in the top 4 bits
    // Thanks to the repeated addition, this is exactly log16 of our number.
    return ((value & 0x1111111111111111u) * 0x0111111111111111u) >> 60;
}

// Use the PoW hash or the PoS hash
uint256 GetHashFromIndex(const CBlockIndex* pindexSample)
{
    if (pindexSample->IsProofOfWork()) {
        // By using the pindex time and checking it against the PoWUpdateTimestamp we can tell the code to either
        // set the veildatahash to all zeros if True is passed, or to do nothing if False is passed.
        // When mining to the local wallet aggressively we have found that occassionaly the memory of the pindex block data
        // shows that the veildatahash is not zero (0000xxxx). This made the wallet not accept valid PoS blocks from the chain tip
        // and allowed the wallet to fork off. This fix allows us to pass in the boolean that is used to tell the code
        // to set the veildatahash to zero manually for us. This can be done only after the PoWUpdateTimestamp as veildatahash isn't used
        // in the block hash calculation after that timestamp.
        return pindexSample->GetX16RTPoWHash(pindexSample->nTime >= Params().PowUpdateTimestamp());
    }

    uint256 hashProof = pindexSample->GetBlockPoSHash();
    return hashProof;
}

// As the sampling gets further back into the chain, use more bits of entropy. This prevents the ability to significantly
// impact the modifier if you create one of the most recent blocks used. For example, the contribution from the first sample
// (which is 101 blocks from the coin tip) will only be 2 bits, thus only able to modify the end result of the modifier
// 4 different ways. As the sampling gets further back into the chain (take previous sample height - nSampleCount*6 for the height
// it will be) it has more entropy that is assigned to that specific sampling. The further back in the chain, the less
// ability there is to have any influence at all over the modifier calculations going forward from that specific block.
// The bits taper down as it gets deeper into the chain so that the precomputability is less significant.
int GetSampleBits(int nSampleCount)
{
    switch(nSampleCount) {
        case 0:
            return 2;
        case 1:
            return 4;
        case 2:
            return 8;
        case 3:
            return 16;
        case 4:
            return 32;
        case 5:
            return 64;
        case 6:
            return 128;
        case 7:
            return 64;
        case 8:
            return 32;
        case 9:
            return 16;
        default:
            return 0;
    }
}

bool GetStakeModifier(uint64_t& nStakeModifier, const CBlockIndex& pindexChainPrev)
{
    uint256 hashModifier;
    //Use a new modifier that is less able to be "grinded"
    int nHeightChain = pindexChainPrev.nHeight;
    int nHeightPrevious = nHeightChain - 100;
    for (int i = 0; i < 10; i++) {
        int nHeightSample = nHeightPrevious - (6*i);
        nHeightPrevious = nHeightSample;
        auto pindexSample = pindexChainPrev.GetAncestor(nHeightSample);

        if (!pindexSample) {
            if (i > 5 && nHeightSample < 0 && (Params().NetworkIDString() == "regtest" || Params().NetworkIDString() == "dev"))
                break;
            return false;
        }
        //Get a sampling of entropy from this block. Rehash the sample, since PoW hashes may have lots of 0's
        uint256 hashSample = GetHashFromIndex(pindexSample);
        hashSample = Hash(hashSample.begin(), hashSample.end());

        //Reduce the size of the sampling
        int nBitsToUse = GetSampleBits(i);
        auto arith = UintToArith256(hashSample);
        arith >>= (256-nBitsToUse);
        hashSample = ArithToUint256(arith);
        hashModifier = Hash(hashModifier.begin(), hashModifier.end(), hashSample.begin(), hashSample.end());
    }

    nStakeModifier = UintToArith256(hashModifier).GetLow64();
    return true;
}

// BRACKETBASE is the Log base that establishes brackets, see below
const uint64_t BRACKETBASE = 16;
// log2 of the BASE is the amount we have to shift by to be equivalent to multiplying by the BASE
// so that << (4 * x) is the same as pow(16, x+1)
const uint32_t LOG2BRACKETBASE = 4;

const CAmount nBareMinStake = BRACKETBASE;
constexpr CAmount nOneSat = 1;

bool CheckMinStake(const CAmount& nAmount)
{
    // Protocol needs to enforce bare minimum (mathematical min). User can define selective min
    if (nAmount <= nBareMinStake)
        return false;
    return true;
}

static constexpr size_t RingCTNumBuckets = 13;
static constexpr uint64_t RingCTWeightsByBracket[RingCTNumBuckets + 1] = {
    0,
    (1ULL << (4 * 1)) + nOneSat,
    (1ULL << (4 * 2)) + nOneSat,
    (1ULL << (4 * 3)) + nOneSat,
    (1ULL << (4 * 4)) + nOneSat,
    (1ULL << (4 * 5)) + nOneSat,
    (1ULL << (4 * 6)) + nOneSat,
    (1ULL << (4 * 7)) + nOneSat,
    (((1ULL << (4 * 8)) + nOneSat) * 95) / 100,
    (((1ULL << (4 * 9)) + nOneSat) * 91) / 100,
    (((1ULL << (4 * 10)) + nOneSat) * 71) / 100,
    (((1ULL << (4 * 11)) + nOneSat) * 5) / 10,
    (((1ULL << (4 * 12)) + nOneSat) * 3) / 10,
    ((1ULL << (4 * 13)) + nOneSat) / 10,
    };

CAmount GetRingCTWeightForValue(const CAmount& nValueIn)
{
    // fast mode
    if (nValueIn <= nBareMinStake)
        return 0;

    // bracket is at least 1 now.
    int bracket = fast_log16(nValueIn - nOneSat);
    return RingCTWeightsByBracket[bracket > RingCTNumBuckets ? RingCTNumBuckets : bracket];
}

// To keep the input private, we use the stake depth as the index from.
const CBlockIndex* RingCTStake::GetIndexFrom(const CBlockIndex* pindexPrev)
{
    if (pindexFrom)
        return pindexFrom;

    pindexFrom = pindexPrev->GetAncestor(pindexPrev->nHeight - Params().RingCT_RequiredStakeDepth());

    return pindexFrom;
}

bool RingCTStake::GetTxFrom(CTransaction& tx)
{
    return false;
}

CAmount RingCTStake::GetValue()
{
    const COutputRecord* oR = coin.rtx->second.GetOutput(coin.i);
    if (!oR)
        return 0;
    return oR->GetAmount();
}

// Returns a weight amount based on a bracket for privacy.
// The bracket is given by log16 (value in sats - 1), and the weight is equal to
// the minimum value of the bracket. Values of 16 sats and below are not eligible
// to be staked.
//
//      Bracket                 min                                     max
//      -------                 ---                                     ---
//         0                    0 (        0.00000000)                 16 (        0.00000016)
//         1                   17 (        0.00000017)                256 (        0.00000256)
//         2                  257 (        0.00000257)               4096 (        0.00004096)
//         3                 4097 (        0.00004097)              65536 (        0.00065536)
//         4                65537 (        0.00065537)            1048576 (        0.01048576)
//         5              1048577 (        0.01048577)           16777216 (        0.16777216)
//         6             16777217 (        0.16777217)          268435456 (        2.68435456)
//         7            268435457 (        2.68435457)         4294967296 (       42.94967296)
//         8           4294967297 (       42.94967297)        68719476736 (      687.19476736)
//         9          68719476737 (      687.19476737)      1099511627776 (    10995.11627776)
//         10       1099511627777 (    10995.11627777)     17592186044416 (   175921.86044416)
//         11      17592186044417 (   175921.86044417)    281474976710656 (  2814749.76710656)
//         12     281474976710657 (  2814749.76710657)   4503599627370496 ( 45035996.27370496)
//         13    4503599627370497 ( 45035996.27370497)  72057594037927936 (720575940.37927936)
CAmount RingCTStake::GetBracketMinValue()
{
    CAmount nValueIn = GetValue();
    // fast mode
    if (nValueIn <= nBareMinStake)
        return 0;

    // bracket is at least 1 now.
    int bracket = fast_log16(nValueIn - nOneSat);
    // We'd do 16 << (4 * (bracket - 1)) but 16 is 1 << 4 so it's really
    // 1 << (4 + 4 * bracket - 4)
    return (1ULL << (4 * bracket)) + nOneSat;
}

// We further reduce the weights of higher brackets to match zerocoin reductions.
CAmount RingCTStake::GetWeight()
{
    if (nWeight == 0) {
        nWeight = GetRingCTWeightForValue(GetValue());
    }
    return nWeight;
}

void RingCTStake::OnStakeFound(const arith_uint256& bnTarget, const uint256& hashProofOfStake)
{
    CAmount nValueIn = GetValue();
    // fast mode
    if (nValueIn <= nBareMinStake)
        return;

    // bracket is at least 1 now.
    arith_uint256 hashProof = UintToArith256(hashProofOfStake);
    int bracket = fast_log16(nValueIn - nOneSat);
    if (bracket > RingCTNumBuckets)
        bracket = RingCTNumBuckets;
    for (--bracket; bracket > 0; --bracket) {
        arith_uint256 newTarget(RingCTWeightsByBracket[bracket]);
        bool overflow = false;
        newTarget.safeMultiply(bnTarget, overflow);
        // in case of overflow, the real target is MAX_UINT256, which is an auto-pass.
        if (overflow) {
            continue;
        }
        // Stop when we find a bucket that fails to meet the target.
        if (hashProof >= newTarget) {
            break;
        }
    }
    ++bracket;
    nWeight = RingCTWeightsByBracket[bracket];
}

bool RingCTStake::GetModifier(uint64_t& nStakeModifier, const CBlockIndex* pindexChainPrev)
{
    if (!pindexChainPrev)
        return false;

    return GetStakeModifier(nStakeModifier, *pindexChainPrev);
}

CDataStream RingCTStake::GetUniqueness()
{
    //The unique identifier for a VEIL RingCT txo is the hash of the keyimage pubkey.
    CDataStream ss(0, 0);
    ss << hashPubKey;
    return ss;
}

bool RingCTStake::MarkSpent(AnonWallet* panonwallet, CMutableTransaction& txNew)
{
    rtx.nFlags |= ORF_ANON_IN;
    rtx.vin.emplace_back(txNew.vin[0].prevout);

    std::vector<COutPoint> spends;
    spends.emplace_back(coin.rtx->first, coin.i);
    panonwallet->MarkInputsAsPendingSpend(spends);

    uint256 txid = txNew.GetHash();
    panonwallet->SaveRecord(txid, rtx);

    return true;
}

/**
 * @brief Create a coinstake transaction from the stake candidate.
 *
 * @note Call CreateCoinStake() after finding a valid stake kernel. A kernel can be found without needing to create the full transaction.
 *
 * @param[in] pwallet: The CWallet that holds the AnonWallet that holds the RingCT output that is being staked.
 * @return <b>true</b> upon success.
 *         <b>false</b> if the AnonWallet fails to find the StakeAddress or if AddAnonInputs() fails.
 */
bool RingCTStake::CreateCoinStake(CWallet* pwallet, const CAmount& nReward, CMutableTransaction& txCoinStake, bool& retryable, CMutableTransaction& txCoinbase)
{
#ifdef ENABLE_WALLET
    if (gArgs.GetBoolArg("-disablewallet", DEFAULT_DISABLE_WALLET)) {
#endif
        return error("%s: wallet disabled", __func__);
#ifdef ENABLE_WALLET
    }
    AnonWallet* panonWallet = pwallet->GetAnonWallet();

    // Update the coinbase tx
    std::string strError;
    if (!panonWallet->AddCoinbaseRewards(txCoinbase, nReward, strError))
        return false;

    CTransactionRef ptx = MakeTransactionRef(txCoinStake);
    CWalletTx wtx(pwallet, ptx);

    //Add the input to coincontrol so that addanoninputs knows what to use
    CCoinControl coinControl;
    coinControl.Select(coin.GetOutpoint(), GetValue());
    coinControl.nCoinType = OUTPUT_RINGCT;
    coinControl.fProofOfStake = true;
    coinControl.nStakeReward = nReward;

    //Tell the rct code who the recipient is
    std::vector<CTempRecipient> vecSend;
    CTempRecipient tempRecipient;
    tempRecipient.nType = OUTPUT_RINGCT;
    tempRecipient.SetAmount(GetValue());
    tempRecipient.address = panonWallet->GetStealthStakeAddress();
    tempRecipient.fSubtractFeeFromAmount = false;
    tempRecipient.fExemptFeeSub = true;
    tempRecipient.fOverwriteRangeProofParams = true;
    tempRecipient.min_value = GetBracketMinValue();
    vecSend.emplace_back(tempRecipient);

    CTransactionRecord rtx;
    CAmount nFeeRet = 0;
    retryable = false;
    if (!panonWallet->AddAnonInputs(
            wtx, rtx, vecSend, /*fSign*/true,
            /*nRingSize*/Params().DefaultRingSize(), /*nInputsPerSig*/32,
            /*nMaximumInputs*/0, nFeeRet, &coinControl, strError))
        return error("%s: AddAnonInputs failed with error %s", __func__, strError);

    txCoinStake = CMutableTransaction(*wtx.tx);
    return true;
#endif
}

ZerocoinStake::ZerocoinStake(const libzerocoin::CoinSpend& spend)
{
    nType = STAKE_ZEROCOIN;
    this->nChecksum = spend.getAccumulatorChecksum();
    this->denom = spend.getDenomination();
    uint256 nSerial = spend.getCoinSerialNumber().getuint256();
    this->hashSerial = Hash(nSerial.begin(), nSerial.end());
    this->pindexFrom = nullptr;
    fMint = false;
}

int ZerocoinStake::GetChecksumHeightFromMint()
{
    int nNewBlockHeight = chainActive.Height() + 1;
    int nHeightChecksum = 0;
    if (nNewBlockHeight >= Params().HeightLightZerocoin()) {
        nHeightChecksum = nNewBlockHeight - Params().Zerocoin_RequiredStakeDepthV2();
    } else {
        nHeightChecksum = nNewBlockHeight - Params().Zerocoin_RequiredStakeDepth();
    }

    //Need to return the first occurance of this checksum in order for the validation process to identify a specific
    //block height
    uint256 nChecksum;
    nChecksum = chainActive[nHeightChecksum]->mapAccumulatorHashes[denom];
    return GetChecksumHeight(nChecksum, denom);
}

int ZerocoinStake::GetChecksumHeightFromSpend()
{
    return GetChecksumHeight(nChecksum, denom);
}

uint256 ZerocoinStake::GetChecksum()
{
    return nChecksum;
}

// The Zerocoin block index is the first appearance of the accumulator checksum that was used in the spend
// note that this also means when staking that this checksum should be from a block that is beyond 60 minutes old and
// 100 blocks deep.
const CBlockIndex* ZerocoinStake::GetIndexFrom(const CBlockIndex* pindexPrev)
{
    if (pindexFrom)
        return pindexFrom;

    int nHeightChecksum = 0;

    if (fMint)
        nHeightChecksum = GetChecksumHeightFromMint();
    else
        nHeightChecksum = GetChecksumHeightFromSpend();

    if (nHeightChecksum > chainActive.Height()) {
        pindexFrom = nullptr;
    } else {
        //note that this will be a nullptr if the height DNE
        pindexFrom = chainActive[nHeightChecksum];
    }

    return pindexFrom;
}

CAmount ZerocoinStake::GetValue()
{
    return denom * COIN;
}

CAmount ZerocoinStake::GetWeight()
{
    if (denom == libzerocoin::CoinDenomination::ZQ_ONE_HUNDRED) {
        //10% reduction
        return (denom * COIN * 9) / 10;
    } else if (denom == libzerocoin::CoinDenomination::ZQ_ONE_THOUSAND) {
        //20% reduction
        return (denom * COIN * 8) / 10;
    } else if (denom == libzerocoin::CoinDenomination::ZQ_TEN_THOUSAND) {
        //30% reduction
        return (denom * COIN * 7) / 10;
    }
    return denom * COIN;
}

int ZerocoinStake::HeightToModifierHeight(int nHeight)
{
    //Nearest multiple of KernelModulus that is over KernelModulus bocks deep in the chain
    return (nHeight - Params().KernelModulus()) - (nHeight % Params().KernelModulus()) ;
}

//Use the first accumulator checkpoint that occurs 60 minutes after the block being staked from
bool ZerocoinStake::GetModifier(uint64_t& nStakeModifier, const CBlockIndex* pindexChainPrev)
{
    const CBlockIndex* pindex = GetIndexFrom(pindexChainPrev);

    if (!pindex || !pindexChainPrev) {
        return false;
    }

    if (pindexChainPrev->nHeight >= Params().HeightLightZerocoin()) {
        return GetStakeModifier(nStakeModifier, *pindexChainPrev);
    }

    int nNearest100Block = ZerocoinStake::HeightToModifierHeight(pindex->nHeight);

    //Rare case block index < 100, we don't use proof of stake for these blocks
    if (nNearest100Block < 1) {
        nStakeModifier = 1;
        return false;
    }

    while (nNearest100Block != pindex->nHeight) {
        pindex = pindex->pprev;
    }

    nStakeModifier = UintToArith256(pindex->mapAccumulatorHashes.at(denom)).GetLow64();
    return true;
}

CDataStream ZerocoinStake::GetUniqueness()
{
    //The unique identifier for a Zerocoin VEIL is a hash of the serial
    CDataStream ss(SER_GETHASH, 0);
    ss << hashSerial;
    return ss;
}

bool ZerocoinStake::CreateTxIn(CWallet* pwallet, CTxIn& txIn, uint256 hashTxOut)
{
#ifdef ENABLE_WALLET
    if (gArgs.GetBoolArg("-disablewallet", DEFAULT_DISABLE_WALLET)) {
#endif
        return error("%s: wallet disabled", __func__);
#ifdef ENABLE_WALLET
    }

    const CBlockIndex* pindexCheckpoint = GetIndexFrom(nullptr);
    if (!pindexCheckpoint)
        return error("%s: failed to find checkpoint block index", __func__);

    CZerocoinMint mint;
    if (!pwallet->GetMintFromStakeHash(hashSerial, mint))
        return error("%s: failed to fetch mint associated with serial hash %s", __func__, hashSerial.GetHex());

    if (libzerocoin::ExtractVersionFromSerial(mint.GetSerialNumber()) < 2)
        return error("%s: serial extract is less than v2", __func__);

    int nSecurityLevel = 100;
    CZerocoinSpendReceipt receipt;
    if (!pwallet->MintToTxIn(mint, nSecurityLevel, hashTxOut, txIn, receipt, libzerocoin::SpendType::STAKE, pindexCheckpoint))
        return error("%s\n", receipt.GetStatusMessage());

    return true;
#endif
}

bool ZerocoinStake::CreateTxOuts(CWallet* pwallet, std::vector<CTxOutBaseRef>& vpout, CAmount nTotal)
{
#ifdef ENABLE_WALLET
    if (gArgs.GetBoolArg("-disablewallet", DEFAULT_DISABLE_WALLET)) {
#endif
        return error("%s: wallet disabled", __func__);
#ifdef ENABLE_WALLET
    }

    //Create an output returning the Zerocoin VEIL that was staked
    CTxOut outReward;
    libzerocoin::CoinDenomination denomStaked = libzerocoin::AmountToZerocoinDenomination(this->GetValue());
    CDeterministicMint dMint;
    if (!pwallet->CreateZOutPut(denomStaked, outReward, dMint))
        return error("%s: failed to create zerocoin output", __func__);
    vpout.emplace_back(outReward.GetSharedPtr());

    //Add new staked denom to our wallet
    if (!pwallet->DatabaseMint(dMint))
        return error("%s: failed to database the staked Zerocoin", __func__);

    CAmount nRewardOut = 0;
    while (nRewardOut < nTotal) {
        CTxOut out;
        CDeterministicMint dMintReward;
        auto denomReward = libzerocoin::CoinDenomination::ZQ_TEN;
        if (!pwallet->CreateZOutPut(denomReward, out, dMintReward))
            return error("%s: failed to create Zerocoin output", __func__);
        vpout.emplace_back(out.GetSharedPtr());

        if (!pwallet->DatabaseMint(dMintReward))
            return error("%s: failed to database mint reward", __func__);
        nRewardOut += libzerocoin::ZerocoinDenominationToAmount(denomReward);
    }

    return true;
#endif
}

bool ZerocoinStake::CompleteTx(CWallet* pwallet, CMutableTransaction& txNew)
{
    if (!MarkSpent(pwallet, txNew.GetHash()))
        return error("%s: failed to mark mint as used\n", __func__);
    return true;
}

bool ZerocoinStake::GetTxFrom(CTransaction& tx)
{
    return false;
}

bool ZerocoinStake::MarkSpent(CWallet *pwallet, const uint256& txid)
{
#ifdef ENABLE_WALLET
    if (gArgs.GetBoolArg("-disablewallet", DEFAULT_DISABLE_WALLET)) {
#endif
        return error("%s: wallet disabled", __func__);
#ifdef ENABLE_WALLET
    }

    CzTracker* zTracker = pwallet->GetZTrackerPointer();
    CMintMeta meta;
    if (!zTracker->GetMetaFromStakeHash(hashSerial, meta))
        return error("%s: tracker does not have serialhash", __func__);

    zTracker->SetPubcoinUsed(meta.hashPubcoin, txid);
    return true;
#endif
}

bool ZerocoinStake::CreateCoinStake(CWallet* pwallet, const CAmount& nBlockReward, CMutableTransaction& txCoinStake, bool& retryable, CMutableTransaction& txCoinbase) {
#ifdef ENABLE_WALLET
    if (gArgs.GetBoolArg("-disablewallet", DEFAULT_DISABLE_WALLET)) {
#endif
        return error("%s: wallet disabled", __func__);
#ifdef ENABLE_WALLET
    }
    if (!CreateTxOuts(pwallet, txCoinStake.vpout, nBlockReward)) {
        LogPrintf("%s : failed to get scriptPubKey\n", __func__);
        retryable = true;
        return false;
    }

    // Limit size
    unsigned int nBytes = ::GetSerializeSize(txCoinStake, SER_NETWORK, PROTOCOL_VERSION | SERIALIZE_TRANSACTION_NO_WITNESS) * WITNESS_SCALE_FACTOR;

    if (nBytes >= MAX_BLOCK_WEIGHT / 5) {
        retryable = false;
        return error("CreateCoinStake : exceeded coinstake size limit");
    }

    uint256 hashTxOut = txCoinStake.GetOutputsHash();
    txCoinStake.vin.emplace_back();
    {
        if (!CreateTxIn(pwallet, txCoinStake.vin[0], hashTxOut)) {
            LogPrintf("%s : failed to create TxIn\n", __func__);
            txCoinStake.vin.clear();
            txCoinStake.vpout.clear();
            retryable = true;
            return false;
        }
    }

    //Mark input as spent
    if (!CompleteTx(pwallet, txCoinStake)) {
        retryable = false;
        return false;
    }
    return true;
#endif
}

/**
 * @brief Get the lowest possible value of the inputs used in this RingCT transaction.
 * @param[out] nValue: The returned minimum value of the RingCT output [usually 2].
 * @return <b>true</b> upon success.
 *         <b>false</b> if the output type is not OUTPUT_RINGCT or if GetRangeProofInfo() fails.
 */
bool PublicRingCTStake::GetMinimumInputValue(CAmount& nValue) const
{
    int nExp = 0;
    int nMantissa = 0;
    CAmount nMinValue = 0;
    CAmount nMaxValue = 0;
    CTxOutRingCT* txout = nullptr;
    for (auto& pout : m_ptx->vpout) {
        if (pout->GetType() != OUTPUT_RINGCT)
            continue;
        txout = (CTxOutRingCT*)pout.get();
        break;
    }

    if (!txout)
        return error("%s: PublicRingCTStake has no RingCT outputs.", __func__);

    if (!GetRangeProofInfo(txout->vRangeproof, nExp, nMantissa, nMinValue, nMaxValue))
        return error("%s: Failed to get range proof info.", __func__);

    nValue = nMinValue;
    return true;
}

const CBlockIndex* PublicRingCTStake::GetIndexFrom(const CBlockIndex* pindexPrev)
{
    if (pindexFrom)
        return pindexFrom;

    pindexFrom = pindexPrev->GetAncestor(pindexPrev->nHeight - Params().RingCT_RequiredStakeDepth());

    return pindexFrom;
}

/**
 * @brief Get the theoretical value of the RingCT stake.
 * @return CAmount: the minimum value of the PublicRingCTStake. Returns 0 on fail.
 * @see GetMinimumInputValue()
 */
CAmount PublicRingCTStake::GetValue()
{
    CAmount nValue = 0;
    if (!GetMinimumInputValue(nValue))
        return 0;

    return nValue;
}

/**
 * @brief Gets the relative weight of the RingCT stake, based on its input.
 * @return CAmount: the weighted value of the PublicRingCTStake. Returns 0 on fail.
 */
CAmount PublicRingCTStake::GetWeight() {
    return GetRingCTWeightForValue(GetValue());
}

bool PublicRingCTStake::GetModifier(uint64_t& nStakeModifier, const CBlockIndex* pindexChainPrev)
{
    if (!pindexChainPrev)
        return false;

    return GetStakeModifier(nStakeModifier, *pindexChainPrev);
}

/**
 * @brief Get the inputs used for the RingCT transaction. This includes all inputs, including decoy inputs.
 * @return std::vector<COutPoint>: A vector of the outpoints that are inputs in the transaction.
 */
std::vector<COutPoint> PublicRingCTStake::GetTxInputs() const
{
    return GetRingCtInputs(m_ptx->vin[0]);
}

/**
 * @brief Get a hash of the the key image for output 0.
 * @param[out] hashPubKey: resulting hash of the key image
 * @return bool: true upon success. false if there is a casting error when attempting to extract the key.
 */
bool PublicRingCTStake::GetPubkeyHash(uint256& hashPubKey) const
{
    //Extract the pubkeyhash from the keyimage
    try {
        const CTxIn &txin = m_ptx->vin[0];
        const std::vector<uint8_t> vKeyImages = txin.scriptData.stack[0];
        uint32_t nInputs, nRingSize;
        txin.GetAnonInfo(nInputs, nRingSize);
        const CCmpPubKey &ki = *((CCmpPubKey *) &vKeyImages[0]);
        hashPubKey = ki.GetHash();
    } catch (...) {
        return error("%s: Deserialization of compressed pubkey failed.", __func__);
    }

    return true;
}

/**
 * @brief Get the deterministic uniqueness of the RingCT output that is spent in this transaction.
 * The uniqueness of a RingCT stake is a hash of the key image.
 *
 * @return CDataStream : uniqueness is serialized into a datastream object.
 */
CDataStream PublicRingCTStake::GetUniqueness()
{
    uint256 hashPubKey;
    GetPubkeyHash(hashPubKey);
    CDataStream ss(0,0);
    ss << hashPubKey;
    return ss;
}
