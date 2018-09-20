#include "zerocoindb.h"

#include <string>
#include <utility>

bool CZerocoinDB::WriteSerialSpend(const uint256& hashSerial, const uint256& txid)
{
    return Write(std::make_pair(std::string("serial"), hashSerial), txid);
}

bool CZerocoinDB::ReadSerialSpend(const uint256& hashSerial, uint256& txid)
{
    return Read(std::make_pair(std::string("serial"), hashSerial), txid);
}

bool CZerocoinDB::EraseSerialSpend(const uint256& hashSerial)
{
    return Erase(std::make_pair(std::string("serial"), hashSerial));
}

bool CZerocoinDB::WriteAccumulatorChecksum(const uint256& nChecksum, const CBigNum& bnValue)
{
    return Write(std::make_pair(std::string("checksum"), nChecksum), bnValue);
}

bool CZerocoinDB::ReadAccumulatorChecksum(const uint256& nChecksum, CBigNum& bnValue)
{
    return Read(std::make_pair(std::string("checksum"), nChecksum), bnValue);
}

bool CZerocoinDB::EraseAccumulatorChecksum(const uint256& nChecksum)
{
    return Erase(std::make_pair(std::string("checksum"), nChecksum));
}

bool CZerocoinDB::WriteMint(const uint256& hashPubcoin, const CBigNum& bnPubcoin)
{
    return Write(std::make_pair(std::string("mint"), hashPubcoin), bnPubcoin);
}

bool CZerocoinDB::ReadMint(const uint256& hashPubcoin, CBigNum& bnPubcoin)
{
    return Read(std::make_pair(std::string("mint"), hashPubcoin), bnPubcoin);
}

bool CZerocoinDB::EraseMint(const uint256& hashPubcoin)
{
    return Erase(std::make_pair(std::string("mint"), hashPubcoin));
}
