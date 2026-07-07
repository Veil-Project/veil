// Copyright (c) 2009-2019 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_NETADDRESS_H
#define BITCOIN_NETADDRESS_H

#if defined(HAVE_CONFIG_H)
#include <config/veil-config.h>
#endif

#include <compat.h>
#include <prevector.h>
#include <serialize.h>
#include <span.h>

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

enum Network
{
    NET_UNROUTABLE = 0,
    NET_IPV4,
    NET_IPV6,
    NET_ONION,
    NET_INTERNAL,

    NET_MAX,
};

// --- BIP155 (addrv2) scaffolding ---------------------------------------------
// Foundational declarations for Tor v3 / addrv2 support. Not yet wired into
// CNetAddr's storage or serialization: the representation rewrite
// (ip[16] -> prevector m_addr) and the ADDRV2_FORMAT-gated serialization land
// in subsequent, independently-gated commits. Kept deliberately additive so
// this commit changes no behavior (NET_MAX and existing paths are untouched).

/** Sentinel bit in the serialize version, set only when (de)serializing an
 *  address in the BIP155 (addrv2) format. Default streams keep the legacy
 *  16-byte encoding, so peers.dat and v1 wire stay byte-identical. */
static constexpr int ADDRV2_FORMAT = 0x20000000;

/** BIP155 network ids as they appear on the wire in addrv2 messages.
 *  Distinct from the internal `enum Network` above. */
enum BIP155Network : uint8_t {
    BIP155_NET_IPV4 = 1,
    BIP155_NET_IPV6 = 2,
    BIP155_NET_TORV2 = 3,
    BIP155_NET_TORV3 = 4,
    BIP155_NET_I2P = 5,
    BIP155_NET_CJDNS = 6,
};

/** Size (in bytes) of the raw address for each supported network. */
static constexpr size_t ADDR_IPV4_SIZE = 4;
static constexpr size_t ADDR_IPV6_SIZE = 16;
static constexpr size_t ADDR_TORV2_SIZE = 10;
static constexpr size_t ADDR_TORV3_SIZE = 32;
static constexpr size_t ADDR_I2P_SIZE = 32;
static constexpr size_t ADDR_CJDNS_SIZE = 16;
static constexpr size_t ADDR_INTERNAL_SIZE = 10;

/** IP address (IPv6, or IPv4 using mapped IPv6 range (::FFFF:0:0/96)) */
class CNetAddr
{
    protected:
        // Raw representation of the network address. Held at ADDR_IPV6_SIZE (16)
        // bytes for every address type supported today (IPv4/IPv6 use the
        // IPv6-mapped form, Tor v2 uses onioncat, internal uses the veil
        // prefix), so all classification, GetGroup and serialization behave
        // exactly as the previous fixed `unsigned char ip[16]`. Storing it in a
        // prevector is the prerequisite for holding longer addresses (Tor v3 =
        // 32 bytes) additively, without disturbing the 16-byte paths.
        prevector<16, uint8_t> m_addr{ADDR_IPV6_SIZE, 0x0};
        uint32_t scopeId; // for scoped/link-local ipv6 addresses

    public:
        CNetAddr();
        explicit CNetAddr(const struct in_addr& ipv4Addr);
        void SetIP(const CNetAddr& ip);

    private:
        /**
         * Set raw IPv4 or IPv6 address (in network byte order)
         * @note Only NET_IPV4 and NET_IPV6 are allowed for network.
         */
        void SetRaw(Network network, const uint8_t *data);

    public:
        /**
          * Transform an arbitrary string into a non-routable ipv6 address.
          * Useful for mapping resolved addresses back to their source.
         */
        bool SetInternal(const std::string& name);

        bool SetSpecial(const std::string &strName); // for Tor addresses
        bool IsIPv4() const;    // IPv4 mapped address (::FFFF:0:0/96, 0.0.0.0/0)
        bool IsIPv6() const;    // IPv6 address (not mapped IPv4, not Tor)
        bool IsRFC1918() const; // IPv4 private networks (10.0.0.0/8, 192.168.0.0/16, 172.16.0.0/12)
        bool IsRFC2544() const; // IPv4 inter-network communications (192.18.0.0/15)
        bool IsRFC6598() const; // IPv4 ISP-level NAT (100.64.0.0/10)
        bool IsRFC5737() const; // IPv4 documentation addresses (192.0.2.0/24, 198.51.100.0/24, 203.0.113.0/24)
        bool IsRFC3849() const; // IPv6 documentation address (2001:0DB8::/32)
        bool IsRFC3927() const; // IPv4 autoconfig (169.254.0.0/16)
        bool IsRFC3964() const; // IPv6 6to4 tunnelling (2002::/16)
        bool IsRFC4193() const; // IPv6 unique local (FC00::/7)
        bool IsRFC4380() const; // IPv6 Teredo tunnelling (2001::/32)
        bool IsRFC4843() const; // IPv6 ORCHID (2001:10::/28)
        bool IsRFC4862() const; // IPv6 autoconfig (FE80::/64)
        bool IsRFC6052() const; // IPv6 well-known prefix (64:FF9B::/96)
        bool IsRFC6145() const; // IPv6 IPv4-translated address (::FFFF:0:0:0/96)
        bool IsTor() const;
        bool IsLocal() const;
        bool IsRoutable() const;
        bool IsInternal() const;
        bool IsValid() const;
        enum Network GetNetwork() const;
        std::string ToString() const;
        std::string ToStringIP() const;
        unsigned int GetByte(int n) const;
        uint64_t GetHash() const;
        bool GetInAddr(struct in_addr* pipv4Addr) const;
        std::vector<unsigned char> GetGroup() const;
        int GetReachabilityFrom(const CNetAddr *paddrPartner = nullptr) const;

