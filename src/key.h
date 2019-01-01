// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2019 The Bitcoin Core developers
// Copyright (c) 2017 The Zcash developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_KEY_H
#define BITCOIN_KEY_H

#include <pubkey.h>
#include <serialize.h>
#include <support/allocators/secure.h>
#include <uint256.h>

#include <stdexcept>
#include <vector>


/**
 * secure_allocator is defined in allocators.h
 * CPrivKey is a serialized private key, with all parameters included
 * (PRIVATE_KEY_SIZE bytes)
 */
typedef std::vector<unsigned char, secure_allocator<unsigned char> > CPrivKey;

/** An encapsulated private key. */
class CKey
{
public:
    /**
     * secp256k1:
     */
    static const unsigned int PRIVATE_KEY_SIZE            = 279;
    static const unsigned int COMPRESSED_PRIVATE_KEY_SIZE = 214;
    /**
     * see www.keylength.com
     * script supports up to 75 for single byte push
     */
    static_assert(
        PRIVATE_KEY_SIZE >= COMPRESSED_PRIVATE_KEY_SIZE,
        "COMPRESSED_PRIVATE_KEY_SIZE is larger than PRIVATE_KEY_SIZE");

private:
    //! Whether this private key is valid. We check for correctness when modifying the key
    //! data, so fValid should always correspond to the actual state.
    bool fValid;

    //! Whether the public key corresponding to this private key is (to be) compressed.
    bool fCompressed;

    //! The actual byte data
    std::vector<unsigned char, secure_allocator<unsigned char> > keydata;

    //! Check whether the 32-byte array pointed to by vch is valid keydata.
    bool static Check(const unsigned char* vch);

public:
    //! Construct an invalid private key.
    CKey() : fValid(false), fCompressed(false)
    {
        // Important: vch must be 32 bytes in length to not break serialization
        keydata.resize(32);
    }

    friend bool operator==(const CKey& a, const CKey& b)
    {
        return a.fCompressed == b.fCompressed &&
            a.size() == b.size() &&
            memcmp(a.keydata.data(), b.keydata.data(), a.size()) == 0;
    }

    friend bool operator<(const CKey& a, const CKey& b)
    {
        return a.fCompressed == b.fCompressed && a.size() == b.size() &&
               memcmp(a.keydata.data(), b.keydata.data(), a.size()) < 0;
    }

    //! Initialize using begin and end iterators to byte data.
    template <typename T>
    void Set(const T pbegin, const T pend, bool fCompressedIn)
    {
        if (size_t(pend - pbegin) != keydata.size()) {
            fValid = false;
        } else if (Check(&pbegin[0])) {
            memcpy(keydata.data(), (unsigned char*)&pbegin[0], keydata.size());
            fValid = true;
            fCompressed = fCompressedIn;
        } else {
            fValid = false;
        }
    }

    void Set(const unsigned char *p, bool fCompressedIn)
    {
        if (Check(p)) {
            memcpy(keydata.data(), p, keydata.size());
            fValid = true;
            fCompressed = fCompressedIn;
        } else {
            fValid = false;
        }
    }

    void Clear()
    {
        //memory_cleanse(vch, sizeof(vch));
        memset(keydata.data(), 0, size());
        fCompressed = true;
        fValid = false;
    }

    void SetFlags(bool fValidIn, bool fCompressedIn)
    {
        fValid = fValidIn;
        fCompressed = fCompressedIn;
    }

    //! Simple read-only vector-like interface.
    unsigned int size() const { return (fValid ? keydata.size() : 0); }
    const unsigned char* begin() const { return keydata.data(); }
    const unsigned char* end() const { return keydata.data() + size(); }
    unsigned char* begin_nc() { return keydata.data(); }

    //! Check whether this private key is valid.
    bool IsValid() const { return fValid; }

    //! Check whether the public key corresponding to this private key is (to be) compressed.
    bool IsCompressed() const { return fCompressed; }

    //! Initialize from a CPrivKey (serialized OpenSSL private key data).
    bool SetPrivKey(const CPrivKey& vchPrivKey, bool fCompressed);

    //! Generate a new private key using a cryptographic PRNG.
    void MakeNewKey(bool fCompressed);

    uint256 GetPrivKey_256() const;

