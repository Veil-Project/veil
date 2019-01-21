// Copyright (c) 2011-2019 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_WALLET_COINCONTROL_H
#define BITCOIN_WALLET_COINCONTROL_H

#include <policy/feerate.h>
#include <policy/fees.h>
#include <primitives/transaction.h>
#include <wallet/wallet.h>

#include <boost/optional.hpp>

class CInputData
{
public:
    CAmount nValue;
    uint256 blind;
    CScriptWitness scriptWitness;
};

/** Coin Control Features. */
class CCoinControl
{
public:
    CScript scriptChange;
    //! Custom change destination, if not set an address is generated
    CTxDestination destChange;
    //! Override the default change type if set, ignored if destChange is set
    boost::optional<OutputType> m_change_type;
    //! If false, allows unselected inputs, but requires all selected inputs be used
    bool fAllowOtherInputs;
    //! Includes watch only addresses which are solvable
    bool fAllowWatchOnly;
    //! Override automatic min/max checks on fee, m_feerate must be set if true
    bool fOverrideFeeRate;
    //! Override the wallet's m_pay_tx_fee if set
    boost::optional<CFeeRate> m_feerate;
    //! Override the default confirmation target if set
    boost::optional<unsigned int> m_confirm_target;
    //! Override the wallet's m_signal_rbf if set
    boost::optional<bool> m_signal_bip125_rbf;
    //! Avoid partial use of funds sent to a given address
    bool m_avoid_partial_spends;
    //! Fee estimation mode to control arguments to estimateSmartFee
    FeeEstimateMode m_fee_mode;

    int nCoinType;
    mutable bool fHaveAnonOutputs = false;
    mutable bool fNeedHardwareKey = false;
    CAmount m_extrafee;
    std::map<COutPoint, CInputData> m_inputData;
    bool fAllowLocked = false;
    mutable int nChangePos = -1;
    bool m_addChangeOutput = true;

    CCoinControl()
    {
        SetNull();
    }

    void SetNull();

    bool HasSelected() const
    {
        return (setSelected.size() > 0);
    }

    bool IsSelected(const COutPoint& output) const
    {
        return (setSelected.count(output) > 0);
    }

    void Select(const COutPoint& output, CAmount nValue)
    {
        setSelected.insert(output);
        CInputData inputData;
        inputData.nValue = nValue;
        m_inputData.emplace(output, inputData);
    }

    void UnSelect(const COutPoint& output)
    {
        setSelected.erase(output);
        m_inputData.erase(output);
    }

    void UnSelectAll()
    {
        setSelected.clear();
        m_inputData.clear();
    }

    void ListSelected(std::vector<COutPoint>& vOutpoints) const
    {
        vOutpoints.assign(setSelected.begin(), setSelected.end());
    }

    CAmount GetValueSelected() const
    {
        CAmount nValue = 0;
        for (const auto& p : m_inputData)
            nValue += p.second.nValue;
        return nValue;
    }

    size_t NumSelected()
    {
        return setSelected.size();
    }

//private:
    std::set<COutPoint> setSelected;
};

#endif // BITCOIN_WALLET_COINCONTROL_H
