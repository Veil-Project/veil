// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2019 The Bitcoin Core developers
// Copyright (c) 2018-2019 The Veil developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_PRIMITIVES_TRANSACTION_H
#define BITCOIN_PRIMITIVES_TRANSACTION_H

#include <stdint.h>
#include <amount.h>
#include <script/script.h>
#include <serialize.h>
#include <uint256.h>
#include <arith_uint256.h>

#include <secp256k1_rangeproof.h>

static const int SERIALIZE_TRANSACTION_NO_WITNESS = 0x40000000;

enum OutputTypes
{
    OUTPUT_NULL             = 0, // marker for CCoinsView (0.14)
    OUTPUT_STANDARD         = 1,
    OUTPUT_CT               = 2,
    OUTPUT_RINGCT           = 3,
    OUTPUT_DATA             = 4,
};

enum TransactionTypes
{
    TXN_STANDARD            = 0,
    TXN_COINBASE            = 1,
    TXN_COINSTAKE           = 2,
};

enum DataOutputTypes
{
    DO_NULL                 = 0, // reserved
    DO_NARR_PLAIN           = 1,
    DO_NARR_CRYPT           = 2,
    DO_STEALTH              = 3,
    DO_STEALTH_PREFIX       = 4,
    DO_VOTE                 = 5,
    DO_FEE                  = 6,
    DO_DEV_FUND_CFWD        = 7,
    DO_FUND_MSG             = 8,
};

/** An outpoint - a combination of a transaction hash and an index n into its vout */
class COutPoint
{
public:
    uint256 hash;
    uint32_t n;

    static const uint32_t ANON_MARKER = 0xffffffa0;

    COutPoint(): n((uint32_t) -1) { }
    COutPoint(const uint256& hashIn, uint32_t nIn): hash(hashIn), n(nIn) { }

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(hash);
        READWRITE(n);
    }

    void SetNull() { hash.SetNull(); n = (uint32_t) -1; }
    bool IsNull() const { return (hash.IsNull() && n == (uint32_t) -1); }

    friend bool operator<(const COutPoint& a, const COutPoint& b)
    {
        int cmp = a.hash.Compare(b.hash);
        return cmp < 0 || (cmp == 0 && a.n < b.n);
    }

    friend bool operator==(const COutPoint& a, const COutPoint& b)
    {
        return (a.hash == b.hash && a.n == b.n);
    }

    friend bool operator!=(const COutPoint& a, const COutPoint& b)
    {
        return !(a == b);
    }

    bool IsAnonInput() const
    {
        return n == ANON_MARKER;
    };

    std::string ToString() const;
};

/** An input of a transaction.  It contains the location of the previous
 * transaction's output that it claims and a signature that matches the
 * output's public key.
 */
class CTxIn
{
public:
    COutPoint prevout;
    CScript scriptSig;
    uint32_t nSequence;
    CScriptWitness scriptData; // Non prunable
    CScriptWitness scriptWitness; //! Only serialized through CTransaction

    /* Setting nSequence to this value for every input in a transaction
     * disables nLockTime. */
    static const uint32_t SEQUENCE_FINAL = 0xffffffff;

    /* Below flags apply in the context of BIP 68*/
    /* If this flag set, CTxIn::nSequence is NOT interpreted as a
     * relative lock-time. */
    static const uint32_t SEQUENCE_LOCKTIME_DISABLE_FLAG = (1 << 31);

    /* If CTxIn::nSequence encodes a relative lock-time and this flag
     * is set, the relative lock-time has units of 512 seconds,
     * otherwise it specifies blocks with a granularity of 1. */
    static const uint32_t SEQUENCE_LOCKTIME_TYPE_FLAG = (1 << 22);

    /* If CTxIn::nSequence encodes a relative lock-time, this mask is
     * applied to extract that lock-time from the sequence field. */
    static const uint32_t SEQUENCE_LOCKTIME_MASK = 0x0000ffff;

    /* In order to use the same number of bits to encode roughly the
     * same wall-clock duration, and because blocks are naturally
     * limited to occur every 600s on average, the minimum granularity
     * for time-based relative lock-time is fixed at 512 seconds.
     * Converting from CTxIn::nSequence to seconds is performed by
     * multiplying by 512 = 2^9, or equivalently shifting up by
     * 9 bits. */
    static const int SEQUENCE_LOCKTIME_GRANULARITY = 9;

