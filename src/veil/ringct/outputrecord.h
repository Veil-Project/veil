// Copyright (c) 2017-2019 The Particl Core developers
// Copyright (c) 2018-2019 Veil developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef VEIL_OUTPUTRECORD_H
#define VEIL_OUTPUTRECORD_H

#include <script/script.h>
#include <amount.h>
#include <script/standard.h>

enum OutputRecordAddressTypes
{
    ORA_EXTKEY       = 1,
    ORA_STEALTH      = 2,
    ORA_STANDARD     = 3,
};

enum OutputRecordFlags
{
    ORF_OWNED               = (1 << 0),
    ORF_FROM                = (1 << 1),
    ORF_CHANGE              = (1 << 2),
    ORF_SPENT               = (1 << 3),
    ORF_LOCKED              = (1 << 4), // Needs wallet to be unlocked for further processing
    ORF_WATCHONLY           = (1 << 6),
    ORF_PENDING_SPEND       = (1 << 7), // Don't use this output because it has been used to spend

    ORF_OWN_WATCH           = ORF_WATCHONLY,
    ORF_OWN_ANY             = ORF_OWNED | ORF_OWN_WATCH,

    ORF_BASECOIN_IN         = (1 << 13),
    ORF_BLIND_IN            = (1 << 14),
    ORF_ANON_IN             = (1 << 15),
};

class COutputRecord
{
private:
    CAmount nValue;
public:
    COutputRecord() : nValue(-1), nType(0), nFlags(0), n(0) {};
    uint8_t nType;
    uint8_t nFlags;
    uint16_t n;
    CScript scriptPubKey;
    std::string sNarration;

    /*
    vPath 0 - ORA_EXTKEY
        1 - index to m
        2... path

    vPath 0 - ORA_STEALTH
        [1, 21] stealthkeyid
        [22, 55] pubkey (if not using ephemkey)

    vPath 0 - ORA_STANDARD
        [1, 34] pubkey
    */
    std::vector<uint8_t> vPath; // index to m is stored in first entry

    void AddStealthAddress(const CKeyID& idStealth);
    bool IsStealth() const { return !vPath.empty() && vPath[0] == ORA_STEALTH; }
    bool IsBasecoin() const;
    bool GetKeyImage(CCmpPubKey& keyImage) const;
    bool GetStealthID(CKeyID& idStealth) const;
    bool IsReceive() const;
    bool IsSend() const;
    bool IsChange() const;
    CAmount GetAmount() const;
    void SetValue(const CAmount& nValue) { this->nValue = nValue; }
    CAmount GetRawValue() const { return nValue; }
    bool GetDestination(CTxDestination& dest) const;
    std::string ToString() const;
    void MarkSpent(bool isSpent);
    void MarkPendingSpend(bool isSpent);
    bool IsSpent(bool fIncludePendingSpend = true) const;

    ADD_SERIALIZE_METHODS;
    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream &s, Operation ser_action)
    {
        READWRITE(nType);
        READWRITE(nFlags);
        READWRITE(n);
        READWRITE(nValue);
        READWRITE(*(CScriptBase*)(&scriptPubKey));
        READWRITE(sNarration);
        READWRITE(vPath);
    };
};

#endif //VEIL_OUTPUTRECORD_H
