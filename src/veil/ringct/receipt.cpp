// Copyright (c) 2017-2019 The PIVX developers
// Copyright (c) 2019-2021 The Veil developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <veil/ringct/receipt.h>
#include <veil/ringct/transactionrecord.h>


void CMultiTxReceipt::SetStatus(std::string strStatus, int nStatus)
{
    strStatusMessage = strStatus;
    this->nStatus = nStatus;
}

CTransactionRecord CMultiTxReceipt::GetTransactionRecord(const int n) const
{
    if (!mapRecords.count(n))
        return CTransactionRecord();
    return mapRecords.at(n);
}

void CMultiTxReceipt::AddTransaction(CTransactionRef& txRef, const CTransactionRecord& rtx)
{
    auto n = vtx.size();
    mapRecords.emplace(n, rtx);
    vtx.emplace_back(txRef);
}

std::string CMultiTxReceipt::GetStatusMessage()
{
    return strStatusMessage;
}
