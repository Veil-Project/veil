// Copyright (c) 2017-2019 The PIVX developers
// Copyright (c) 2019-2021 The Veil developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef VEIL_RECEIPT_H
#define VEIL_RECEIPT_H

#include <veil/ringct/temprecipient.h>
#include <veil/ringct/transactionrecord.h>

class CMultiTxReceipt
{
protected:
    std::string strStatusMessage;
    int nStatus;
    std::map<int, CTransactionRecord> mapRecords; // key:tx's spot in vtx
    std::vector<CTransactionRef> vtx;

public:
    void AddTransaction(CTransactionRef& txRef, const CTransactionRecord& rtx);
    void SetStatus(std::string strStatus, int nStatus);
    std::string GetStatusMessage();
    std::vector<CTransactionRef> GetTransactions() const { return vtx; }
    CTransactionRecord GetTransactionRecord(int n) const;
};

#endif //VEIL_RECEIPT_H
