// Copyright (c) 2017-2018 The Particl Core developers
// Copyright (c) 2018 The Veil developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef VEIL_TRANSACTIONRECORD_H
#define VEIL_TRANSACTIONRECORD_H

#include <veil/ringct/outputrecord.h>

extern const uint256 ABANDON_HASH;

typedef std::map<uint8_t, std::vector<uint8_t> > mapRTxValue_t;
class CTransactionRecord
{
// Stored by uint256 txnHash;
public:
    CTransactionRecord() :
        nFlags(0), nIndex(0), nBlockTime(0) , nTimeReceived(0) , nFee(0) {};


    // Conflicted state is marked by set blockHash and nIndex -1
    uint256 blockHash;
    int16_t nFlags;
    int16_t nIndex;

    int64_t nBlockTime;
    int64_t nTimeReceived;
    CAmount nFee;
    mapRTxValue_t mapValue;

    std::vector<COutPoint> vin;
    std::vector<COutputRecord> vout;

    int InsertOutput(COutputRecord &r);
    bool EraseOutput(uint16_t n);

    COutputRecord *GetOutput(int n);
    const COutputRecord *GetOutput(int n) const;
    const COutputRecord *GetChangeOutput() const;

    void SetMerkleBranch(const uint256 &blockHash_, int posInBlock)
    {
        blockHash = blockHash_;
        nIndex = posInBlock;
    };

    bool IsAbandoned() const { return (blockHash == ABANDON_HASH); }
    bool HashUnset() const { return (blockHash.IsNull() || blockHash == ABANDON_HASH); }

    void SetAbandoned()
    {
        blockHash = ABANDON_HASH;
    };

    int64_t GetTxTime() const
    {
        if (HashUnset() || nIndex < 0)
            return nTimeReceived;
        return std::min(nTimeReceived, nBlockTime);
    };

    bool HaveChange() const
    {
        for (const auto &r : vout)
            if (r.nFlags & ORF_CHANGE)
                return true;
        return false;
    };

    CAmount TotalOutput()
    {
        CAmount nTotal = 0;
        for (const auto &r : vout)
            nTotal += r.GetAmount();
        return nTotal;
    };

    OutputTypes GetInputType() const
    {
        if (nFlags & ORF_BASECOIN_IN)
            return OutputTypes::OUTPUT_STANDARD;
        else if (nFlags & ORF_ANON_IN)
            return OutputTypes::OUTPUT_RINGCT;
        else if (nFlags & ORF_BLIND_IN)
            return OutputTypes::OUTPUT_CT;
        return OutputTypes::OUTPUT_NULL;
    }

    //mutable uint32_t nCacheFlags;
    bool InMempool() const;
    bool IsTrusted() const;

    bool IsCoinBase() const {return false;};

    ADD_SERIALIZE_METHODS;
    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream &s, Operation ser_action)
    {
        READWRITE(blockHash);
        READWRITE(nFlags);
        READWRITE(nIndex);
        READWRITE(nBlockTime);
        READWRITE(nTimeReceived);
        READWRITE(mapValue);
        READWRITE(nFee);
        READWRITE(vin);
        READWRITE(vout);
    };
};

#endif //VEIL_TRANSACTIONRECORD_H
