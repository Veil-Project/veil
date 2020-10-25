// Copyright (c) 2017-2019 The Particl Core developers
// Copyright (c) 2018-2019 Veil developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <veil/ringct/anonwalletdb.h>
#include <veil/ringct/anonwallet.h>

#include <serialize.h>

class PackKey
{
public:
    PackKey(std::string s, const CKeyID &keyId, uint32_t nPack)
        : m_prefix(s), m_keyId(keyId), m_nPack(nPack) { };

    std::string m_prefix;
    CKeyID m_keyId;
    uint32_t m_nPack;

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action)
    {
        READWRITE(m_prefix);
        READWRITE(m_keyId);
        READWRITE(m_nPack);
    }
};

bool AnonWalletDB::WriteStealthKeyMeta(const CKeyID &keyId, const CStealthKeyMetadata &sxKeyMeta)
{
    return WriteIC(std::make_pair(std::string("sxkm"), keyId), sxKeyMeta, true);
}

bool AnonWalletDB::EraseStealthKeyMeta(const CKeyID &keyId)
{
    return EraseIC(std::make_pair(std::string("sxkm"), keyId));
}


bool AnonWalletDB::WriteStealthAddress(const CStealthAddress &sxAddr)
{
    return WriteIC(std::make_pair(std::string("sxad"), sxAddr.scan_pubkey), sxAddr, true);
}

bool AnonWalletDB::WriteStealthDestinationMeta(const CKeyID& idStealthDestination, const std::vector<uint8_t>& vchEphemPK)
{
    return WriteIC(std::make_pair(std::string("sdmeta"), idStealthDestination), vchEphemPK, true);
}

bool AnonWalletDB::ReadStealthDestinationMeta(const CKeyID& idStealthDestination, std::vector<uint8_t>& vchEphemPK)
{
    return m_batch.Read(std::make_pair(std::string("sdmeta"), idStealthDestination), vchEphemPK);
}

bool AnonWalletDB::ReadStealthAddress(CStealthAddress& sxAddr)
{
    // Set scan_pubkey before reading
    return m_batch.Read(std::make_pair(std::string("sxad"), sxAddr.scan_pubkey), sxAddr);
}

bool AnonWalletDB::EraseStealthAddress(const CStealthAddress& sxAddr)
{
    return EraseIC(std::make_pair(std::string("sxad"), sxAddr.scan_pubkey));
}


bool AnonWalletDB::ReadNamedExtKeyId(const std::string &name, CKeyID &identifier, uint32_t nFlags)
{
    return m_batch.Read(std::make_pair(std::string("eknm"), name), identifier);
}

bool AnonWalletDB::WriteNamedExtKeyId(const std::string &name, const CKeyID &identifier)
{
    return WriteIC(std::make_pair(std::string("eknm"), name), identifier, true);
}


bool AnonWalletDB::ReadExtKey(const CKeyID &identifier, CKeyID& idAccount, BIP32Path& vPath)
{
    std::pair<CKeyID, BIP32Path> pLocation;
    if (!m_batch.Read(std::make_pair(std::string("ek32_n"), identifier), pLocation))
        return false;
    idAccount = pLocation.first;
    vPath = pLocation.second;
    return true;
}

bool AnonWalletDB::WriteExtKey(const CKeyID& idAccount, const CKeyID &idNew, const BIP32Path& vPath)
{
    std::pair<CKeyID, BIP32Path> pLocation(idAccount, vPath);
    return WriteIC(std::make_pair(std::string("ek32_n"), idNew), pLocation, true);
}

bool AnonWalletDB::EraseExtKey(const CKeyID& idKey)
{
    return EraseIC(std::make_pair(std::string("ek32_n"), idKey));
}

bool AnonWalletDB::WriteAccountCounter(const CKeyID& idAccount, const uint32_t& nCount)
{
    return WriteIC(std::make_pair(std::string("acct_c"), idAccount), nCount);
}

bool AnonWalletDB::ReadFlag(const std::string &name, int32_t &nValue, uint32_t nFlags)
{
    return m_batch.Read(std::make_pair(std::string("flag"), name), nValue);
}

bool AnonWalletDB::WriteFlag(const std::string &name, int32_t nValue)
{
    return WriteIC(std::make_pair(std::string("flag"), name), nValue, true);
}


bool AnonWalletDB::ReadExtKeyIndex(uint32_t id, CKeyID &identifier, uint32_t nFlags)
{
    return m_batch.Read(std::make_pair(std::string("ine"), id), identifier);
}

bool AnonWalletDB::WriteExtKeyIndex(uint32_t id, const CKeyID &identifier)
{
    return WriteIC(std::make_pair(std::string("ine"), id), identifier, true);
}


bool AnonWalletDB::ReadStealthAddressIndex(uint32_t id, CStealthAddressIndexed &sxi, uint32_t nFlags)
{
    return m_batch.Read(std::make_pair(std::string("ins"), id), sxi);
}

