#ifndef VEIL_ZEROCOINDB_H
#define VEIL_ZEROCOINDB_H

#include "dbwrapper.h"
#include "uint256.h"
#include "libzerocoin/bignum.h"

class CZerocoinDB : public CDBWrapper
{
public:
    CZerocoinDB(size_t nCacheSize, bool fMemory = false, bool fWipe = false, bool obfuscate = false) :
                                            CDBWrapper(GetDefaultDataDir() / "zerocoin", nCacheSize, fMemory, fWipe) {}

    bool WriteSerialSpend(const uint256& hashSerial, const uint256& txid);
    bool ReadSerialSpend(const uint256& hashSerial, uint256& txid);
    bool EraseSerialSpend(const uint256& hashSerial);

    bool WriteAccumulatorChecksum(const uint256& nChecksum, const CBigNum& bnValue);
    bool ReadAccumulatorChecksum(const uint256& nChecksum, CBigNum& bnValue);
    bool EraseAccumulatorChecksum(const uint256& nChecksum);

    bool WriteMint(const uint256& hashPubcoin, const CBigNum& bnPubcoin);
    bool ReadMint(const uint256& hashPubcoin, CBigNum& bnPubcoin);
    bool EraseMint(const uint256& hashPubcoin);

private:
    CZerocoinDB(const CZerocoinDB&);
    void operator=(const CZerocoinDB&);
};


#endif //VEIL_ZEROCOINDB_H
