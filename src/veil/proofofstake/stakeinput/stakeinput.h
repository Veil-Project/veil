// Copyright (c) 2017-2019 The PIVX developers
// Copyright (c) 2019 The Veil developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef PIVX_STAKEINPUT_H
#define PIVX_STAKEINPUT_H

#include <chain.h>
#include <streams.h>

class CKeyStore;
class COutputRecord;
class CTransactionRecord;
class CWallet;
class CWalletTx;

enum StakeInputType
{
    STAKE_ZEROCOIN,
    STAKE_RINGCT,
    STAKE_RINGCTCANDIDATE
};

std::string StakeInputTypeToString(StakeInputType t);

class StakeInput
{
protected:
    CBlockIndex* pindexFrom;
    libzerocoin::CoinDenomination denom = libzerocoin::CoinDenomination::ZQ_ERROR;
    StakeInputType m_type;

public:
    virtual ~StakeInput(){};
    virtual CBlockIndex* GetIndexFrom() = 0;
    virtual bool CreateCoinStake(CWallet* pwallet, const CAmount& nBlockReward, CMutableTransaction& txCoinStake) = 0;
    virtual CAmount GetValue() = 0;
    virtual bool CreateTxOuts(CWallet* pwallet, std::vector<CTxOut>& vout, CAmount nTotal) = 0;
    virtual bool IsZerocoins() = 0;
    virtual CDataStream GetUniqueness() = 0;

    bool GetModifier(uint64_t& nStakeModifier, const CBlockIndex* pindexChainPrev);
    StakeInputType GetType() const { return m_type; }
};

#endif //PIVX_STAKEINPUT_H
