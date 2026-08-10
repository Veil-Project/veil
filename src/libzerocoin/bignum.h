// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2012 The Bitcoin developers
// Copyright (c) 2017-2019 The PIVX developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#ifndef BITCOIN_BIGNUM_H
#define BITCOIN_BIGNUM_H

#if defined HAVE_CONFIG_H
#include "veil-config.h"
#endif

#include <climits>
#include <stdexcept>
#include <vector>
#if defined(USE_NUM_GMP)
#include <gmp.h>
#endif

#include "serialize.h"
#include "arith_uint256.h"
#include "uint256.h"
#include "version.h"
#include "random.h"

/** Errors thrown by the bignum class */
class bignum_error : public std::runtime_error
{
public:
    explicit bignum_error(const std::string& str) : std::runtime_error(str) {}
};

#if defined(USE_NUM_GMP)
/** C++ wrapper for BIGNUM (Gmp bignum) */
class CBigNum
{
    mpz_t bn;
public:
    CBigNum()
    {
        mpz_init(bn);
    }

    CBigNum(const CBigNum& b)
    {
        mpz_init(bn);
        mpz_set(bn, b.bn);
    }

    CBigNum& operator=(const CBigNum& b)
    {
        mpz_set(bn, b.bn);
        return (*this);
    }

    ~CBigNum()
    {
        mpz_clear(bn);
    }

    //CBigNum(char n) is not portable.  Use 'signed char' or 'unsigned char'.
    CBigNum(signed char n)      { mpz_init(bn); if (n >= 0) mpz_set_ui(bn, n); else mpz_set_si(bn, n); }
    CBigNum(short n)            { mpz_init(bn); if (n >= 0) mpz_set_ui(bn, n); else mpz_set_si(bn, n); }
    CBigNum(int n)              { mpz_init(bn); if (n >= 0) mpz_set_ui(bn, n); else mpz_set_si(bn, n); }
    CBigNum(long n)             { mpz_init(bn); if (n >= 0) mpz_set_ui(bn, n); else mpz_set_si(bn, n); }
    CBigNum(long long n)        { mpz_init(bn); mpz_set_si(bn, n); }
    CBigNum(unsigned char n)    { mpz_init(bn); mpz_set_ui(bn, n); }
    CBigNum(unsigned short n)   { mpz_init(bn); mpz_set_ui(bn, n); }
    CBigNum(unsigned int n)     { mpz_init(bn); mpz_set_ui(bn, n); }
    CBigNum(unsigned long n)    { mpz_init(bn); mpz_set_ui(bn, n); }

    explicit CBigNum(uint256 n) { mpz_init(bn); setuint256(n); }
    explicit CBigNum(arith_uint256 n) { mpz_init(bn); setarith_256(n); }

    explicit CBigNum(const std::vector<unsigned char>& vch)
    {
        mpz_init(bn);
        setvch(vch);
    }

    /** PRNGs use OpenSSL for consistency with seed initialization **/

    /** Generates a cryptographically secure random number between zero and range exclusive
    * i.e. 0 < returned number < range
    * (returns 0 if range = 0 or 1)
    * @param range The upper bound on the number.
    * @return
    */
    static CBigNum randBignum(const CBigNum& range) {
        if (range < 2)
            return 0;

        size_t size = (mpz_sizeinbase (range.bn, 2) + CHAR_BIT-1) / CHAR_BIT;

        // Upstream removed RandAddSeed() with the OpenSSL RNG, and the
        // post-0.20 GetRandBytes/GetStrongRandBytes are capped at 32 bytes
        // per call. Upstream's idiom for bulk randomness is a fresh
        // FastRandomContext: seeded with 256 bits from the global CSPRNG,
        // expanded by ChaCha20 — the same pool-expansion structure the old
        // OpenSSL RAND_bytes had. Distribution is unchanged: uniform bytes.
        FastRandomContext rng;
        std::vector<unsigned char> buf = rng.randbytes(size);

        CBigNum ret(buf);
        if (ret < 0)
            mpz_neg(ret.bn, ret.bn);
        return 1 + (ret % (range-1));
    }

