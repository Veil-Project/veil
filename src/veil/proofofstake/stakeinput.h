// Copyright (c) 2017-2019 The PIVX developers
// Copyright (c) 2019-2022 The Veil developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef PIVX_STAKEINPUT_H
#define PIVX_STAKEINPUT_H

#include "chain.h"
#include "streams.h"
#include "veil/ringct/transactionrecord.h"
#include "veil/zerocoin/accumulatormap.h"

#include "libzerocoin/CoinSpend.h"

class AnonWallet;
class CKeyStore;
class CWallet;
class CWalletTx;

enum StakeInputType {
    STAKE_ZEROCOIN,
    STAKE_RINGCT,
};

class CStakeInput
{
protected:
    const CBlockIndex* pindexFrom = nullptr;
    libzerocoin::CoinDenomination denom = libzerocoin::CoinDenomination::ZQ_ERROR;
    StakeInputType nType = STAKE_RINGCT;

public:
    virtual ~CStakeInput(){};
    virtual const CBlockIndex* GetIndexFrom(const CBlockIndex* pindexPrev) = 0;
    virtual bool GetTxFrom(CTransaction& tx) = 0;
    virtual CAmount GetValue() = 0;
    virtual CAmount GetWeight() = 0;
    virtual bool GetModifier(uint64_t& nStakeModifier, const CBlockIndex* pindexChainPrev) = 0;
    virtual bool IsZerocoins() = 0;
    virtual CDataStream GetUniqueness() = 0;
    libzerocoin::CoinDenomination GetDenomination() { return denom; };
    StakeInputType GetType() const { return nType; }

    virtual bool CreateCoinStake(CWallet* pwallet, const CAmount& nBlockReward, CMutableTransaction& txCoinStake, bool& retryable, CMutableTransaction& txCoinbase) = 0;
    virtual void OnStakeFound(const arith_uint256& bnTarget, const uint256& hashProofOfStake) {}
};


class RingCTStake : public CStakeInput
{
private:
    static const int RING_SIZE = 11;

    // Contains: depth (coin.nDepth), txo idx (coin.i), output record (coin.rtx->second)
    // A copy is required for lifetime.
    const COutputR coin;
    CAmount nWeight = 0;

    CTransactionRecord rtx;

    // A hash of the key image of the RingCt output. This is used for the CStakeInput's uniqueness.
    uint256 hashPubKey;

    CAmount GetBracketMinValue();

public:
    explicit RingCTStake(const COutputR& coin_, uint256 hashPubKey_)
        : coin(coin_), hashPubKey(hashPubKey_) {}

    const CBlockIndex* GetIndexFrom(const CBlockIndex* pindexPrev) override;
    bool GetTxFrom(CTransaction& tx) override;
    CAmount GetValue() override;
    CAmount GetWeight() override;
    bool GetModifier(uint64_t& nStakeModifier, const CBlockIndex* pindexChainPrev) override;
    CDataStream GetUniqueness() override;

    bool IsZerocoins() override { return false; }

    bool MarkSpent(AnonWallet* panonwallet, CMutableTransaction& txNew);

    void OnStakeFound(const arith_uint256& bnTarget, const uint256& hashProofOfStake) override;
    bool CreateCoinStake(CWallet* pwallet, const CAmount& nBlockReward, CMutableTransaction& txCoinStake, bool& retryable, CMutableTransaction& txCoinbase) override;
};


// ZerocoinStake can take two forms
// 1) the stake candidate, which is a zcmint that is attempted to be staked
// 2) a staked zerocoin, which is a zcspend that has successfully staked
class ZerocoinStake : public CStakeInput
{
private:
    uint256 nChecksum;
    bool fMint;
    uint256 hashSerial;

public:
    explicit ZerocoinStake(libzerocoin::CoinDenomination denom, const uint256& hashSerial)
    {
        nType = STAKE_ZEROCOIN;
        this->denom = denom;
        this->hashSerial = hashSerial;
        this->pindexFrom = nullptr;
        fMint = true;
    }

    explicit ZerocoinStake(const libzerocoin::CoinSpend& spend);

    const CBlockIndex* GetIndexFrom(const CBlockIndex* pindexPrev) override;
    bool GetTxFrom(CTransaction& tx) override;
    CAmount GetValue() override;
    CAmount GetWeight() override;
    bool GetModifier(uint64_t& nStakeModifier, const CBlockIndex* pindexChainPrev) override;
    CDataStream GetUniqueness() override;
    bool CreateTxIn(CWallet* pwallet, CTxIn& txIn, uint256 hashTxOut = uint256());
    bool CreateTxOuts(CWallet* pwallet, std::vector<CTxOutBaseRef>& vpout, CAmount nTotal);
    bool CompleteTx(CWallet* pwallet, CMutableTransaction& txNew);
    bool CreateCoinStake(CWallet* pwallet, const CAmount& nBlockReward, CMutableTransaction& txCoinStake, bool& retryable, CMutableTransaction& txCoinbase) override;
    bool IsZerocoins() override { return true; }

    bool MarkSpent(CWallet* pwallet, const uint256& txid);
    int GetChecksumHeightFromMint();
    int GetChecksumHeightFromSpend();
    uint256 GetChecksum();

    static int HeightToModifierHeight(int nHeight);
};


/**
 * @brief A RingCt output that has been published to the blockchain in a coinstake transaction.
 * @note The PublicRingCTStake contains no private information about the RingCt output. The data that a PublicRingCTStake
 * reveals that would not have been revealed in a typical RingCt transaction is a narrowed range of values in the rangeproof.
 * An object of this class is safe to communicate to peers, but should only exist inside of a block and not as a loose transaction.
 */
class PublicRingCTStake : public CStakeInput
{
private:
    //! The CTransactionRef that is the coinstake transaction containing this CStakeInput
    CTransactionRef m_ptx;

public:
    explicit PublicRingCTStake(const CTransactionRef& txStake) : m_ptx(txStake) {}

    // None of these are implemented, intentionally.
    bool GetTxFrom(CTransaction& tx) override { return false; }
    bool CreateCoinStake(CWallet* pwallet, const CAmount& nBlockReward, CMutableTransaction& txCoinStake, bool& retryable, CMutableTransaction& txCoinbase) override { return false; }

    // Most of these are similar to RingCTStake's. TODO: maybe we could have
    // a base ringctstake class instead of duplicating.
    bool IsZerocoins() override { return false; }
    const CBlockIndex* GetIndexFrom(const CBlockIndex* pindexPrev) override;
    CAmount GetValue() override;
    CAmount GetWeight() override;
    bool GetModifier(uint64_t& nStakeModifier, const CBlockIndex* pindexChainPrev) override;

    // Uses the transaction embedded
    CDataStream GetUniqueness() override;

    // PublicRingCt specific items
    std::vector<COutPoint> GetTxInputs() const;

private:
    bool GetMinimumInputValue(CAmount& nValue) const;
    bool GetPubkeyHash(uint256& hashPubKey) const;
};

#endif // PIVX_STAKEINPUT_H
