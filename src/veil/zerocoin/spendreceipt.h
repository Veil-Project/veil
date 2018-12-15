// Copyright (c) 2017-2018 The PIVX developers
// Copyright (c) 2018 The Veil developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef VEIL_SPENDRECEIPT_H
#define VEIL_SPENDRECEIPT_H

#include <primitives/zerocoin.h>
#include <veil/ringct/temprecipient.h>

class CZerocoinSpendReceipt
{
private:
    std::string strStatusMessage;
    int nStatus;
    int nNeededSpends;
    std::vector<CZerocoinSpend> vSpends;
    std::vector<CTempRecipient> vRecipients;

public:
    void AddSpend(const CZerocoinSpend& spend);
    void AddTempRecipient(const CTempRecipient& rec);
    std::vector<CZerocoinSpend> GetSpends();
    void SetStatus(std::string strStatus, int nStatus, int nNeededSpends = 0);
    std::string GetStatusMessage();
    int GetStatus();
    int GetNeededSpends();
   // std::vector<CTempRecipient> GetRecipients();
};

#endif //VEIL_SPENDRECEIPT_H
