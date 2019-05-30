// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2019 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <primitives/transaction.h>

#include <hash.h>
#include <tinyformat.h>
#include <utilstrencodings.h>

std::string COutPoint::ToString() const
{
    return strprintf("COutPoint(%s, %u)", hash.ToString().substr(0,10), n);
}

CTxIn::CTxIn(COutPoint prevoutIn, CScript scriptSigIn, uint32_t nSequenceIn)
{
    prevout = prevoutIn;
    scriptSig = scriptSigIn;
    nSequence = nSequenceIn;
}

CTxIn::CTxIn(uint256 hashPrevTx, uint32_t nOut, CScript scriptSigIn, uint32_t nSequenceIn)
{
    prevout = COutPoint(hashPrevTx, nOut);
    scriptSig = scriptSigIn;
    nSequence = nSequenceIn;
}

std::string CTxIn::ToString() const
{
    std::string str;
    str += "CTxIn(";
    str += prevout.ToString();
    if (prevout.IsNull() && !IsZerocoinSpend())
        str += strprintf(", coinbase %s", HexStr(scriptSig));
    else
        str += strprintf(", scriptSig=%s", HexStr(scriptSig).substr(0, 24));
    if (nSequence != SEQUENCE_FINAL)
        str += strprintf(", nSequence=%u", nSequence);
    str += ")";
    return str;
}

void CTxOutBase::SetValue(int64_t value)
{
    // convenience function intended for use with CTxOutStandard only
    if (nVersion != OUTPUT_STANDARD) return;
    ((CTxOutStandard*) this)->nValue = value;
}

void CTxOutBase::AddToValue(const CAmount& nValue)
{
    if (nVersion != OUTPUT_STANDARD)
        return;
    ((CTxOutStandard*) this)->nValue += nValue;
}

CAmount CTxOutBase::GetValue() const
{
    // convenience function intended for use with CTxOutStandard only
    /*
    switch (nVersion)
    {
        case OUTPUT_STANDARD:
            return ((CTxOutStandard*) this)->nValue;
        case OUTPUT_DATA:
            return 0;
        default:
            assert(false);

    };
    */
    if (nVersion != OUTPUT_STANDARD) return 0;
    return ((CTxOutStandard*) this)->nValue;
};

bool CTxOutBase::GetTxOut(CTxOut& out) const
{
    if (!IsStandardOutput())
        return false;

    out = GetStandardOutput()->ToTxOut();
    return true;
}

std::string CTxOutBase::ToString() const
{
    switch (nVersion)
    {
        case OUTPUT_STANDARD:
        {
            CTxOutStandard *so = (CTxOutStandard*)this;
            return strprintf("CTxOutStandard(nValue=%d.%08d, scriptPubKey=%s)", so->nValue / COIN, so->nValue % COIN, HexStr(so->scriptPubKey).substr(0, 30));
        }
        case OUTPUT_DATA:
        {
            CTxOutData *dout = (CTxOutData*)this;
            return strprintf("CTxOutData(data=%s)", HexStr(dout->vData).substr(0, 30));
        }
        case OUTPUT_CT:
        {
            CTxOutCT *cto = (CTxOutCT*)this;
            return strprintf("CTxOutCT(data=%s, scriptPubKey=%s)", HexStr(cto->vData).substr(0, 30), HexStr(cto->scriptPubKey).substr(0, 30));
        }
        case OUTPUT_RINGCT:
        {
            CTxOutRingCT *rcto = (CTxOutRingCT*)this;
            return strprintf("CTxOutRingCT(data=%s, pk=%s)", HexStr(rcto->vData).substr(0, 30), HexStr(rcto->pk).substr(0, 30));
        }
        default:
            break;
    };
    return strprintf("CTxOutBase unknown version %d", nVersion);
}

CTxOutStandard::CTxOutStandard(const CAmount& nValueIn, CScript scriptPubKeyIn) : CTxOutBase(OUTPUT_STANDARD)
{
    nValue = nValueIn;
    scriptPubKey = scriptPubKeyIn;
}

bool CTxOutStandard::SetScriptPubKey(const CScript& scriptPubKey)
{
    this->scriptPubKey = scriptPubKey;
    return true;
}

bool CTxOutCT::SetScriptPubKey(const CScript& scriptPubKey)
{
    this->scriptPubKey = scriptPubKey;
    return true;
}

