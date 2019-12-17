// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2019 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_HASH_H
#define BITCOIN_HASH_H

#include <crypto/ripemd160.h>
#include <crypto/sha256.h>
#include <crypto/sha512.h>
#include "crypto/x16r/sph_blake.h"
#include "crypto/x16r/sph_bmw.h"
#include "crypto/x16r/sph_groestl.h"
#include "crypto/x16r/sph_jh.h"
#include "crypto/x16r/sph_keccak.h"
#include "crypto/x16r/sph_skein.h"
#include "crypto/x16r/sph_luffa.h"
#include "crypto/x16r/sph_cubehash.h"
#include "crypto/x16r/sph_shavite.h"
#include "crypto/x16r/sph_simd.h"
#include "crypto/x16r/sph_echo.h"
#include "crypto/x16r/sph_hamsi.h"
#include "crypto/x16r/sph_fugue.h"
#include "crypto/x16r/sph_shabal.h"
#include "crypto/x16r/sph_whirlpool.h"
#include "crypto/x16r/sph_sha2.h"

#include <prevector.h>
#include <serialize.h>
#include <uint256.h>
#include <version.h>

#include <vector>

class CBlockHeader;
typedef uint256 ChainCode;

/** A hasher class for Bitcoin's 256-bit hash (double SHA-256). */
class CHash256 {
private:
    CSHA256 sha;
public:
    static const size_t OUTPUT_SIZE = CSHA256::OUTPUT_SIZE;

    void Finalize(unsigned char hash[OUTPUT_SIZE]) {
        unsigned char buf[CSHA256::OUTPUT_SIZE];
        sha.Finalize(buf);
        sha.Reset().Write(buf, CSHA256::OUTPUT_SIZE).Finalize(hash);
    }

    CHash256& Write(const unsigned char *data, size_t len) {
        sha.Write(data, len);
        return *this;
    }

    CHash256& Reset() {
        sha.Reset();
        return *this;
    }
};

/** A hasher class for Bitcoin's 160-bit hash (SHA-256 + RIPEMD-160). */
class CHash160 {
private:
    CSHA256 sha;
public:
    static const size_t OUTPUT_SIZE = CRIPEMD160::OUTPUT_SIZE;

    void Finalize(unsigned char hash[OUTPUT_SIZE]) {
        unsigned char buf[CSHA256::OUTPUT_SIZE];
        sha.Finalize(buf);
        CRIPEMD160().Write(buf, CSHA256::OUTPUT_SIZE).Finalize(hash);
    }

    CHash160& Write(const unsigned char *data, size_t len) {
        sha.Write(data, len);
        return *this;
    }

    CHash160& Reset() {
        sha.Reset();
        return *this;
    }
};

class CHash512
{
private:
    CSHA512 sha;

public:
    static const size_t OUTPUT_SIZE = CSHA512::OUTPUT_SIZE;

    void Finalize(unsigned char hash[OUTPUT_SIZE])
    {
        unsigned char buf[CSHA512::OUTPUT_SIZE];
        sha.Finalize(buf);
        sha.Reset().Write(buf, CSHA512::OUTPUT_SIZE).Finalize(hash);
    }

    CHash512& Write(const unsigned char* data, size_t len)
    {
        sha.Write(data, len);
        return *this;
    }

    CHash512& Reset()
    {
        sha.Reset();
        return *this;
    }
};

/** Compute the 256-bit hash of an object. */
template<typename T1>
inline uint256 Hash(const T1 pbegin, const T1 pend)
{
    static const unsigned char pblank[1] = {};
    uint256 result;
    CHash256().Write(pbegin == pend ? pblank : (const unsigned char*)&pbegin[0], (pend - pbegin) * sizeof(pbegin[0]))
              .Finalize((unsigned char*)&result);
    return result;
}

/** Compute the sha256 hash of an object. */
template<typename T1>
inline uint256 HashSha256(const T1 pbegin, const T1 pend)
{
    static const unsigned char pblank[1] = {};
    uint256 result;
    CSHA256().Write(pbegin == pend ? pblank : (const unsigned char*)&pbegin[0], (pend - pbegin) * sizeof(pbegin[0]))
            .Finalize((unsigned char*)&result);
    return result;
}

/** Compute the 256-bit hash of the concatenation of two objects. */
template<typename T1, typename T2>
inline uint256 Hash(const T1 p1begin, const T1 p1end,
                    const T2 p2begin, const T2 p2end) {
    static const unsigned char pblank[1] = {};
    uint256 result;
    CHash256().Write(p1begin == p1end ? pblank : (const unsigned char*)&p1begin[0], (p1end - p1begin) * sizeof(p1begin[0]))
              .Write(p2begin == p2end ? pblank : (const unsigned char*)&p2begin[0], (p2end - p2begin) * sizeof(p2begin[0]))
              .Finalize((unsigned char*)&result);
    return result;
}

