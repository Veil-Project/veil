// Copyright (c) 2018 The Veil Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "outputrecord.h"
#include <boost/variant.hpp>
#include <tinyformat.h>

void COutputRecord::AddStealthAddress(const CKeyID& idStealth)
{
    vPath.resize(21);
    vPath[0] = ORA_STEALTH;
    memcpy(&vPath[1], &idStealth, idStealth.size());
}

bool COutputRecord::GetStealthID(CKeyID& idStealth) const
{
    if (!IsStealth())
        return false;
    if (vPath.size() < 21)
        return false;
    memcpy(idStealth.begin(), vPath.data()+1, 20);
    return true;
}

bool COutputRecord::IsReceive() const
{
    if ((nFlags & ORF_OWNED) && (nFlags & ORF_FROM))
        return true;
    return nFlags & ORF_OWN_ANY;
}

bool COutputRecord::IsChange() const
{
    return nFlags & ORF_CHANGE;
}

bool COutputRecord::IsSend() const
{
    if (IsReceive())
        return false;
    return nFlags & ORF_FROM;
}

bool COutputRecord::IsBasecoin() const
{
    return nType == OUTPUT_STANDARD;
}

CAmount COutputRecord::GetAmount() const
{
    return nValue * ((nFlags & ORF_OWN_ANY) ? 1 : -1);
}

bool COutputRecord::GetDestination(CTxDestination& dest) const
{
    if (!scriptPubKey.empty()) {
        //if (!ExtractDestination(scriptPubKey, dest))
        //    return false;
    } else if (IsStealth()) {
        CKeyID keyID;
        //if (!GetStealthID(keyID))
         //   return false;
       // dest.swap(CTxDestination(keyID));
    }

    return true;
}

std::string COutputRecord::ToString() const
{
    return strprintf("TransactionRecord:\n  n=%d\n  nValue=%d\n  nType=%d\n  IsSend()=%d\n", n, nValue, nType, IsSend());
}