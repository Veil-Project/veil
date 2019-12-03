// Copyright (c) 2017-2019 The PIVX developers
// Copyright (c) 2019 The Veil developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef VEIL_ZEROCOINSTAKEINPUT_H
#define VEIL_ZEROCOINSTAKEINPUT_H

#include "stakeinput.h"
#include <veil/zerocoin/accumulatormap.h>
#include <libzerocoin/CoinSpend.h>

// zPIVStake can take two forms
// 1) the stake candidate, which is a zcmint that is attempted to be staked
// 2) a staked zerocoin, which is a zcspend that has successfully staked
class ZerocoinStake : public StakeInput
{
private:
    uint256 nChecksum;
    bool fMint;
    uint256 hashSerial;

public:
    explicit ZerocoinStake(libzerocoin::CoinDenomination denom, const uint256& hashSerial)
    {
        this->denom = denom;
        this->hashSerial = hashSerial;
        this->pindexFrom = nullptr;
        fMint = true;
        m_type = STAKE_ZEROCOIN;
    }

    explicit ZerocoinStake(const libzerocoin::CoinSpend& spend);

    CBlockIndex* GetIndexFrom() override;
    CAmount GetValue() override;
    CDataStream GetUniqueness() override;
    bool CreateCoinStake(CWallet* pwallet, const CAmount& nBlockReward, CMutableTransaction& txCoinStake) override;

    bool CreateTxIn(CWallet* pwallet, CTxIn& txIn, uint256 hashTxOut = uint256());
    bool CreateTxOuts(CWallet* pwallet, std::vector<CTxOut>& vout, CAmount nTotal);
    bool MarkSpent(CWallet* pwallet, const uint256& txid);
    bool IsZerocoins() override { return true; }
    int GetChecksumHeightFromMint();
    int GetChecksumHeightFromSpend();
    uint256 GetChecksum();
    uint256 GetSerialStakeHash();
};

#endif //VEIL_ZEROCOINSTAKEINPUT_H