/** Compute the 160-bit hash an object. */
template<typename T1>
inline uint160 Hash160(const T1 pbegin, const T1 pend)
{
    static unsigned char pblank[1] = {};
    uint160 result;
    CHash160().Write(pbegin == pend ? pblank : (const unsigned char*)&pbegin[0], (pend - pbegin) * sizeof(pbegin[0]))
              .Finalize((unsigned char*)&result);
    return result;
}

/** Compute the 160-bit hash of a vector. */
inline uint160 Hash160(const std::vector<unsigned char>& vch)
{
    return Hash160(vch.begin(), vch.end());
}

/** Compute the 160-bit hash of a vector. */
template<unsigned int N>
inline uint160 Hash160(const prevector<N, unsigned char>& vch)
{
    return Hash160(vch.begin(), vch.end());
}

/** Compute the 512-bit hash of an object. */
template <typename T1>
inline uint512 Hash512(const T1 pbegin, const T1 pend)
{
    static const unsigned char pblank[1] = {};
    uint512 result;
    CHash512().Write(pbegin == pend ? pblank : (const unsigned char*)&pbegin[0], (pend - pbegin) *
         sizeof(pbegin[0])).Finalize((unsigned char*)&result);
    return result;
}

template <typename T1, typename T2>
inline uint512 Hash512(const T1 p1begin, const T1 p1end, const T2 p2begin, const T2 p2end)
{
    static const unsigned char pblank[1] = {};
    uint512 result;
    CHash512().Write(p1begin == p1end ? pblank : (const unsigned char*)&p1begin[0], (p1end - p1begin) *
         sizeof(p1begin[0])).Write(p2begin == p2end ? pblank : (const unsigned char*)&p2begin[0], (p2end - p2begin) *
         sizeof(p2begin[0])).Finalize((unsigned char*)&result);

    return result;
}

/** A writer stream (for serialization) that computes a 256-bit hash. */
class CHashWriter
{
private:
    CHash256 ctx;

    const int nType;
    const int nVersion;
public:

    CHashWriter(int nTypeIn, int nVersionIn) : nType(nTypeIn), nVersion(nVersionIn) {}

    int GetType() const { return nType; }
    int GetVersion() const { return nVersion; }

    void write(const char *pch, size_t size) {
        ctx.Write((const unsigned char*)pch, size);
    }

    void read(const char *psz, size_t _nSize)
    {
        // do nothing, needed by CTxOutBaseCompressor
    }

    // invalidates the object
    uint256 GetHash() {
        uint256 result;
        ctx.Finalize((unsigned char*)&result);
        return result;
    }

    template<typename T>
    CHashWriter& operator<<(const T& obj) {
        // Serialize to this stream
        ::Serialize(*this, obj);
        return (*this);
    }
};

/** A hasher class for Veils Zerocoin-Bulletproofs Protocol */
/** given buffer x returns 1024 bits where the first (left-most) 512 bits
 ** are SHA512(x) and the second (right-most) 512 bits are SHA512(SHA512(x))
 **/
class CHash1024
{
private:
    CSHA512 sha_l, sha_r;

public:
    static const size_t OUTPUT_SIZE = CSHA512::OUTPUT_SIZE;

    void Finalize(unsigned char hash_l[OUTPUT_SIZE], unsigned char hash_r[OUTPUT_SIZE])
    {
        sha_l.Finalize(hash_l);
        sha_l.Reset();
        sha_r.Reset().Write(hash_l, CSHA512::OUTPUT_SIZE).Finalize(hash_r);
    }

    CHash1024& Write(const unsigned char* data, size_t len)
    {
        sha_l.Write(data, len);
        return *this;
    }

    CHash1024& Reset()
    {
        sha_l.Reset();
        sha_r.Reset();
        return *this;
    }
};


/** A writer stream (for serialization) for the 1024-bit hash. **/
class CHashWriter1024
{
private:
    CHash1024 ctx;

public:
    int nType;
    int nVersion;

    CHashWriter1024(int nTypeIn, int nVersionIn) : nType(nTypeIn), nVersion(nVersionIn) {}

    CHashWriter1024& write(const char* pch, size_t size)
    {
        ctx.Write((const unsigned char*)pch, size);
        return (*this);
    }

    // invalidates the object
    std::vector<unsigned char> GetHash()
    {
      std::unique_ptr<unsigned char []> buf_l(new unsigned char[ctx.OUTPUT_SIZE]);
      std::unique_ptr<unsigned char []> buf_r(new unsigned char[ctx.OUTPUT_SIZE]);
      std::vector<unsigned char> buf(2*ctx.OUTPUT_SIZE);
      ctx.Finalize(buf_l.get(), buf_r.get());
      for(unsigned int i=0; i<ctx.OUTPUT_SIZE; i++)
        buf[i] = buf_l[i];
      for(unsigned int i=ctx.OUTPUT_SIZE; i<2*ctx.OUTPUT_SIZE; i++)
        buf[i] = buf_r[i-ctx.OUTPUT_SIZE];
      return buf;
    }