    /** Generates a cryptographically secure random k-bit number
    * @param k The bit length of the number.
    * @return
    */
    static CBigNum randKBitBignum(const uint32_t k){
        FastRandomContext rng;
        std::vector<unsigned char> buf = rng.randbytes((k+7)/8);

        CBigNum ret(buf);
        if (ret < 0)
            mpz_neg(ret.bn, ret.bn);
        return ret % (CBigNum(1) << k);
    }

    /**Returns the size in bits of the underlying bignum.
     *
     * @return the size
     */
    int bitSize() const{
        return  mpz_sizeinbase(bn, 2);
    }

    void setulong(unsigned long n)
    {
        mpz_set_ui(bn, n);
    }

    unsigned long getulong() const
    {
        return mpz_get_ui(bn);
    }

    unsigned int getuint() const
    {
        return mpz_get_ui(bn);
    }

    int getint() const
    {
        unsigned long n = getulong();
        if (mpz_cmp(bn, CBigNum(0).bn) >= 0) {
            return (n > (unsigned long)std::numeric_limits<int>::max() ? std::numeric_limits<int>::max() : n);
        } else {
            return (n > (unsigned long)std::numeric_limits<int>::max() ? std::numeric_limits<int>::min() : -(int)n);
        }
    }

    void setuint256(uint256 n)
    {
        mpz_import(bn, n.size(), -1, 1, 0, 0, (unsigned char*)&n);
    }

    void setarith_256(arith_uint256 n)
    {
        setuint256(ArithToUint256(n));
    }

    uint256 getuint256() const
    {
        uint256 n = uint256();
        if (bitSize() > 256) {
            return MaxUint256();
        }

        mpz_export((unsigned char*)&n, NULL, -1, 1, 0, 0, bn);
        return n;
    }

    arith_uint256 getarith_uint256() const
    {
        auto n = getuint256();
        return UintToArith256(n);
    }

    void setvch(const std::vector<unsigned char>& vch)
    {
        std::vector<unsigned char> vch2 = vch;
        unsigned char sign = 0;
        if (vch2.size() > 0) {
            sign = vch2[vch2.size()-1] & 0x80;
            vch2[vch2.size()-1] = vch2[vch2.size()-1] & 0x7f;
            mpz_import(bn, vch2.size(), -1, 1, 0, 0, &vch2[0]);
            if (sign)
                mpz_neg(bn, bn);
        }
        else {
            mpz_set_si(bn, 0);
        }
    }

    std::vector<unsigned char> getvch() const
    {
        if (mpz_cmp(bn, CBigNum(0).bn) == 0) {
            return std::vector<unsigned char>(0);
        }
        size_t size = (mpz_sizeinbase (bn, 2) + CHAR_BIT-1) / CHAR_BIT;
        if (size <= 0)
            return std::vector<unsigned char>();
        std::vector<unsigned char> v(size + 1);
        mpz_export(&v[0], &size, -1, 1, 0, 0, bn);
        if (v[v.size()-2] & 0x80) {
            if (mpz_sgn(bn)<0) {
                v[v.size()-1] = 0x80;
            } else {
                v[v.size()-1] = 0x00;
            }
        } else {
            v.pop_back();
            if (mpz_sgn(bn)<0) {
                v[v.size()-1] |= 0x80;
            }
        }
        return v;
    }

    void SetDec(const std::string& str)
    {
        const char* psz = str.c_str();
        mpz_set_str(bn, psz, 10);
    }

    void SetHex(const std::string& str)
    {
        SetHexBool(str);
    }

    bool SetHexBool(const std::string& str)
    {
        const char* psz = str.c_str();
        int ret = 1 + mpz_set_str(bn, psz, 16);
        return (bool) ret;
    }

    std::string ToString(int nBase=10) const
    {
        char* c_str = mpz_get_str(NULL, nBase, bn);
        std::string str(c_str);
        return str;
    }

    std::string GetHex() const
    {
        return ToString(16);
    }

    std::string GetDec() const
    {
        return ToString(10);
    }