    CTxIn()
    {
        nSequence = SEQUENCE_FINAL;
    }

    CAmount GetZerocoinSpent() const;
    bool IsZerocoinSpend() const;

    explicit CTxIn(COutPoint prevoutIn, CScript scriptSigIn=CScript(), uint32_t nSequenceIn=SEQUENCE_FINAL);
    CTxIn(uint256 hashPrevTx, uint32_t nOut, CScript scriptSigIn=CScript(), uint32_t nSequenceIn=SEQUENCE_FINAL);

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(prevout);
        READWRITE(scriptSig);
        READWRITE(nSequence);

        if (IsAnonInput())
        {
            READWRITE(scriptData.stack);
        }
    }

    friend bool operator==(const CTxIn& a, const CTxIn& b)
    {
        return (a.prevout   == b.prevout &&
                a.scriptSig == b.scriptSig &&
                a.nSequence == b.nSequence);
    }

    friend bool operator!=(const CTxIn& a, const CTxIn& b)
    {
        return !(a == b);
    }

    bool IsAnonInput() const
    {
        return prevout.IsAnonInput();
    }

    bool SetAnonInfo(uint32_t nInputs, uint32_t nRingSize)
    {
        memcpy(prevout.hash.begin(), &nInputs, 4);
        memcpy(prevout.hash.begin()+4, &nRingSize, 4);
        return true;
    }

    bool GetAnonInfo(uint32_t &nInputs, uint32_t &nRingSize) const
    {
        memcpy(&nInputs, prevout.hash.begin(), 4);
        memcpy(&nRingSize, prevout.hash.begin()+4, 4);
        return true;
    }

    std::string ToString() const;
};

class CTxOutStandard;
class CTxOutCT;
class CTxOutRingCT;
class CTxOutData;

/** An output of a transaction.  It contains the public key that the next input
 * must be able to sign with to claim it.
 */
class CTxOut
{
public:
    CAmount nValue;
    CScript scriptPubKey;

    CTxOut()
    {
        SetNull();
    }

    CTxOut(const CAmount& nValueIn, CScript scriptPubKeyIn);

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(nValue);
        READWRITE(scriptPubKey);
    }

    void SetNull()
    {
        nValue = -1;
        scriptPubKey.clear();
    }

    bool IsNull() const
    {
        return (nValue == -1);
    }

    void SetEmpty()
    {
        nValue = 0;
        scriptPubKey.clear();
    }

    bool IsEmpty() const
    {
        return (nValue == 0 && scriptPubKey.empty());
    }

    bool IsZerocoinMint() const
    {
        return !scriptPubKey.empty() && scriptPubKey.IsZerocoinMint();
    }

    friend bool operator==(const CTxOut& a, const CTxOut& b)
    {
        return (a.nValue       == b.nValue &&
                a.scriptPubKey == b.scriptPubKey);
    }

    friend bool operator!=(const CTxOut& a, const CTxOut& b)
    {
        return !(a == b);
    }

    std::string ToString() const;
    std::shared_ptr<CTxOutStandard> GetSharedPtr();
};

class CTxOutBase
{
public:
    CTxOutBase(uint8_t v) : nVersion(v) {};
    virtual ~CTxOutBase() {};
    uint8_t nVersion;

    template<typename Stream>
    void Serialize(Stream &s) const
    {
        switch (nVersion)
        {
            case OUTPUT_STANDARD:
                s << *((CTxOutStandard*) this);
                break;
            case OUTPUT_CT:
                s << *((CTxOutCT*) this);
                break;
            case OUTPUT_RINGCT:
                s << *((CTxOutRingCT*) this);
                break;
            case OUTPUT_DATA:
                s << *((CTxOutData*) this);
                break;
            default:
                throw std::runtime_error("serialize error: tx output type does not exist");
        }
    }

    template<typename Stream>
    void Unserialize(Stream &s)
    {
        switch (nVersion)
        {
            case OUTPUT_STANDARD:
                s >> *((CTxOutStandard*) this);
                break;
            case OUTPUT_CT:
                s >> *((CTxOutCT*) this);
                break;
            case OUTPUT_RINGCT:
                s >> *((CTxOutRingCT*) this);
                break;
            case OUTPUT_DATA:
                s >> *((CTxOutData*) this);
                break;
            default:
                throw std::runtime_error("deserialize error: tx output type does not exist");
        }
    }

