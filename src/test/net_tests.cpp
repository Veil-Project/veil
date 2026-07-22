// Copyright (c) 2012-2019 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#include <addrman.h>
#include <test/test_veil.h>
#include <string>
#include <boost/test/unit_test.hpp>
#include <hash.h>
#include <cstdint>
#include <serialize.h>
#include <streams.h>
#include <crypto/sha3.h>
#include <net.h>
#include <netbase.h>
#include <netaddress.h>
#include <chainparams.h>
#include <util/strencodings.h>
#include <util/system.h>

#include <memory>

class CAddrManSerializationMock : public CAddrMan
{
public:
    virtual void Serialize(CDataStream& s) const = 0;

    //! Ensure that bucket placement is always the same for testing purposes.
    void MakeDeterministic()
    {
        nKey.SetNull();
        insecure_rand = FastRandomContext(true);
    }
};

class CAddrManUncorrupted : public CAddrManSerializationMock
{
public:
    void Serialize(CDataStream& s) const override
    {
        CAddrMan::Serialize(s);
    }
};

class CAddrManCorrupted : public CAddrManSerializationMock
{
public:
    void Serialize(CDataStream& s) const override
    {
        // Produces corrupt output that claims addrman has 20 addrs when it only has one addr.
        unsigned char nVersion = 1;
        s << nVersion;
        s << ((unsigned char)32);
        s << nKey;
        s << 10; // nNew
        s << 10; // nTried

        int nUBuckets = ADDRMAN_NEW_BUCKET_COUNT ^ (1 << 30);
        s << nUBuckets;

        CService serv;
        Lookup("252.1.1.1", serv, 7777, false);
        CAddress addr = CAddress(serv, NODE_NONE);
        CNetAddr resolved;
        LookupHost("252.2.2.2", resolved, false);
        CAddrInfo info = CAddrInfo(addr, resolved);
        s << info;
    }
};

static CDataStream AddrmanToStream(CAddrManSerializationMock& _addrman)
{
    CDataStream ssPeersIn(SER_DISK, CLIENT_VERSION);
    ssPeersIn << Params().MessageStart();
    ssPeersIn << _addrman;
    std::string str = ssPeersIn.str();
    std::vector<unsigned char> vchData(str.begin(), str.end());
    return CDataStream(vchData, SER_DISK, CLIENT_VERSION);
}

BOOST_FIXTURE_TEST_SUITE(net_tests, BasicTestingSetup)

BOOST_AUTO_TEST_CASE(cnode_listen_port)
{
    // test default
    uint16_t port = GetListenPort();
    BOOST_CHECK(port == Params().GetDefaultPort());
    // test set port
    uint16_t altPort = 12345;
    gArgs.SoftSetArg("-port", std::to_string(altPort));
    port = GetListenPort();
    BOOST_CHECK(port == altPort);
}

BOOST_AUTO_TEST_CASE(caddrdb_read)
{
    SetDataDir("caddrdb_read");
    CAddrManUncorrupted addrmanUncorrupted;
    addrmanUncorrupted.MakeDeterministic();

    CService addr1, addr2, addr3;
    Lookup("250.7.1.1", addr1, 8333, false);
    Lookup("250.7.2.2", addr2, 9999, false);
    Lookup("250.7.3.3", addr3, 9999, false);

    // Add three addresses to new table.
    CService source;
    Lookup("252.5.1.1", source, 8333, false);
    addrmanUncorrupted.Add(CAddress(addr1, NODE_NONE), source);
    addrmanUncorrupted.Add(CAddress(addr2, NODE_NONE), source);
    addrmanUncorrupted.Add(CAddress(addr3, NODE_NONE), source);

    // Test that the de-serialization does not throw an exception.
    CDataStream ssPeers1 = AddrmanToStream(addrmanUncorrupted);
    bool exceptionThrown = false;
    CAddrMan addrman1;

    BOOST_CHECK(addrman1.size() == 0);
    try {
        unsigned char pchMsgTmp[4];
        ssPeers1 >> pchMsgTmp;
        ssPeers1 >> addrman1;
    } catch (const std::exception& e) {
        exceptionThrown = true;
    }

    BOOST_CHECK(addrman1.size() == 3);
    BOOST_CHECK(exceptionThrown == false);

    // Test that CAddrDB::Read creates an addrman with the correct number of addrs.
    CDataStream ssPeers2 = AddrmanToStream(addrmanUncorrupted);

    CAddrMan addrman2;
    CAddrDB adb;
    BOOST_CHECK(addrman2.size() == 0);
    adb.Read(addrman2, ssPeers2);
    BOOST_CHECK(addrman2.size() == 3);
}