void DeepCopy(CTxOutBaseRef &to, const CTxOutBaseRef &from)
{
    switch (from->GetType()) {
        case OUTPUT_STANDARD:
            to = MAKE_OUTPUT<CTxOutStandard>();
            *((CTxOutStandard*)to.get()) = *((CTxOutStandard*)from.get());
            break;
        case OUTPUT_CT:
            to = MAKE_OUTPUT<CTxOutCT>();
            *((CTxOutCT*)to.get()) = *((CTxOutCT*)from.get());
            break;
        case OUTPUT_RINGCT:
            to = MAKE_OUTPUT<CTxOutRingCT>();
            *((CTxOutRingCT*)to.get()) = *((CTxOutRingCT*)from.get());
            break;
        case OUTPUT_DATA:
            to = MAKE_OUTPUT<CTxOutData>();
            *((CTxOutData*)to.get()) = *((CTxOutData*)from.get());
            break;
        default:
            break;
    }
    return;
}

std::vector<CTxOutBaseRef> DeepCopy(const std::vector<CTxOutBaseRef> &from)
{
    std::vector<CTxOutBaseRef> vpout;
    vpout.resize(from.size());
    for (size_t i = 0; i < from.size(); ++i) {
        DeepCopy(vpout[i], from[i]);
    }

    return vpout;
}


CAmount CTxIn::GetZerocoinSpent() const
{
    if (!IsZerocoinSpend())
        return 0;
    return (nSequence&CTxIn::SEQUENCE_LOCKTIME_MASK) * COIN;
}

bool CTxIn::IsZerocoinSpend() const
{
    return scriptSig.IsZerocoinSpend();
}

CTxOut::CTxOut(const CAmount& nValueIn, CScript scriptPubKeyIn)
{
    nValue = nValueIn;
    scriptPubKey = scriptPubKeyIn;
}

std::shared_ptr<CTxOutStandard> CTxOut::GetSharedPtr()
{
    OUTPUT_PTR<CTxOutStandard> p = MAKE_OUTPUT<CTxOutStandard>();
    p->scriptPubKey = scriptPubKey;
    p->nValue = nValue;
    return std::move(p);
}

std::string CTxOut::ToString() const
{
    return strprintf("CTxOut(nValue=%d.%08d, scriptPubKey=%s)", nValue / COIN, nValue % COIN, HexStr(scriptPubKey).substr(0, 30));
}

CMutableTransaction::CMutableTransaction() : nVersion(CTransaction::CURRENT_VERSION), nLockTime(0) {}
CMutableTransaction::CMutableTransaction(const CTransaction& tx) : vin(tx.vin), vpout{DeepCopy(tx.vpout)}, nVersion(tx.nVersion), nLockTime(tx.nLockTime) {}

uint256 CMutableTransaction::GetHash() const
{
    return SerializeHash(*this, SER_GETHASH, SERIALIZE_TRANSACTION_NO_WITNESS);
}

uint256 CTransaction::ComputeHash() const
{
    return SerializeHash(*this, SER_GETHASH, SERIALIZE_TRANSACTION_NO_WITNESS);
}

uint256 CTransaction::ComputeWitnessHash() const
{
    if (!HasWitness()) {
        return hash;
    }
    return SerializeHash(*this, SER_GETHASH, 0);
}

uint256 CTransaction::GetOutputsHash() const
{
    uint256 hashOutputs;
    for (const auto& out : vpout) {
        auto hash = SerializeHash(*out, SER_GETHASH);
        hashOutputs = Hash(BEGIN(hash), END(hash), hashOutputs.begin(), hashOutputs.end());
    }
    return hashOutputs;
}

/* For backward compatibility, the hash is initialized to 0. TODO: remove the need for this default constructor entirely. */
CTransaction::CTransaction() : vin(), vpout(), nVersion(CTransaction::CURRENT_VERSION), nLockTime(0), hash{}, m_witness_hash{} {}
CTransaction::CTransaction(const CMutableTransaction &tx) : vin(tx.vin), vpout{DeepCopy(tx.vpout)}, nVersion(tx.nVersion), nLockTime(tx.nLockTime), hash{ComputeHash()}, m_witness_hash{ComputeWitnessHash()} {}
CTransaction::CTransaction(CMutableTransaction &&tx) : vin(std::move(tx.vin)), vpout(std::move(tx.vpout)), nVersion(tx.nVersion), nLockTime(tx.nLockTime), hash{ComputeHash()}, m_witness_hash{ComputeWitnessHash()} {}

