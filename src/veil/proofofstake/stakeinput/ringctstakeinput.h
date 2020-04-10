// Copyright (c) 2019 The Veil developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "stakeinput.h"

/**
 * @brief An input that is used by the owner of the rct input to do the initial stake mining.
 * @note The RingCtStakeCandidate contains private information that is only known to owner of the RingCt output being staked.
 * This object of this class should never be communicated to peers. Instead it must use CreateTxIn() to put it into the proper
 * format to be used in a coinstake transaction.
 */
class RingCtStakeCandidate : public CStakeInput
{
private:
    //! The CTransactionRef that the RingCt output is from.
    CTransactionRef m_ptx;

    //! The COutputRecord that is associated with the RingCt output.
    const COutputRecord* m_pout;

    //! The COutPoint that locates the RingCt output within m_ptx::vpout
    COutPoint m_outpoint;

    //! A hash of the key image of the RingCt output. This is used for the CStakeInput's uniqueness.
    uint256 m_hashPubKey;

    //! The <i>actual</i> value of the RingCt output.
    //! @note Other areas that are seen by the public will only be able to see the minimum possible value of this field (@see PublicRingCtStake::GetMinimumInputValue())
    CAmount m_nAmount;

public:
    RingCtStakeCandidate(CWallet* pwallet, CTransactionRef ptx, const COutPoint& outpoint, const COutputRecord* pout);

    bool IsZerocoins() override { return false; }
    CBlockIndex* GetIndexFrom() override;
    bool CreateCoinStake(CWallet* pwallet, const CAmount& nBlockReward, CMutableTransaction& txCoinStake) override;
    CAmount GetValue() override { return m_nAmount; }
    // XXX - TODO: Why is CreateTxOuts commented out?
    bool CreateTxOuts(CWallet* pwallet, std::vector<CTxOut>& vout, CAmount nTotal) override { return false; }
    CDataStream GetUniqueness() override;
};

/**
 * @brief A RingCt output that has been published to the blockchain in a coinstake transaction.
 * @note The PublicRingCtStake contains no private information about the RingCt output. The data that a PublicRingCtStake
 * reveals that would not have been revealed in a typical RingCt transaction is a narrowed range of values in the rangeproof.
 * An object of this class is safe to communicate to peers, but should only exist inside of a block and not as a loose transaction.
 */
class PublicRingCtStake : public CStakeInput
{
private:
    //! The CTransactionRef that is the coinstake transaction containing this CStakeInput
    CTransactionRef m_ptx;

public:
    explicit PublicRingCtStake(const CTransactionRef& txStake);
    bool IsZerocoins() override { return false; }

    CBlockIndex* GetIndexFrom() override { return nullptr; }
    bool CreateTxIn(CWallet* pwallet, CTxIn& txIn, uint256 hashTxOut = uint256()) { return false; }
    bool GetTxFrom(CTransaction& tx) { return false; }
    CAmount GetValue() override;
    bool CreateTxOuts(CWallet* pwallet, std::vector<CTxOut>& vout, CAmount nTotal) override { return false; }
    CDataStream GetUniqueness() override;
    bool CreateCoinStake(CWallet* pwallet, const CAmount& nBlockReward, CMutableTransaction& txCoinStake) override;

    // PublicRingCt specific items
    std::vector<COutPoint> GetTxInputs() const;
    bool GetMinimumInputValue(CAmount& nValue) const;
    bool GetPubkeyHash(uint256& hashPubKey) const;
};