    /**
     * Convert the private key to a CPrivKey (serialized OpenSSL private key data).
     * This is expensive.
     */
    CPrivKey GetPrivKey() const;

    /**
     * Compute the public key from a private key.
     * This is expensive.
     */
    CPubKey GetPubKey() const;

    /**
     * Compute the ECDH exchange result using this private key and another public key.
     */
    uint256 ECDH(const CPubKey& pubkey) const;

    CKey Add(const uint8_t *p) const;


    /**
     * Create a DER-serialized signature.
     * The test_case parameter tweaks the deterministic nonce.
     */
    bool Sign(const uint256& hash, std::vector<unsigned char>& vchSig, bool grind = true, uint32_t test_case = 0) const;

    /**
     * Create a compact signature (65 bytes), which allows reconstructing the used public key.
     * The format is one header byte, followed by two times 32 bytes for the serialized r and s values.
     * The header byte: 0x1B = first key with even y, 0x1C = first key with odd y,
     *                  0x1D = second key with even y, 0x1E = second key with odd y,
     *                  add 0x04 for compressed keys.
     */
    bool SignCompact(const uint256& hash, std::vector<unsigned char>& vchSig) const;

    //! Derive BIP32 child key.
    bool Derive(CKey& keyChild, ChainCode &ccChild, unsigned int nChild, const ChainCode& cc) const;
    bool Derive(CKey& keyChild, unsigned char ccChild[32], unsigned int nChild, const unsigned char cc[32]) const;

    /**
     * Verify thoroughly whether a private key and a public key match.
     * This is done using a different mechanism than just regenerating it.
     */
    bool VerifyPubKey(const CPubKey& vchPubKey) const;

    //! Load private key and check that public key matches.
    bool Load(const CPrivKey& privkey, const CPubKey& vchPubKey, bool fSkipCheck);

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream &s, Operation ser_action)
    {
        if (!ser_action.ForRead()) {
            s.write((char*)&keydata[0], 32);
        } else {
            s.read((char*)&keydata[0], 32);
        }
        READWRITE(fValid);
        READWRITE(fCompressed);
    }
};

//struct CExtKey {
//    unsigned char nDepth;
//    unsigned char vchFingerprint[4];
//    unsigned int nChild;
//    ChainCode chaincode;
//    CKey key;
//
//    friend bool operator==(const CExtKey& a, const CExtKey& b)
//    {
//        return a.nDepth == b.nDepth &&
//            memcmp(&a.vchFingerprint[0], &b.vchFingerprint[0], sizeof(vchFingerprint)) == 0 &&
//            a.nChild == b.nChild &&
//            a.chaincode == b.chaincode &&
//            a.key == b.key;
//    }
//
//    void Encode(unsigned char code[BIP32_EXTKEY_SIZE]) const;
//    void Decode(const unsigned char code[BIP32_EXTKEY_SIZE]);
//    bool Derive(CExtKey& out, unsigned int nChild) const;
//    CExtPubKey Neuter() const;
//    void SetSeed(const unsigned char* seed, unsigned int nSeedLen);
//    template <typename Stream>
//    void Serialize(Stream& s) const
//    {
//        unsigned int len = BIP32_EXTKEY_SIZE;
//        ::WriteCompactSize(s, len);
//        unsigned char code[BIP32_EXTKEY_SIZE];
//        Encode(code);
//        s.write((const char *)&code[0], len);
//    }
//    template <typename Stream>
//    void Unserialize(Stream& s)
//    {
//        unsigned int len = ::ReadCompactSize(s);
//        unsigned char code[BIP32_EXTKEY_SIZE];
//        if (len != BIP32_EXTKEY_SIZE)
//            throw std::runtime_error("Invalid extended key size\n");
//        s.read((char *)&code[0], len);
//        Decode(code);
//    }
//};