BOOST_AUTO_TEST_CASE(caddrdb_read_corrupted)
{
    SetDataDir("caddrdb_read_corrupted");
    CAddrManCorrupted addrmanCorrupted;
    addrmanCorrupted.MakeDeterministic();

    // Test that the de-serialization of corrupted addrman throws an exception.
    CDataStream ssPeers1 = AddrmanToStream(addrmanCorrupted);
    bool exceptionThrown = false;
    CAddrMan addrman1;
    BOOST_CHECK(addrman1.size() == 0);
    try {
        unsigned char pchMsgTmp[4];
        ssPeers1 >> pchMsgTmp;
        ssPeers1 >> addrman1;
    } catch (const std::exception& e) {
        exceptionThrown = true;
    }
    // Even through de-serialization failed addrman is not left in a clean state.
    BOOST_CHECK(addrman1.size() == 1);
    BOOST_CHECK(exceptionThrown);

    // Test that CAddrDB::Read leaves addrman in a clean state if de-serialization fails.
    CDataStream ssPeers2 = AddrmanToStream(addrmanCorrupted);

    CAddrMan addrman2;
    CAddrDB adb;
    BOOST_CHECK(addrman2.size() == 0);
    adb.Read(addrman2, ssPeers2);
    BOOST_CHECK(addrman2.size() == 0);
}

BOOST_AUTO_TEST_CASE(cnode_simple_test)
{
    SOCKET hSocket = INVALID_SOCKET;
    NodeId id = 0;
    int height = 0;

    in_addr ipv4Addr;
    ipv4Addr.s_addr = 0xa0b0c001;

    CAddress addr = CAddress(CService(ipv4Addr, 7777), NODE_NETWORK);
    std::string pszDest;
    bool fInboundIn = false;

    // Test that fFeeler is false by default.
    std::unique_ptr<CNode> pnode1(new CNode(id++, NODE_NETWORK, height, hSocket, addr, 0, 0, CAddress(), pszDest, fInboundIn));
    BOOST_CHECK(pnode1->fInbound == false);
    BOOST_CHECK(pnode1->fFeeler == false);

    fInboundIn = true;
    std::unique_ptr<CNode> pnode2(new CNode(id++, NODE_NETWORK, height, hSocket, addr, 1, 1, CAddress(), pszDest, fInboundIn));
    BOOST_CHECK(pnode2->fInbound == true);
    BOOST_CHECK(pnode2->fFeeler == false);
}

// Locks the legacy v1 (non-addrv2) address serialization to a fixed 16 raw
// bytes with no length prefix. This guards the prevector-based m_addr storage
// against accidentally emitting a CompactSize-prefixed vector, which would
// break peers.dat and v1 wire compatibility.
BOOST_AUTO_TEST_CASE(cnetaddr_serialize_v1_bytes)
{
    struct in6_addr raw;
    for (int i = 0; i < 16; ++i) reinterpret_cast<uint8_t*>(&raw)[i] = static_cast<uint8_t>(i + 1);
    CNetAddr netaddr(raw);

    CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
    ss << netaddr;
    BOOST_CHECK_EQUAL(HexStr(ss), "0102030405060708090a0b0c0d0e0f10");

    // CService appends a big-endian port; here 0x208D = 8333.
    CService svc(netaddr, 8333);
    CDataStream ss2(SER_NETWORK, PROTOCOL_VERSION);
    ss2 << svc;
    BOOST_CHECK_EQUAL(HexStr(ss2), "0102030405060708090a0b0c0d0e0f10208d");

    // Round-trip must reproduce the same address.
    CNetAddr back;
    ss >> back;
    BOOST_CHECK(back == netaddr);
}