    template <typename T>
    CHashWriter1024& operator<<(const T& obj)
    {
        // Serialize to this stream
        ::Serialize(*this, obj);
        return (*this);
    }
};

/** Reads data from an underlying stream, while hashing the read data. */
template<typename Source>
class CHashVerifier : public CHashWriter
{
private:
    Source* source;

public:
    explicit CHashVerifier(Source* source_) : CHashWriter(source_->GetType(), source_->GetVersion()), source(source_) {}

    void read(char* pch, size_t nSize)
    {
        source->read(pch, nSize);
        this->write(pch, nSize);
    }

    void ignore(size_t nSize)
    {
        char data[1024];
        while (nSize > 0) {
            size_t now = std::min<size_t>(nSize, 1024);
            read(data, now);
            nSize -= now;
        }
    }

    template<typename T>
    CHashVerifier<Source>& operator>>(T&& obj)
    {
        // Unserialize from this stream
        ::Unserialize(*this, obj);
        return (*this);
    }
};

/** Compute the 256-bit hash of an object's serialization. */
template<typename T>
uint256 SerializeHash(const T& obj, int nType=SER_GETHASH, int nVersion=PROTOCOL_VERSION)
{
    CHashWriter ss(nType, nVersion);
    ss << obj;
    return ss.GetHash();
}

unsigned int MurmurHash3(unsigned int nHashSeed, const std::vector<unsigned char>& vDataToHash);

void BIP32Hash(const ChainCode &chainCode, unsigned int nChild, unsigned char header, const unsigned char data[32], unsigned char output[64]);
void BIP32Hash(const unsigned char chainCode[32], unsigned int nChild, unsigned char header, const unsigned char data[32], unsigned char output[64]);

/** SipHash-2-4 */
class CSipHasher
{
private:
    uint64_t v[4];
    uint64_t tmp;
    int count;

public:
    /** Construct a SipHash calculator initialized with 128-bit key (k0, k1) */
    CSipHasher(uint64_t k0, uint64_t k1);
    /** Hash a 64-bit integer worth of data
     *  It is treated as if this was the little-endian interpretation of 8 bytes.
     *  This function can only be used when a multiple of 8 bytes have been written so far.
     */
    CSipHasher& Write(uint64_t data);
    /** Hash arbitrary bytes. */
    CSipHasher& Write(const unsigned char* data, size_t size);
    /** Compute the 64-bit SipHash-2-4 of the data written so far. The object remains untouched. */
    uint64_t Finalize() const;
};

/** Optimized SipHash-2-4 implementation for uint256.
 *
 *  It is identical to:
 *    SipHasher(k0, k1)
 *      .Write(val.GetUint64(0))
 *      .Write(val.GetUint64(1))
 *      .Write(val.GetUint64(2))
 *      .Write(val.GetUint64(3))
 *      .Finalize()
 */
uint64_t SipHashUint256(uint64_t k0, uint64_t k1, const uint256& val);
uint64_t SipHashUint256Extra(uint64_t k0, uint64_t k1, const uint256& val, uint32_t extra);

inline int GetHashSelection(const uint256 PrevBlockHash, int index) {
    assert(index >= 0);
    assert(index < 16);

#define START_OF_LAST_16_NIBBLES_OF_HASH 48
    int hashSelection = PrevBlockHash.GetNibble(START_OF_LAST_16_NIBBLES_OF_HASH + index);
    return(hashSelection);
}

extern double algoHashTotal[16];
extern int algoHashHits[16];