//class CExtKeyPair
//{
//public:
//    //uint8_t nFlags; ? crypted
//    uint8_t nDepth;
//    uint8_t vchFingerprint[4];
//    uint32_t nChild;
//    ChainCode chaincode;
//    CKey key;
//    CPubKey pubkey;
//
//    CExtKeyPair() {};
//    CExtKeyPair(CExtKey &vk)
//    {
//        nDepth = vk.nDepth;
//        memcpy(vchFingerprint, vk.vchFingerprint, sizeof(vchFingerprint));
//        nChild = vk.nChild;
//        chaincode = vk.chaincode;
//        key = vk.key;
//        pubkey = key.GetPubKey();
//    };
//
//    CExtKeyPair(CExtPubKey &pk)
//    {
//        nDepth = pk.nDepth;
//        memcpy(vchFingerprint, pk.vchFingerprint, sizeof(vchFingerprint));
//        nChild = pk.nChild;
//        chaincode = pk.chaincode;
//        key.Clear();
//        pubkey = pk.pubkey;
//    };
//
//    CExtKey GetExtKey() const
//    {
//        CExtKey vk;
//        vk.nDepth = nDepth;
//        memcpy(vk.vchFingerprint, vchFingerprint, sizeof(vchFingerprint));
//        vk.nChild = nChild;
//        vk.chaincode = chaincode;
//        vk.key = key;
//        return vk;
//    };
//
//    CKeyID GetID() const {
//        return pubkey.GetID();
//    }
//
//    friend bool operator==(const CExtKeyPair &a, const CExtKeyPair &b)
//    {
//        return a.nDepth == b.nDepth && memcmp(&a.vchFingerprint[0], &b.vchFingerprint[0], 4) == 0 && a.nChild == b.nChild &&
//               memcmp(a.chaincode.begin(), b.chaincode.begin(), 32) == 0 && a.key == b.key && a.pubkey == b.pubkey;
//    }
//
//    friend bool operator < (const CExtKeyPair &a, const CExtKeyPair &b)
//    {
//        return a.nDepth < b.nDepth || memcmp(&a.vchFingerprint[0], &b.vchFingerprint[0], 4) < 0 || a.nChild < b.nChild
//               || memcmp(a.chaincode.begin(), b.chaincode.begin(), 32) < 0 || a.key < b.key || a.pubkey < b.pubkey;
//    }
//
//    bool IsValidV() const { return key.IsValid(); }
//    bool IsValidP() const { return pubkey.IsValid(); }
//
//    void EncodeV(unsigned char code[74]) const;
//    void DecodeV(const unsigned char code[74]);
//
//    void EncodeP(unsigned char code[74]) const;
//    void DecodeP(const unsigned char code[74]);
//
//    bool Derive(CExtKey &out, unsigned int nChild) const;
//    bool Derive(CExtPubKey &out, unsigned int nChild) const;
//    bool Derive(CKey &out, unsigned int nChild) const;
//    bool Derive(CPubKey &out, unsigned int nChild) const;
//
//    CExtPubKey GetExtPubKey() const;
//    CExtKeyPair Neutered() const;
//    void SetSeed(const unsigned char *seed, unsigned int nSeedLen);
//    int SetKeyCode(const unsigned char *pkey, const unsigned char *pcode);
//
//    template<typename Stream>
//    void Serialize(Stream &s) const
//    {
//        s.write((char*)&nDepth, 1);
//        s.write((char*)vchFingerprint, 4);
//        s.write((char*)&nChild, 4);
//        s.write((char*)&chaincode, 32);
//
//        char fValid = key.IsValid();
//        s.write((char*)&fValid, 1);
//        if (fValid)
//            s.write((char*)key.begin(), 32);
//
//        pubkey.Serialize(s);
//    }
//    template<typename Stream>
//    void Unserialize(Stream &s)
//    {
//        s.read((char*)&nDepth, 1);
//        s.read((char*)vchFingerprint, 4);
//        s.read((char*)&nChild, 4);
//        s.read((char*)&chaincode, 32);
//
//        char tmp[33];
//        s.read((char*)tmp, 1); // key.IsValid()
//        if (tmp[0])
//        {
//            s.read((char*)tmp+1, 32);
//            key.Set((uint8_t*)tmp+1, 1);
//        } else
//        {
//            key.Clear();
//        };
//        pubkey.Unserialize(s);
//    }
//};

/** Initialize the elliptic curve support. May not be called twice without calling ECC_Stop first. */
void ECC_Start(void);

/** Deinitialize the elliptic curve support. No-op if ECC_Start wasn't called first. */
void ECC_Stop(void);

/** Check that required EC support is available at runtime. */
bool ECC_InitSanityCheck(void);

#endif // BITCOIN_KEY_H