// BIP155 (addrv2) encode/decode vectors, exercised on an ADDRV2_FORMAT stream.
// Confirms the network-id + CompactSize-length + raw-bytes wire form and that a
// decoded Tor v3 address classifies correctly, while the legacy (non-addrv2)
// encoding is untouched.
BOOST_AUTO_TEST_CASE(bip155_encode_decode)
{
    // IPv4 1.2.3.4 -> net id 0x01, len 0x04, four address bytes.
    struct in_addr a4;
    uint8_t v4[4] = {1, 2, 3, 4};
    memcpy(&a4.s_addr, v4, 4);
    CNetAddr n4(a4);
    CDataStream s4(SER_NETWORK, PROTOCOL_VERSION | ADDRV2_FORMAT);
    s4 << n4;
    BOOST_CHECK_EQUAL(HexStr(s4), "010401020304");
    CNetAddr back4;
    s4 >> back4;
    BOOST_CHECK(back4 == n4);
    BOOST_CHECK(back4.IsIPv4());

    // IPv6 -> net id 0x02, len 0x10, sixteen address bytes.
    struct in6_addr a6;
    for (int i = 0; i < 16; ++i) reinterpret_cast<uint8_t*>(&a6)[i] = static_cast<uint8_t>(i + 1);
    CNetAddr n6(a6);
    CDataStream s6(SER_NETWORK, PROTOCOL_VERSION | ADDRV2_FORMAT);
    s6 << n6;
    BOOST_CHECK_EQUAL(HexStr(s6), "0210" "0102030405060708090a0b0c0d0e0f10");
    CNetAddr back6;
    s6 >> back6;
    BOOST_CHECK(back6 == n6);
    BOOST_CHECK(back6.IsIPv6());

    // Tor v3: decode a BIP155 blob (net id 0x04, len 0x20, 32-byte pubkey),
    // check it classifies as a routable onion address, then re-encode.
    std::vector<uint8_t> v3(32);
    for (int i = 0; i < 32; ++i) v3[i] = static_cast<uint8_t>(i + 1);
    CDataStream s3(SER_NETWORK, PROTOCOL_VERSION | ADDRV2_FORMAT);
    ser_writedata8(s3, BIP155_NET_TORV3);
    WriteCompactSize(s3, v3.size());
    s3.write(reinterpret_cast<const char*>(v3.data()), v3.size());
    CNetAddr n3;
    s3 >> n3;
    BOOST_CHECK(n3.IsTor());
    BOOST_CHECK(n3.GetNetwork() == NET_ONION);
    BOOST_CHECK(n3.IsRoutable());
    BOOST_CHECK(!n3.IsIPv4());
    BOOST_CHECK(!n3.IsIPv6());
    CDataStream s3b(SER_NETWORK, PROTOCOL_VERSION | ADDRV2_FORMAT);
    s3b << n3;
    BOOST_CHECK_EQUAL(HexStr(s3b), "0420" + HexStr(v3.begin(), v3.end()));

    // An unknown network id is consumed but yields an invalid address.
    CDataStream su(SER_NETWORK, PROTOCOL_VERSION | ADDRV2_FORMAT);
    ser_writedata8(su, 99);
    WriteCompactSize(su, 4);
    su.write("\x01\x02\x03\x04", 4);
    CNetAddr nu;
    su >> nu;
    BOOST_CHECK(!nu.IsValid());

    // Legacy (non-addrv2) encoding is unchanged: still exactly 16 raw bytes.
    CDataStream sv1(SER_NETWORK, PROTOCOL_VERSION);
    sv1 << n6;
    BOOST_CHECK_EQUAL(HexStr(sv1), "0102030405060708090a0b0c0d0e0f10");
}

