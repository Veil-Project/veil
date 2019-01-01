// Copyright (c) 2017-2019 The PIVX developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <streams.h>
#include "primitives/zerocoin.h"
#include "hash.h"
#include "util.h"
#include "utilstrencodings.h"

bool CMintMeta::operator <(const CMintMeta& a) const
{
    return this->hashPubcoin < a.hashPubcoin;
}

uint256 GetSerialHash(const CBigNum& bnSerial)
{
    CDataStream ss(SER_GETHASH, 0);
    ss << bnSerial;
    return Hash(ss.begin(), ss.end());
}

uint256 GetPubCoinHash(const CBigNum& bnValue)
{
    CDataStream ss(SER_GETHASH, 0);
    ss << bnValue;
    return Hash(ss.begin(), ss.end());
}

bool CZerocoinMint::GetKeyPair(CKey &key) const
{
    if (version < STAKABLE_VERSION)
        return error("%s: version is %d", __func__, version);

    if (privkey.empty())
        return error("%s: empty privkey %s", __func__, privkey.data());

    return key.SetPrivKey(privkey, true);
}

std::string CZerocoinMint::ToString() const
{
    std::string str = strprintf("\n  ZerocoinMint:\n   version=%d   \ntxfrom=%s   \nheight=%d \n   randomness: %s   \n serial %s   \n privkey %s\n",
                                version, txid.GetHex(), nHeight, randomness.GetHex(), serialNumber.GetHex(), HexStr(privkey));
    return str;
}