    unsigned int GetSerializeSize(int nType=0) const
    {
        return ::GetSerializeSize(getvch(), nType);
    }

    template<typename Stream>
    void Serialize(Stream& s, int nType=0) const
    {
        ::Serialize(s, getvch());
    }

    template<typename Stream>
    void Unserialize(Stream& s, int nType=0)
    {
        std::vector<unsigned char> vch;
        ::Unserialize(s, vch);
        setvch(vch);
    }

    /**
        * exponentiation with an int. this^e
        * @param e the exponent as an int
        * @return
        */
    CBigNum pow(const int e) const {
        return this->pow(CBigNum(e));
    }

    /**
     * exponentiation this^e
     * @param e the exponent
     * @return
     */
    CBigNum pow(const CBigNum& e) const {
        CBigNum ret;
        long unsigned int ei = mpz_get_ui (e.bn);
        mpz_pow_ui(ret.bn, bn, ei);
        return ret;
    }

    /**
     * modular multiplication: (this * b) mod m
     * @param b operand
     * @param m modulus
     */
    CBigNum mul_mod(const CBigNum& b, const CBigNum& m) const {
        CBigNum ret;
        mpz_mul (ret.bn, bn, b.bn);
        mpz_mod (ret.bn, ret.bn, m.bn);
        return ret;
    }

    /**
     * modular exponentiation: this^e mod n
     * @param e exponent
     * @param m modulus
     */
    CBigNum pow_mod(const CBigNum& e, const CBigNum& m) const {
        CBigNum ret;
        if (e > CBigNum(0) && mpz_odd_p(m.bn))
            mpz_powm_sec (ret.bn, bn, e.bn, m.bn);
        else
            mpz_powm (ret.bn, bn, e.bn, m.bn);
        return ret;
    }

   /**
    * Calculates the inverse of this element mod m.
    * i.e. i such this*i = 1 mod m
    * @param m the modu
    * @return the inverse
    */
    CBigNum inverse(const CBigNum& m) const {
        CBigNum ret;
        mpz_invert(ret.bn, bn, m.bn);
        return ret;
    }

    /**
     * Generates a random (safe) prime of numBits bits
     * @param numBits the number of bits
     * @param safe true for a safe prime
     * @return the prime
     */
    static CBigNum generatePrime(const unsigned int numBits, bool safe = false) {
        CBigNum rand = randKBitBignum(numBits);
        CBigNum prime;
        mpz_nextprime(prime.bn, rand.bn);
        return prime;
    }

    /**
     * Calculates the greatest common divisor (GCD) of two numbers.
     * @param m the second element
     * @return the GCD
     */
    CBigNum gcd( const CBigNum& b) const{
        CBigNum ret;
        mpz_gcd(ret.bn, bn, b.bn);
        return ret;
    }

   /**
    * Miller-Rabin primality test on this element
    * @param checks: optional, the number of Miller-Rabin tests to run
    *               default causes error rate of 2^-80.
    * @return true if prime
    */
    bool isPrime(const int checks=15) const {
        int ret = mpz_probab_prime_p(bn, checks);
        return ret;
    }

    bool isOne() const
    {
        return mpz_cmp(bn, CBigNum(1).bn) == 0;
    }

    bool operator!() const
    {
        return mpz_cmp(bn, CBigNum(0).bn) == 0;
    }

    CBigNum& operator+=(const CBigNum& b)
    {
        mpz_add(bn, bn, b.bn);
        return *this;
    }

    CBigNum& operator-=(const CBigNum& b)
    {
        mpz_sub(bn, bn, b.bn);
        return *this;
    }

    CBigNum& operator*=(const CBigNum& b)
    {
        mpz_mul(bn, bn, b.bn);
        return *this;
    }

    CBigNum& operator/=(const CBigNum& b)
    {
        *this = *this / b;
        return *this;
    }

    CBigNum& operator%=(const CBigNum& b)
    {
        *this = *this % b;
        return *this;
    }

    CBigNum& operator<<=(unsigned int shift)
    {
        mpz_mul_2exp(bn, bn, shift);
        return *this;
    }