template<typename T1>
inline uint256 HashX16R(const T1 pbegin, const T1 pend, const uint256 PrevBlockHash)
{
//	static std::chrono::duration<double>[16];
    int hashSelection;

    sph_blake512_context     ctx_blake;      //0
    sph_bmw512_context       ctx_bmw;        //1
    sph_groestl512_context   ctx_groestl;    //2
    sph_jh512_context        ctx_jh;         //3
    sph_keccak512_context    ctx_keccak;     //4
    sph_skein512_context     ctx_skein;      //5
    sph_luffa512_context     ctx_luffa;      //6
    sph_cubehash512_context  ctx_cubehash;   //7
    sph_shavite512_context   ctx_shavite;    //8
    sph_simd512_context      ctx_simd;       //9
    sph_echo512_context      ctx_echo;       //A
    sph_hamsi512_context     ctx_hamsi;      //B
    sph_fugue512_context     ctx_fugue;      //C
    sph_shabal512_context    ctx_shabal;     //D
    sph_whirlpool_context    ctx_whirlpool;  //E
    sph_sha512_context       ctx_sha512;     //F

    static unsigned char pblank[1];

    uint512 hash[16];

    for (int i=0;i<16;i++)
    {
        const void *toHash;
        int lenToHash;
        if (i == 0) {
            toHash = (pbegin == pend ? pblank : static_cast<const void*>(&pbegin[0]));
            lenToHash = (pend - pbegin) * sizeof(pbegin[0]);
        } else {
            toHash = static_cast<const void*>(&hash[i-1]);
            lenToHash = 64;
        }

        hashSelection = GetHashSelection(PrevBlockHash, i);

        switch(hashSelection) {
            case 0:
                sph_blake512_init(&ctx_blake);
                sph_blake512 (&ctx_blake, toHash, lenToHash);
                sph_blake512_close(&ctx_blake, static_cast<void*>(&hash[i]));
                break;
            case 1:
                sph_bmw512_init(&ctx_bmw);
                sph_bmw512 (&ctx_bmw, toHash, lenToHash);
                sph_bmw512_close(&ctx_bmw, static_cast<void*>(&hash[i]));
                break;
            case 2:
                sph_groestl512_init(&ctx_groestl);
                sph_groestl512 (&ctx_groestl, toHash, lenToHash);
                sph_groestl512_close(&ctx_groestl, static_cast<void*>(&hash[i]));
                break;
            case 3:
                sph_jh512_init(&ctx_jh);
                sph_jh512 (&ctx_jh, toHash, lenToHash);
                sph_jh512_close(&ctx_jh, static_cast<void*>(&hash[i]));
                break;
            case 4:
                sph_keccak512_init(&ctx_keccak);
                sph_keccak512 (&ctx_keccak, toHash, lenToHash);
                sph_keccak512_close(&ctx_keccak, static_cast<void*>(&hash[i]));
                break;
            case 5:
                sph_skein512_init(&ctx_skein);
                sph_skein512 (&ctx_skein, toHash, lenToHash);
                sph_skein512_close(&ctx_skein, static_cast<void*>(&hash[i]));
                break;
            case 6:
                sph_luffa512_init(&ctx_luffa);
                sph_luffa512 (&ctx_luffa, toHash, lenToHash);
                sph_luffa512_close(&ctx_luffa, static_cast<void*>(&hash[i]));
                break;
            case 7:
                sph_cubehash512_init(&ctx_cubehash);
                sph_cubehash512 (&ctx_cubehash, toHash, lenToHash);
                sph_cubehash512_close(&ctx_cubehash, static_cast<void*>(&hash[i]));
                break;
            case 8:
                sph_shavite512_init(&ctx_shavite);
                sph_shavite512(&ctx_shavite, toHash, lenToHash);
                sph_shavite512_close(&ctx_shavite, static_cast<void*>(&hash[i]));
                break;
            case 9:
                sph_simd512_init(&ctx_simd);
                sph_simd512 (&ctx_simd, toHash, lenToHash);
                sph_simd512_close(&ctx_simd, static_cast<void*>(&hash[i]));
                break;
            case 10:
                sph_echo512_init(&ctx_echo);
                sph_echo512 (&ctx_echo, toHash, lenToHash);
                sph_echo512_close(&ctx_echo, static_cast<void*>(&hash[i]));
                break;
            case 11:
                sph_hamsi512_init(&ctx_hamsi);
                sph_hamsi512 (&ctx_hamsi, toHash, lenToHash);
                sph_hamsi512_close(&ctx_hamsi, static_cast<void*>(&hash[i]));
                break;
            case 12:
                sph_fugue512_init(&ctx_fugue);
                sph_fugue512 (&ctx_fugue, toHash, lenToHash);
                sph_fugue512_close(&ctx_fugue, static_cast<void*>(&hash[i]));
                break;
            case 13:
                sph_shabal512_init(&ctx_shabal);
                sph_shabal512 (&ctx_shabal, toHash, lenToHash);
                sph_shabal512_close(&ctx_shabal, static_cast<void*>(&hash[i]));
                break;
            case 14:
                sph_whirlpool_init(&ctx_whirlpool);
                sph_whirlpool(&ctx_whirlpool, toHash, lenToHash);
                sph_whirlpool_close(&ctx_whirlpool, static_cast<void*>(&hash[i]));
                break;
            case 15:
                sph_sha512_init(&ctx_sha512);
                sph_sha512 (&ctx_sha512, toHash, lenToHash);
                sph_sha512_close(&ctx_sha512, static_cast<void*>(&hash[i]));
                break;
        }
    }

    return hash[15].trim256();
}


uint256 ProgPowHash(CBlockHeader& blockHeader);


#endif // BITCOIN_HASH_H
