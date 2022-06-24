// Copyright (c) 2017-2019 The PIVX developers
// Copyright (c) 2019-2021 The Veil developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <veil/zerocoin/spendreceipt.h>
#include <veil/ringct/receipt.h>
#include <veil/ringct/transactionrecord.h>

void CZerocoinSpendReceipt::AddSpend(const CZerocoinSpend& spend)
{
    int n = vtx.size();
    if (!mapSpends.count(n))
        mapSpends.emplace(n, std::vector<CZerocoinSpend>());
    mapSpends.at(n).emplace_back(spend);
}

std::vector<CZerocoinSpend> CZerocoinSpendReceipt::GetSpends(int n)
{
    if (!mapSpends.count(n))
        return std::vector<CZerocoinSpend>();
    return mapSpends.at(n);
}

std::vector<CZerocoinSpend> CZerocoinSpendReceipt::GetSpends_back()
{
    int n = vtx.size();
    if (!mapSpends.count(n))
        return std::vector<CZerocoinSpend>();
    return mapSpends.at(n);
}

void CZerocoinSpendReceipt::SetStatus(std::string strStatus, int nStatus, int nNeededSpends)
{
    CMultiTxReceipt::SetStatus(strStatus, nStatus);
    this->nNeededSpends = nNeededSpends;
}