    uint8_t GetType() const
    {
        return nVersion;
    }

    bool IsType(uint8_t nType) const
    {
        return nVersion == nType;
    }

    bool IsStandardOutput() const
    {
        return nVersion == OUTPUT_STANDARD;
    }

    bool IsZerocoinMint() const
    {
        if (!IsStandardOutput())
            return false;
        return GetPScriptPubKey()->IsZerocoinMint();
    }

    const CTxOutStandard *GetStandardOutput() const
    {
        if (nVersion != OUTPUT_STANDARD)
            throw std::runtime_error("GetStandardOutput(): nVersion is not OUTPUT_STANDARD");
        return (CTxOutStandard*)this;
    }

    bool GetTxOut(CTxOut& out) const;

    virtual bool IsEmpty() const { return false;}

    void SetValue(CAmount value);
    void AddToValue(const CAmount& nValue);
    virtual bool SetScriptPubKey(const CScript& scriptPubKey) { return false; }

    virtual void SetNull() = 0;
    virtual CAmount GetValue() const;

    virtual bool PutValue(std::vector<uint8_t> &vchAmount) const { return false; };

    virtual bool GetScriptPubKey(CScript &scriptPubKey_) const { return false; };
    virtual const CScript *GetPScriptPubKey() const { return nullptr; };

    virtual secp256k1_pedersen_commitment *GetPCommitment() { return nullptr; };
    virtual std::vector<uint8_t> *GetPRangeproof() { return nullptr; };

    virtual bool GetCTFee(CAmount &nFee) const { return false; };
    virtual bool SetCTFee(CAmount &nFee) { return false; };

    std::string ToString() const;
};

#define OUTPUT_PTR std::shared_ptr
typedef OUTPUT_PTR<CTxOutBase> CTxOutBaseRef;
#define MAKE_OUTPUT std::make_shared

class CTxOutStandard : public CTxOutBase
{
public:
    CTxOutStandard() : CTxOutBase(OUTPUT_STANDARD) {};
    CTxOutStandard(const CAmount& nValueIn, CScript scriptPubKeyIn);

    CAmount nValue;
    CScript scriptPubKey;

    template<typename Stream>
    void Serialize(Stream &s) const
    {
        s << nValue;
        s << *(CScriptBase*)(&scriptPubKey);
    };

    template<typename Stream>
    void Unserialize(Stream &s)
    {
        s >> nValue;
        s >> *(CScriptBase*)(&scriptPubKey);
    };

    void SetNull() override
    {
        nValue = -1;
        scriptPubKey.clear();
    }

    bool IsEmpty() const override
    {
        return (nValue == 0 && scriptPubKey.empty());
    }

    bool PutValue(std::vector<uint8_t> &vchAmount) const override
    {
        vchAmount.resize(8);
        memcpy(&vchAmount[0], &nValue, 8);
        return true;
    }

    CAmount GetValue() const override
    {
        return nValue;
    }

    bool SetScriptPubKey(const CScript& scriptPubKey) override;

    bool GetScriptPubKey(CScript &scriptPubKey_) const override
    {
        scriptPubKey_ = scriptPubKey;
        return true;
    }

    virtual const CScript *GetPScriptPubKey() const override
    {
        return &scriptPubKey;
    }

    CTxOut ToTxOut() const { return CTxOut(nValue, scriptPubKey); }
};

static const uint32_t EPHEMERAL_PUBKEY_LENGTH = 33;

class CTxOutCT : public CTxOutBase
{
public:
    CTxOutCT() : CTxOutBase(OUTPUT_CT) {};
    secp256k1_pedersen_commitment commitment;
    std::vector<uint8_t> vData; // first 33 bytes is always ephemeral pubkey, can contain token for stealth prefix matching
    CScript scriptPubKey;
    std::vector<uint8_t> vRangeproof;

    template<typename Stream>
    void Serialize(Stream &s) const
    {
        s.write((char*)&commitment.data[0], 33);
        s << vData;
        s << *(CScriptBase*)(&scriptPubKey);
        s << vRangeproof;
    }

    template<typename Stream>
    void Unserialize(Stream &s)
    {
        s.read((char*)&commitment.data[0], 33);
        s >> vData;
        s >> *(CScriptBase*)(&scriptPubKey);
        s >> vRangeproof;
    }