// A CAddress round-trips through the addrv2 (BIP155) format, where services are
// a CompactSize and the address uses the BIP155 encoding; the legacy format is
// unchanged. This is the wire format the addrv2 message relay (2B.3b) will use.
BOOST_AUTO_TEST_CASE(caddress_addrv2_roundtrip)
{
    struct in6_addr a6;
    for (int i = 0; i < 16; ++i) reinterpret_cast<uint8_t*>(&a6)[i] = static_cast<uint8_t>(i + 1);
    CAddress addr(CService(CNetAddr(a6), 8333), NODE_NETWORK);
    addr.nTime = 0x11223344;

    // addrv2 format: time(4) + services(CompactSize) + BIP155 addr(net+len+bytes) + port.
    CDataStream s2(SER_NETWORK, PROTOCOL_VERSION | ADDRV2_FORMAT);
    s2 << addr;
    BOOST_CHECK_EQUAL(HexStr(s2),
        "44332211"                               // nTime, little-endian
        "01"                                     // services CompactSize (NODE_NETWORK)
        "02" "10" "0102030405060708090a0b0c0d0e0f10"  // net id 2, len 16, IPv6 bytes
        "208d");                                 // port 8333 big-endian
    CAddress back2;
    s2 >> back2;
    BOOST_CHECK(back2 == addr);                  // CService operator== (addr + port)
    BOOST_CHECK(back2.nServices == addr.nServices);
    BOOST_CHECK(back2.nTime == addr.nTime);

    // Legacy format still round-trips (services as uint64).
    CDataStream s1(SER_NETWORK, PROTOCOL_VERSION);
    s1 << addr;
    CAddress back1;
    s1 >> back1;
    BOOST_CHECK(back1 == addr);
    BOOST_CHECK(back1.nServices == addr.nServices);
}

// Internal (DNS-seed placeholder) addresses have no BIP155 network id; they
// are embedded as IPv6 in addrv2 streams (as upstream does) and classify back
// via their prefix, so they survive a peers.dat v2 round-trip.
BOOST_AUTO_TEST_CASE(bip155_internal_roundtrip)
{
    CNetAddr internal;
    BOOST_CHECK(internal.SetInternal("dnsseed.example.com"));
    BOOST_CHECK(internal.IsInternal());

    CDataStream s(SER_NETWORK, PROTOCOL_VERSION | ADDRV2_FORMAT);
    s << internal;
    CNetAddr back;
    s >> back;
    BOOST_CHECK(back.IsInternal());
    BOOST_CHECK(back == internal);
    BOOST_CHECK_EQUAL(back.ToStringIP(), internal.ToStringIP());
}

// SHA3-256 known-answer (NIST): SHA3-256("") == a7ffc6f8...434a. Validates the
// ported hash that the Tor v3 checksum relies on.
BOOST_AUTO_TEST_CASE(sha3_256_empty)
{
    unsigned char out[SHA3_256::OUTPUT_SIZE];
    SHA3_256().Finalize(MakeSpan(out));
    BOOST_CHECK_EQUAL(HexStr(out, out + sizeof(out)),
        "a7ffc6f8bf1ed76651c14756a061d662f580ff4de43b49fa82d80a4b80f8434a");
}

// Tor v3: a 32-byte pubkey decoded from BIP155 renders to a .onion string (with
// the SHA3-256 checksum) and re-parses via SetSpecial back to the same pubkey,
// and a corrupted checksum is rejected.
BOOST_AUTO_TEST_CASE(torv3_onion_roundtrip)
{
    std::vector<uint8_t> pubkey(32);
    for (int i = 0; i < 32; ++i) pubkey[i] = static_cast<uint8_t>(0x80 + i);
    CDataStream s(SER_NETWORK, PROTOCOL_VERSION | ADDRV2_FORMAT);
    ser_writedata8(s, BIP155_NET_TORV3);
    WriteCompactSize(s, pubkey.size());
    s.write(reinterpret_cast<const char*>(pubkey.data()), pubkey.size());
    CNetAddr v3;
    s >> v3;
    BOOST_CHECK(v3.IsTor());

    const std::string onion = v3.ToStringIP();
    BOOST_CHECK(onion.size() == 62 && onion.substr(onion.size() - 6) == ".onion"); // 56 + ".onion"

    CNetAddr reparsed;
    BOOST_CHECK(reparsed.SetSpecial(onion));   // validates version + SHA3 checksum
    BOOST_CHECK(reparsed == v3);
    BOOST_CHECK(reparsed.GetNetwork() == NET_ONION);

    // Flip a character in the pubkey portion: checksum no longer matches.
    std::string corrupt = onion;
    corrupt[0] = (corrupt[0] == 'a') ? 'b' : 'a';
    CNetAddr bad;
    BOOST_CHECK(!bad.SetSpecial(corrupt));
}

BOOST_AUTO_TEST_SUITE_END()