    CBigNum& operator>>=(unsigned int shift)
    {
        mpz_div_2exp(bn, bn, shift);
        return *this;
    }


    CBigNum& operator++()
    {
        // prefix operator
        mpz_add(bn, bn, CBigNum(1).bn);
        return *this;
    }

    const CBigNum operator++(int)
    {
        // postfix operator
        const CBigNum ret = *this;
        ++(*this);
        return ret;
    }

    CBigNum& operator--()
    {
        // prefix operator
        mpz_sub(bn, bn, CBigNum(1).bn);
        return *this;
    }

    const CBigNum operator--(int)
    {
        // postfix operator
        const CBigNum ret = *this;
        --(*this);
        return ret;
    }

    friend inline const CBigNum operator+(const CBigNum& a, const CBigNum& b);
    friend inline const CBigNum operator-(const CBigNum& a, const CBigNum& b);
    friend inline const CBigNum operator/(const CBigNum& a, const CBigNum& b);
    friend inline const CBigNum operator%(const CBigNum& a, const CBigNum& b);
    friend inline const CBigNum operator*(const CBigNum& a, const CBigNum& b);
    friend inline const CBigNum operator<<(const CBigNum& a, unsigned int shift);
    friend inline const CBigNum operator-(const CBigNum& a);
    friend inline bool operator==(const CBigNum& a, const CBigNum& b);
    friend inline bool operator!=(const CBigNum& a, const CBigNum& b);
    friend inline bool operator<=(const CBigNum& a, const CBigNum& b);
    friend inline bool operator>=(const CBigNum& a, const CBigNum& b);
    friend inline bool operator<(const CBigNum& a, const CBigNum& b);
    friend inline bool operator>(const CBigNum& a, const CBigNum& b);
};

inline const CBigNum operator+(const CBigNum& a, const CBigNum& b)
{
    CBigNum r;
    mpz_add(r.bn, a.bn, b.bn);
    return r;
}

inline const CBigNum operator-(const CBigNum& a, const CBigNum& b)
{
    CBigNum r;
    mpz_sub(r.bn, a.bn, b.bn);
    return r;
}

inline const CBigNum operator-(const CBigNum& a)
{
    CBigNum r;
    mpz_neg(r.bn, a.bn);
    return r;
}

inline const CBigNum operator*(const CBigNum& a, const CBigNum& b)
{
    CBigNum r;
    mpz_mul(r.bn, a.bn, b.bn);
    return r;
}

inline const CBigNum operator/(const CBigNum& a, const CBigNum& b)
{
    CBigNum r;
    mpz_tdiv_q(r.bn, a.bn, b.bn);
    return r;
}

inline const CBigNum operator%(const CBigNum& a, const CBigNum& b)
{
    CBigNum r;
    mpz_mmod(r.bn, a.bn, b.bn);
    return r;
}

inline const CBigNum operator<<(const CBigNum& a, unsigned int shift)
{
    CBigNum r;
    mpz_mul_2exp(r.bn, a.bn, shift);
    return r;
}

inline const CBigNum operator>>(const CBigNum& a, unsigned int shift)
{
    CBigNum r = a;
    r >>= shift;
    return r;
}

inline bool operator==(const CBigNum& a, const CBigNum& b) { return (mpz_cmp(a.bn, b.bn) == 0); }
inline bool operator!=(const CBigNum& a, const CBigNum& b) { return (mpz_cmp(a.bn, b.bn) != 0); }
inline bool operator<=(const CBigNum& a, const CBigNum& b) { return (mpz_cmp(a.bn, b.bn) <= 0); }
inline bool operator>=(const CBigNum& a, const CBigNum& b) { return (mpz_cmp(a.bn, b.bn) >= 0); }
inline bool operator<(const CBigNum& a, const CBigNum& b)  { return (mpz_cmp(a.bn, b.bn) < 0); }
inline bool operator>(const CBigNum& a, const CBigNum& b)  { return (mpz_cmp(a.bn, b.bn) > 0); }
inline std::ostream& operator<<(std::ostream &strm, const CBigNum &b) { return strm << b.ToString(10); }
#endif

typedef CBigNum Bignum;


#endif
