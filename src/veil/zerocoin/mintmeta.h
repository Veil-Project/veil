// Copyright (c) 2017-2018 The PIVX developers
// Copyright (c) 2019 The Veil developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef VEIL_MINTMETA_H
#define VEIL_MINTMETA_H

#include <uint256.h>
#include <libzerocoin/Denominations.h>

enum MintMemoryFlags
{
    MINT_MATURE = (1 << 0),
    MINT_CONFIRMED = (1 << 1),
    MINT_PENDINGSPEND = (1 << 2)
};

//struct that is safe to store essential mint data, without holding any information that allows for actual spending (serial, randomness, private key)
struct CMintMeta
{
    int nHeight;
    uint256 hashSerial;
    uint256 hashPubcoin;
    uint256 hashStake; //requires different hashing method than hashSerial above
    uint8_t nVersion;
    libzerocoin::CoinDenomination denom;
    uint256 txid;
    bool isUsed;
    bool isArchived;
    bool isDeterministic;

    uint8_t nMemFlags; //non-permanent flags that carry MintMemoryFlags

    bool operator <(const CMintMeta& a) const;
};

#endif //VEIL_MINTMETA_H