    void SetNull() override
    {
        commitment = secp256k1_pedersen_commitment();
        vData.clear();
        scriptPubKey.clear();
        vRangeproof.clear();
    }

    bool PutValue(std::vector<uint8_t> &vchAmount) const override
    {
        vchAmount.resize(33);
        memcpy(&vchAmount[0], commitment.data, 33);
        return true;
    }

    bool GetScriptPubKey(CScript &scriptPubKey_) const override
    {
        scriptPubKey_ = scriptPubKey;
        return true;
    }

    bool SetScriptPubKey(const CScript& scriptPubKey) override;

    virtual const CScript *GetPScriptPubKey() const override
    {
        return &scriptPubKey;
    }

    secp256k1_pedersen_commitment *GetPCommitment() override
    {
        return &commitment;
    }

    std::vector<uint8_t> *GetPRangeproof() override
    {
        return &vRangeproof;
    }
};

class CTxOutRingCT : public CTxOutBase
{
public:
    CTxOutRingCT() : CTxOutBase(OUTPUT_RINGCT) {};
    CCmpPubKey pk;
    std::vector<uint8_t> vData; // first 33 bytes is always ephemeral pubkey, can contain token for stealth prefix matching
    secp256k1_pedersen_commitment commitment;
    std::vector<uint8_t> vRangeproof;



    template<typename Stream>
    void Serialize(Stream &s) const
    {
        s.write((char*)pk.begin(), 33);
        s.write((char*)&commitment.data[0], 33);
        s << vData;
        s << vRangeproof;
    }

    template<typename Stream>
    void Unserialize(Stream &s)
    {
        s.read((char*)pk.ncbegin(), 33);
        s.read((char*)&commitment.data[0], 33);
        s >> vData;
        s >> vRangeproof;
    }

    void SetNull() override
    {
        pk = CCmpPubKey();
        vData.clear();
        commitment = secp256k1_pedersen_commitment();
        vRangeproof.clear();
    }

    bool PutValue(std::vector<uint8_t> &vchAmount) const override
    {
        vchAmount.resize(33);
        memcpy(&vchAmount[0], commitment.data, 33);
        return true;
    }

    secp256k1_pedersen_commitment *GetPCommitment() override
    {
        return &commitment;
    }

    std::vector<uint8_t> *GetPRangeproof() override
    {
        return &vRangeproof;
    }

    bool SetScriptPubKey(const CScript& scriptPubKey) override { return false; }
};

class CTxOutWatchonly
{
public:
    CTxOutWatchonly() {};

    enum {
        NOTSET = -1,
        STEALTH = 0,
        ANON = 1
    };

    int type;
    CTxOutRingCT ringctOut;
    CTxOutCT ctOut;
    uint256 tx_hash;
    int nIndex;


    template<typename Stream>
    void Serialize(Stream &s) const
    {
        s << type;
        s << tx_hash;
        s << nIndex;
        if(type == STEALTH) {
            s << ctOut;
        } else if (type == ANON) {
            s << ringctOut;
        }
    }

    template<typename Stream>
    void Unserialize(Stream &s)
    {
        s >> type;
        s >> tx_hash;
        s >> nIndex;
        if (type == STEALTH) {
            s >> ctOut;
        } else if (type == ANON) {
            s >> ringctOut;
        }
    }
};

class CTxOutData : public CTxOutBase
{
public:
    CTxOutData() : CTxOutBase(OUTPUT_DATA) {};
    CTxOutData(const std::vector<uint8_t> &vData_) : CTxOutBase(OUTPUT_DATA), vData(vData_) {};

    std::vector<uint8_t> vData;

    template<typename Stream>
    void Serialize(Stream &s) const
    {
        s << vData;
    }

    template<typename Stream>
    void Unserialize(Stream &s)
    {
        s >> vData;
    }

    void SetNull() override
    {
        vData.clear();
    }

    bool GetCTFee(CAmount &nFee) const override
    {
        if (vData.size() < 2 || vData[0] != DO_FEE)
            return false;

        size_t nb;
        return (0 == GetVarInt(vData, 1, (uint64_t&)nFee, nb));
    };

    bool SetCTFee(CAmount &nFee) override
    {
        vData.clear();
        vData.push_back(DO_FEE);
        return (0 == PutVarInt(vData, nFee));
    }
    bool SetScriptPubKey(const CScript& scriptPubKey) override { return false; }
};

struct CMutableTransaction;