        explicit CNetAddr(const struct in6_addr& pipv6Addr, const uint32_t scope = 0);
        bool GetIn6Addr(struct in6_addr* pipv6Addr) const;

        friend bool operator==(const CNetAddr& a, const CNetAddr& b);
        friend bool operator!=(const CNetAddr& a, const CNetAddr& b) { return !(a == b); }
        friend bool operator<(const CNetAddr& a, const CNetAddr& b);

        ADD_SERIALIZE_METHODS;

        template <typename Stream, typename Operation>
        inline void SerializationOp(Stream& s, Operation ser_action) {
            // Legacy fixed 16-byte encoding, byte-identical to the previous
            // `unsigned char ip[16]`. m_addr is invariant at ADDR_IPV6_SIZE here.
            unsigned char legacy_ip[ADDR_IPV6_SIZE];
            if (!ser_action.ForRead()) {
                assert(m_addr.size() == ADDR_IPV6_SIZE);
                memcpy(legacy_ip, m_addr.data(), ADDR_IPV6_SIZE);
            }
            READWRITE(legacy_ip);
            if (ser_action.ForRead()) {
                m_addr.assign(legacy_ip, legacy_ip + ADDR_IPV6_SIZE);
            }
        }

        friend class CSubNet;
};

class CSubNet
{
    protected:
        /// Network (base) address
        CNetAddr network;
        /// Netmask, in network byte order
        uint8_t netmask[16];
        /// Is this value valid? (only used to signal parse errors)
        bool valid;

    public:
        CSubNet();
        CSubNet(const CNetAddr &addr, int32_t mask);
        CSubNet(const CNetAddr &addr, const CNetAddr &mask);

        //constructor for single ip subnet (<ipv4>/32 or <ipv6>/128)
        explicit CSubNet(const CNetAddr &addr);

        bool Match(const CNetAddr &addr) const;

        std::string ToString() const;
        bool IsValid() const;

        friend bool operator==(const CSubNet& a, const CSubNet& b);
        friend bool operator!=(const CSubNet& a, const CSubNet& b) { return !(a == b); }
        friend bool operator<(const CSubNet& a, const CSubNet& b);

        ADD_SERIALIZE_METHODS;

        template <typename Stream, typename Operation>
        inline void SerializationOp(Stream& s, Operation ser_action) {
            READWRITE(network);
            READWRITE(netmask);
            READWRITE(valid);
        }
};

/** A combination of a network address (CNetAddr) and a (TCP) port */
class CService : public CNetAddr
{
    protected:
        uint16_t port; // host order

    public:
        CService();
        CService(const CNetAddr& ip, uint16_t port);
        CService(const struct in_addr& ipv4Addr, uint16_t port);
        explicit CService(const struct sockaddr_in& addr);
        uint16_t GetPort() const;
        bool GetSockAddr(struct sockaddr* paddr, socklen_t *addrlen) const;
        bool SetSockAddr(const struct sockaddr* paddr);
        friend bool operator==(const CService& a, const CService& b);
        friend bool operator!=(const CService& a, const CService& b) { return !(a == b); }
        friend bool operator<(const CService& a, const CService& b);
        std::vector<unsigned char> GetKey() const;
        std::string ToString() const;
        std::string ToStringPort() const;
        std::string ToStringIPPort() const;

        CService(const struct in6_addr& ipv6Addr, uint16_t port);
        explicit CService(const struct sockaddr_in6& addr);

        ADD_SERIALIZE_METHODS;

        template <typename Stream, typename Operation>
        inline void SerializationOp(Stream& s, Operation ser_action) {
            // Legacy fixed 16-byte address + big-endian port, byte-identical to
            // the previous `unsigned char ip[16]` encoding.
            unsigned char legacy_ip[ADDR_IPV6_SIZE];
            if (!ser_action.ForRead()) {
                assert(m_addr.size() == ADDR_IPV6_SIZE);
                memcpy(legacy_ip, m_addr.data(), ADDR_IPV6_SIZE);
            }
            READWRITE(legacy_ip);
            if (ser_action.ForRead()) {
                m_addr.assign(legacy_ip, legacy_ip + ADDR_IPV6_SIZE);
            }
            READWRITE(WrapBigEndian(port));
        }
};

#endif // BITCOIN_NETADDRESS_H
