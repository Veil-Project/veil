// Copyright (c) 2017-2019 The PIVX developers
// Copyright (c) 2019 The Veil developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef VEIL_SPENDRECEIPT_H
#define VEIL_SPENDRECEIPT_H

#include <primitives/zerocoin.h>
#include <veil/ringct/temprecipient.h>
#include <veil/ringct/transactionrecord.h>

class CTransactionRecord;

class CZerocoinSpendReceipt
{
private:
    std::string strStatusMessage;
    int nStatus;
    int nNeededSpends;
    std::map<int, std::vector<CZerocoinSpend>> mapSpends; // key:tx's spot in vtx
    std::map<int, CTransactionRecord> mapRecords; // key:tx's spot in vtx
    std::vector<CTransactionRef> vtx;

public:
    void AddSpend(const CZerocoinSpend& spend);
    void AddTransaction(CTransactionRef& txRef, const CTransactionRecord& rtx);
    std::vector<CZerocoinSpend> GetSpends(int n);
    std::vector<CZerocoinSpend> GetSpends_back();
    void SetStatus(std::string strStatus, int nStatus, int nNeededSpends = 0);
    std::string GetStatusMessage();
    std::vector<CTransactionRef> GetTransactions() const { return vtx; }
    CTransactionRecord GetTransactionRecord(int n) const;
};

#endif //VEIL_SPENDRECEIPT_H