/**
 * Basic transaction serialization format:
 * - int32_t nVersion
 * - std::vector<CTxIn> vin
 * - std::vector<CTxOut> vout
 * - uint32_t nLockTime
 *
 * Extended transaction serialization format:
 * - int32_t nVersion
 * - unsigned char dummy = 0x00
 * - unsigned char flags (!= 0)
 * - bool Tx Has Segwit
 * - std::vector<CTxIn> vin
 * - std::vector<CTxOut> vout
 * - if (flags & 1):
 *   - CTxWitness wit;
 * - uint32_t nLockTime
 */
template<typename Stream, typename TxType>
inline void UnserializeTransaction(TxType& tx, Stream& s) {

    uint8_t bv;
    tx.nVersion = 0;
    s >> bv;

    tx.nVersion = bv;
    s >> bv;
    tx.nVersion |= bv<<8; // TransactionTypes

    bool fUseSegwit;
    s >> fUseSegwit;

    s >> tx.nLockTime;

    tx.vin.clear();
    s >> tx.vin;

    size_t nOutputs = ReadCompactSize(s);
    tx.vpout.resize(nOutputs);
    for (size_t k = 0; k < tx.vpout.size(); ++k) {
        s >> bv;
        switch (bv) {
            case OUTPUT_STANDARD:
                tx.vpout[k] = MAKE_OUTPUT<CTxOutStandard>();
                break;
            case OUTPUT_CT:
                tx.vpout[k] = MAKE_OUTPUT<CTxOutCT>();
                break;
            case OUTPUT_RINGCT:
                tx.vpout[k] = MAKE_OUTPUT<CTxOutRingCT>();
                break;
            case OUTPUT_DATA:
                tx.vpout[k] = MAKE_OUTPUT<CTxOutData>();
                break;
            default:
                throw std::runtime_error("UnserializeTransaction error: output type does not exist");
        }

        tx.vpout[k]->nVersion = bv;
        s >> *tx.vpout[k];
    }

    if (fUseSegwit) {
        for (auto &txin : tx.vin)
            s >> txin.scriptWitness.stack;
    }
}

template<typename Stream, typename TxType>
inline void SerializeTransaction(const TxType& tx, Stream& s) {

    uint8_t bv = tx.nVersion & 0xFF;
    s << bv;

    bv = (tx.nVersion>>8) & 0xFF;
    s << bv; // TransactionType

    s << tx.HasWitness();

    s << tx.nLockTime;
    s << tx.vin;

    WriteCompactSize(s, tx.vpout.size());
    for (size_t k = 0; k < tx.vpout.size(); ++k) {
        s << tx.vpout[k]->nVersion;
        s << *tx.vpout[k];
    }

    if (tx.HasWitness()) {
        for (auto &txin : tx.vin)
            s << txin.scriptWitness.stack;
    }
}


/** The basic transaction that is broadcasted on the network and contained in
 * blocks.  A transaction can contain multiple inputs and outputs.
 */
class CTransaction
{
public:
    // Default transaction version.
    static const int32_t CURRENT_VERSION=2;

    // Changing the default transaction version requires a two step process: first
    // adapting relay policy by bumping MAX_STANDARD_VERSION, and then later date
    // bumping the default CURRENT_VERSION at which point both CURRENT_VERSION and
    // MAX_STANDARD_VERSION will be equal.
    static const int32_t MAX_STANDARD_VERSION=2;

    // The local variables are made const to prevent unintended modification
    // without updating the cached hash value. However, CTransaction is not
    // actually immutable; deserialization and assignment are implemented,
    // and bypass the constness. This is safe, as they update the entire
    // structure, including the hash.
    const std::vector<CTxIn> vin;
    const std::vector<CTxOutBaseRef> vpout;
    const int32_t nVersion;
    const uint32_t nLockTime;

private:
    /** Memory only. */
    const uint256 hash;
    const uint256 m_witness_hash;

    uint256 ComputeHash() const;
    uint256 ComputeWitnessHash() const;

public:
    /** Construct a CTransaction that qualifies as IsNull() */
    CTransaction();

    /** Convert a CMutableTransaction into a CTransaction. */
    CTransaction(const CMutableTransaction &tx);
    CTransaction(CMutableTransaction &&tx);

    template <typename Stream>
    inline void Serialize(Stream& s) const {
        SerializeTransaction(*this, s);
    }