bool AnonWalletDB::WriteStealthAddressIndex(uint32_t id, const CStealthAddressIndexed &sxi)
{
    return WriteIC(std::make_pair(std::string("ins"), id), sxi, true);
}


bool AnonWalletDB::ReadStealthAddressIndexReverse(const uint160 &hash, uint32_t &id, uint32_t nFlags)
{
    return m_batch.Read(std::make_pair(std::string("ris"), hash), id);
}

bool AnonWalletDB::WriteStealthAddressIndexReverse(const uint160 &hash, uint32_t id)
{
    return WriteIC(std::make_pair(std::string("ris"), hash), id, true);
}


bool AnonWalletDB::ReadStealthAddressLink(const CKeyID &keyId, uint32_t &id, uint32_t nFlags)
{
    return m_batch.Read(std::make_pair(std::string("lns"), keyId), id);
}

bool AnonWalletDB::WriteStealthAddressLink(const CKeyID &keyId, uint32_t id)
{
    return WriteIC(std::make_pair(std::string("lns"), keyId), id, true);
}


bool AnonWalletDB::WriteAddressBookEntry(const std::string &sKey, const CAddressBookData &data)
{
    return WriteIC(std::make_pair(std::string("abe"), sKey), data, true);
}

bool AnonWalletDB::EraseAddressBookEntry(const std::string &sKey)
{
    return EraseIC(std::make_pair(std::string("abe"), sKey));
}


bool AnonWalletDB::ReadVoteTokens(std::vector<CVoteToken> &vVoteTokens, uint32_t nFlags)
{
    return m_batch.Read(std::string("votes"), vVoteTokens);
}

bool AnonWalletDB::WriteVoteTokens(const std::vector<CVoteToken> &vVoteTokens)
{
    return WriteIC(std::string("votes"), vVoteTokens, true);
}


bool AnonWalletDB::WriteTxRecord(const uint256 &hash, const CTransactionRecord &rtx)
{
    return WriteIC(std::make_pair(std::string("rtx"), hash), rtx, true);
}

bool AnonWalletDB::EraseTxRecord(const uint256 &hash)
{
    return EraseIC(std::make_pair(std::string("rtx"), hash));
}


bool AnonWalletDB::ReadStoredTx(const uint256 &hash, CStoredTransaction &stx, uint32_t nFlags)
{
    return m_batch.Read(std::make_pair(std::string("stx"), hash), stx);
}

bool AnonWalletDB::WriteStoredTx(const uint256 &hash, const CStoredTransaction &stx)
{
    return WriteIC(std::make_pair(std::string("stx"), hash), stx, true);
}

bool AnonWalletDB::EraseStoredTx(const uint256 &hash)
{
    return EraseIC(std::make_pair(std::string("stx"), hash));
}


bool AnonWalletDB::WriteKeyImageFromOutpoint(const COutPoint& outpoint, const CCmpPubKey& keyimage)
{
    return WriteIC(std::make_pair(std::string("out_ki"), outpoint), keyimage, true);
}

bool AnonWalletDB::GetKeyImageFromOutpoint(const COutPoint& outpoint, CCmpPubKey& keyimage)
{
    return m_batch.Read(std::make_pair(std::string("out_ki"), outpoint), keyimage);
}

bool AnonWalletDB::ReadAnonKeyImage(const CCmpPubKey &ki, COutPoint &op, uint32_t nFlags)
{
    return m_batch.Read(std::make_pair(std::string("aki"), ki), op);
}

bool AnonWalletDB::WriteAnonKeyImage(const CCmpPubKey &ki, const COutPoint &op)
{
    return WriteIC(std::make_pair(std::string("aki"), ki), op, true);
}

bool AnonWalletDB::EraseAnonKeyImage(const CCmpPubKey &ki)
{
    return EraseIC(std::make_pair(std::string("aki"), ki));
}


bool AnonWalletDB::HaveLockedAnonOut(const COutPoint &op, uint32_t nFlags)
{
    char c;
    return m_batch.Read(std::make_pair(std::string("lao"), op), c);
}

bool AnonWalletDB::WriteLockedAnonOut(const COutPoint &op)
{
    char c = 't';
    return WriteIC(std::make_pair(std::string("lao"), op), c, true);
}

bool AnonWalletDB::EraseLockedAnonOut(const COutPoint &op)
{
    return EraseIC(std::make_pair(std::string("lao"), op));
}


bool AnonWalletDB::ReadWalletSetting(const std::string &setting, std::string &json, uint32_t nFlags)
{
    return m_batch.Read(std::make_pair(std::string("wset"), setting), json);
}

bool AnonWalletDB::WriteWalletSetting(const std::string &setting, const std::string &json)
{
    return WriteIC(std::make_pair(std::string("wset"), setting), json, true);
}

bool AnonWalletDB::EraseWalletSetting(const std::string &setting)
{
    return EraseIC(std::make_pair(std::string("wset"), setting));
}