CAmount CTransaction::GetValueOut() const
{
    CAmount nValueOut = 0;
    for (const auto& pout : vpout) {
        nValueOut += pout->GetValue();
        if (!MoneyRange(pout->GetValue()) || !MoneyRange(nValueOut))
            throw std::runtime_error(std::string(__func__) + ": value out of range");
    }

    return nValueOut;
}

CAmount CTransaction::GetPlainValueOut(size_t &nStandard, size_t &nCT, size_t &nRingCT) const
{
    // accumulators not cleared here intentionally
    CAmount nValueOut = 0;

    for (const auto &txout : vpout) {
        if (txout->IsType(OUTPUT_CT)) {
            nCT++;
        } else if (txout->IsType(OUTPUT_RINGCT)) {
            nRingCT++;
        }

        if (!txout->IsStandardOutput())
            continue;

        nStandard++;
        CAmount nValue = txout->GetValue();
        nValueOut += nValue;
        if (!MoneyRange(nValue) || !MoneyRange(nValueOut))
            throw std::runtime_error(std::string(__func__) + ": value out of range");
    }

    return nValueOut;
}

unsigned int CTransaction::GetTotalSize() const
{
    return ::GetSerializeSize(*this, SER_NETWORK, PROTOCOL_VERSION);
}

std::string CTransaction::ToString() const
{
    std::string str;
    str += strprintf("CTransaction(hash=%s, ver=%d, vin.size=%u, nLockTime=%u)\n",
        GetHash().ToString().substr(0,10),
        nVersion,
        vin.size(),
        nLockTime);
    for (const auto& tx_in : vin)
        str += "    " + tx_in.ToString() + "\n";
    for (const auto& tx_in : vin)
        str += "    " + tx_in.scriptWitness.ToString() + "\n";
    for (const auto& tx_out : vpout)
        str += "    " + tx_out->ToString() + "\n";
    return str;
}

bool CTransaction::IsCoinBase() const
{
    return !IsZerocoinSpend() && (vin.size() == 1 && vin[0].prevout.IsNull());
}

bool CTransaction::IsCoinStake() const
{
    if (vin.empty())
        return false;

    if (vin.size() != 1 || !vin[0].IsZerocoinSpend())
        return false;

    // the coin stake transaction is marked with the first output empty
    return (vpout.size() > 1 && vpout[0]->IsEmpty());
}

bool CTransaction::HasBlindedValues() const
{
    for (const auto& in : vin) {
        if (in.IsAnonInput())
            return true;
    }

    for (const auto& pout : vpout) {
        if (pout->IsType(OUTPUT_CT) || pout->IsType(OUTPUT_RINGCT))
            return true;
    }

    return false;
}

bool CTransaction::IsZerocoinMint() const
{
    for(const auto& pout : vpout) {
        CScript script;
        if (pout->GetScriptPubKey(script)) {
            if (script.IsZerocoinMint())
                return true;
        }
    }
    return false;
}

bool CTransaction::IsZerocoinSpend() const
{
    for (const CTxIn& in : vin) {
        if (in.IsZerocoinSpend())
            return true;
    }
    return false;
}

CAmount CTransaction::GetZerocoinSpent() const
{
    if(!IsZerocoinSpend())
        return 0;

    CAmount nValueOut = 0;
    for (const CTxIn& txin : vin) {
        if(!txin.IsZerocoinSpend())
            continue;

        nValueOut += txin.GetZerocoinSpent();
    }

    return nValueOut;
}

CAmount CTransaction::GetZerocoinMinted() const
{
    for (const auto& pOut : vpout) {
        if (!pOut->IsStandardOutput())
            continue;
        if (!pOut->GetPScriptPubKey()->IsZerocoinMint())
            continue;

        return pOut->GetValue();
    }

    return  CAmount(0);
}

int CTransaction::GetZerocoinMintCount() const
{
    int nCount = 0;
    for (const auto& pOut : vpout) {
        if (!pOut->IsStandardOutput())
            continue;
        if (pOut->GetPScriptPubKey()->IsZerocoinMint())
            nCount++;
    }
    return nCount;
}

CTransaction& CTransaction::operator=(const CTransaction &tx) {
    *const_cast<int*>(&nVersion) = tx.nVersion;
    *const_cast<std::vector<CTxIn>*>(&vin) = tx.vin;
    *const_cast<std::vector<CTxOutBaseRef>*>(&vpout) = tx.vpout;
    *const_cast<unsigned int*>(&nLockTime) = tx.nLockTime;
    *const_cast<uint256*>(&hash) = tx.hash;
    return *this;
}