    /** This deserializing constructor is provided instead of an Unserialize method.
     *  Unserialize is not possible, since it would require overwriting const fields. */
    template <typename Stream>
    CTransaction(deserialize_type, Stream& s) : CTransaction(CMutableTransaction(deserialize, s)) {}

    bool IsNull() const {
        return vin.empty() && vpout.empty();
    }

    int GetType() const {
        return (nVersion >> 8) & 0xFF;
    }

    size_t GetNumVOuts() const
    {
        return vpout.size();
    }

    size_t GetNumVInps() const
    {
        return vin.size();
    }

    bool IsCoinStake() const;

    bool IsRingCtSpend() const;
    bool IsZerocoinSpend() const;

    bool IsZerocoinMint() const;

    bool ContainsZerocoins() const
    {
        return IsZerocoinSpend() || IsZerocoinMint();
    }

    CAmount GetZerocoinSpent() const;
    CAmount GetZerocoinMinted() const;
    int GetZerocoinMintCount() const;

    const uint256& GetHash() const { return hash; }
    const uint256& GetWitnessHash() const { return m_witness_hash; };
    uint256 GetOutputsHash() const;

    // Return sum of txouts.
    CAmount GetValueOut() const;

    // Return sum of standard txouts and counts of output types
    CAmount GetPlainValueOut(size_t &nStandard, size_t &nCT, size_t &nRingCT) const;

    bool HasBlindedValues() const;

    // GetValueIn() is a method on CCoinsViewCache, because
    // inputs must be known to compute value in.

    /**
     * Get the total transaction size in bytes, including witness data.
     * "Total Size" defined in BIP141 and BIP144.
     * @return Total transaction size in bytes
     */
    unsigned int GetTotalSize() const;

    bool IsCoinBase() const;

    bool GetCTFee(CAmount &nFee) const
    {
        if (vpout.size() < 2 || vpout[0]->nVersion != OUTPUT_DATA)
            return false;

        return vpout[0]->GetCTFee(nFee);
    }

    friend bool operator==(const CTransaction& a, const CTransaction& b)
    {
        return a.hash == b.hash;
    }

    friend bool operator!=(const CTransaction& a, const CTransaction& b)
    {
        return a.hash != b.hash;
    }

    CTransaction& operator=(const CTransaction& tx);

    std::string ToString() const;

    bool HasWitness() const
    {
        for (size_t i = 0; i < vin.size(); i++) {
            if (!vin[i].scriptWitness.IsNull()) {
                return true;
            }
        }
        return false;
    }
};

/** A mutable version of CTransaction. */
struct CMutableTransaction
{
    std::vector<CTxIn> vin;
    std::vector<CTxOutBaseRef> vpout;
    int32_t nVersion;
    uint32_t nLockTime;

    CMutableTransaction();
    explicit CMutableTransaction(const CTransaction& tx);

    template <typename Stream>
    inline void Serialize(Stream& s) const {
        SerializeTransaction(*this, s);
    }


    template <typename Stream>
    inline void Unserialize(Stream& s) {
        UnserializeTransaction(*this, s);
    }

    template <typename Stream>
    CMutableTransaction(deserialize_type, Stream& s) {
        Unserialize(s);
    }

    void SetType(int type) {
        nVersion |= (type & 0xFF) << 8;
    }

    int GetType() const {
        return (nVersion >> 8) & 0xFF;
    }

    size_t GetNumVOuts() const
    {
        return vpout.size();
    }

    /** Compute the hash of this CMutableTransaction. This is computed on the
     * fly, as opposed to GetHash() in CTransaction, which uses a cached result.
     */
    uint256 GetHash() const;
    uint256 GetOutputsHash() const { return ((CTransaction*)this)->GetOutputsHash(); }

    bool HasWitness() const
    {
        for (size_t i = 0; i < vin.size(); i++) {
            if (!vin[i].scriptWitness.IsNull()) {
                return true;
            }
        }
        return false;
    }
};

typedef std::shared_ptr<const CTransaction> CTransactionRef;
static inline CTransactionRef MakeTransactionRef() { return std::make_shared<const CTransaction>(); }
template <typename Tx> static inline CTransactionRef MakeTransactionRef(Tx&& txIn) { return std::make_shared<const CTransaction>(std::forward<Tx>(txIn)); }

#endif // BITCOIN_PRIMITIVES_TRANSACTION_H
