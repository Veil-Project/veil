// Copyright (c) 2017-2019 The PIVX developers
// Copyright (c) 2019 The Veil developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <tinyformat.h>
#include <consensus/consensus.h>
#include "veil/zerocoin/accumulators.h"
#include "veil/zerocoin/mintmeta.h"
#include "chain.h"
#include "chainparams.h"
#include "wallet/deterministicmint.h"
#include "validation.h"
#include "zerocoinstakeinput.h"
#include "veil/proofofstake/kernel.h"
#ifdef ENABLE_WALLET
#include "wallet/wallet.h"
#endif

typedef std::vector<unsigned char> valtype;

ZerocoinStake::ZerocoinStake(const libzerocoin::CoinSpend& spend)
{
    this->nChecksum = spend.getAccumulatorChecksum();
    this->denom = spend.getDenomination();
    uint256 nSerial = spend.getCoinSerialNumber().getuint256();
    this->hashSerial = Hash(nSerial.begin(), nSerial.end());
    this->pindexFrom = nullptr;
    fMint = false;
    m_type = STAKE_ZEROCOIN;
}

int ZerocoinStake::GetChecksumHeightFromMint()
{
    int nNewBlockHeight = chainActive.Height() + 1;
    int nHeightChecksum = 0;
    if (nNewBlockHeight >= Params().HeightLightZerocoin()) {
        nHeightChecksum = nNewBlockHeight - Params().Zerocoin_RequiredStakeDepthV2();
    } else {
        nHeightChecksum = chainActive.Height() + 1 - Params().Zerocoin_RequiredStakeDepth();
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
CBlockIndex* ZerocoinStake::GetIndexFrom()
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

CDataStream ZerocoinStake::GetUniqueness()
{
    //The unique identifier for a Zerocoin VEIL is a hash of the serial
    CDataStream ss(SER_GETHASH, 0);
    ss << hashSerial;
    return ss;
}

bool ZerocoinStake::CreateCoinStake(CWallet* pwallet, const CAmount& nBlockReward, CMutableTransaction& txCoinStake)
{
    // Create the output transaction(s)
    vector<CTxOut> vout;
    if (!CreateTxOuts(pwallet, vout, nBlockReward))
        return error("%s : failed to get scriptPubKey\n", __func__);

    CScript scriptEmpty;
    scriptEmpty.clear();
    txCoinStake.vpout.clear();
    txCoinStake.vpout.emplace_back(CTxOut(0, scriptEmpty).GetSharedPtr());
    for (auto& txOut : vout)
        txCoinStake.vpout.emplace_back(txOut.GetSharedPtr());

    // Limit size
    unsigned int nBytes = ::GetSerializeSize(txCoinStake, SER_NETWORK, PROTOCOL_VERSION | SERIALIZE_TRANSACTION_NO_WITNESS) * WITNESS_SCALE_FACTOR;

    if (nBytes >= MAX_BLOCK_WEIGHT / 5)
        return error("CreateCoinStake : exceeded coinstake size limit");

    uint256 hashTxOut = txCoinStake.GetOutputsHash();
    CTxIn in;
    {
        if (!CreateTxIn(pwallet, in, hashTxOut)) {
            txCoinStake = CMutableTransaction();
            return error("%s : failed to create TxIn\n", __func__);
        }
    }
    txCoinStake.vin.emplace_back(in);

    //Mark mints as spent
    if (MarkSpent(pwallet, txCoinStake.GetHash()))
        return error("%s: failed to mark mint as used\n", __func__);

    return true;
}

bool ZerocoinStake::CreateTxIn(CWallet* pwallet, CTxIn& txIn, uint256 hashTxOut)
{
#ifdef ENABLE_WALLET
    if (gArgs.GetBoolArg("-disablewallet", DEFAULT_DISABLE_WALLET)) {
#endif
        return error("%s: wallet disabled", __func__);
#ifdef ENABLE_WALLET
    }

    CBlockIndex* pindexCheckpoint = GetIndexFrom();
    if (!pindexCheckpoint)
        return error("%s: failed to find checkpoint block index", __func__);

    CZerocoinMint mint;
    if (!pwallet->GetMintFromStakeHash(hashSerial, mint))
        return error("%s: failed to fetch mint associated with serial hash %s", __func__, hashSerial.GetHex());

    if (libzerocoin::ExtractVersionFromSerial(mint.GetSerialNumber()) < 2)
        return error("%s: serial extract is less than v2", __func__);

    int nSecurityLevel = 100;
    CZerocoinSpendReceipt receipt;
    if (!pwallet->MintToTxIn(mint, nSecurityLevel, hashTxOut, txIn, receipt, libzerocoin::SpendType::STAKE, GetIndexFrom()))
        return error("%s\n", receipt.GetStatusMessage());

    return true;
#endif
}

bool ZerocoinStake::CreateTxOuts(CWallet* pwallet, vector<CTxOut>& vout, CAmount nTotal)
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
    vout.emplace_back(outReward);

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
        vout.emplace_back(out);

        if (!pwallet->DatabaseMint(dMintReward))
            return error("%s: failed to database mint reward", __func__);
        nRewardOut += libzerocoin::ZerocoinDenominationToAmount(denomReward);
    }

    return true;
#endif
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

uint256 ZerocoinStake::GetSerialStakeHash()
{
    return hashSerial;
}
