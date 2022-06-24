// Copyright (c) 2017-2019 The PIVX developers
// Copyright (c) 2019-2021 The Veil developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef VEIL_SPENDRECEIPT_H
#define VEIL_SPENDRECEIPT_H

#include <primitives/zerocoin.h>
#include <veil/ringct/receipt.h>
#include <veil/ringct/temprecipient.h>
#include <veil/ringct/transactionrecord.h>

class CZerocoinSpendReceipt : public CMultiTxReceipt
{
private:
    int nNeededSpends;
    std::map<int, std::vector<CZerocoinSpend>> mapSpends; // key:tx's spot in vtx

public:
    void AddSpend(const CZerocoinSpend& spend);
    std::vector<CZerocoinSpend> GetSpends(int n);
    std::vector<CZerocoinSpend> GetSpends_back();
    void SetStatus(std::string strStatus, int nStatus, int nNeededSpends = 0);
};

#endif //VEIL_SPENDRECEIPT_H
