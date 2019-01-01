// Copyright (c) 2017-2019 The PIVX developers
// Copyright (c) 2019 The Veil developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <logging.h>
#include "zwallet.h"
#include "txdb.h"
#include "wallet/walletdb.h"
#include "wallet/deterministicmint.h"
#include "zchain.h"
#include "chainparams.h"
#include "validation.h"
#include "consensus/validation.h"
#include "shutdown.h"

using namespace libzerocoin;

CzWallet::CzWallet(CWallet* wallet)
{
    this->walletDatabase = wallet->database;
    WalletBatch walletdb(*walletDatabase);

    nCountLastUsed = 0;

    //Read master seed hash from DB, make sure the correct one is loaded
    bool fFirstRun = true;
    CKeyID seedID;
    if (walletdb.ReadCurrentSeedHash(seedID)) {
        fFirstRun = false;
        seedMasterID = seedID;
    }

    //Don't try to do anything if the wallet is locked.
    if (wallet->IsLocked()) {
        mapMasterSeeds.clear();
        this->mintPool = CMintPool();
        return;
    }

    //! Load zerocoin master seed from wallet
    CKey keySeed;
    if (!wallet->GetZerocoinSeed(keySeed)) {
        throw std::runtime_error(strprintf("%s: failed to get zerocoin masterseed, using zerocoin could result in losing funds!\n", __func__));
    }
    CKeyID idNew = keySeed.GetPubKey().GetID();
    if (!fFirstRun && idNew != seedMasterID) {
        throw std::runtime_error(strprintf("%s: Failed to load correct zerocoin master key. Looking for ID %s but got %s\n",
                __func__, seedMasterID.GetHex(), idNew.GetHex()));
    }
    mapMasterSeeds.emplace(seedMasterID, keySeed);

    if (!walletdb.ReadZCount(nCountLastUsed)) {
        nCountLastUsed = 0;
    }

    this->mintPool = CMintPool(nCountLastUsed);
}

bool CzWallet::GetMasterSeed(CKey& key) const
{
    if (seedMasterID == uint160() || !mapMasterSeeds.count(seedMasterID))
        return false;
    key = mapMasterSeeds.at(seedMasterID);
    return true;
}

void CzWallet::SetMasterSeed(const CKey& keyMaster, bool fResetCount)
{
    WalletBatch walletdb(*walletDatabase);

    seedMasterID = keyMaster.GetPubKey().GetID();
    mapMasterSeeds.emplace(seedMasterID, keyMaster);
    walletdb.WriteCurrentSeedHash(seedMasterID);

    nCountLastUsed = 0;

    if (fResetCount)
        walletdb.WriteZCount(nCountLastUsed);
    else if (!walletdb.ReadZCount(nCountLastUsed))
        nCountLastUsed = 0;

    mintPool.Reset();
}

void CzWallet::Lock()
{
    mapMasterSeeds.clear();
}

void CzWallet::AddToMintPool(const std::pair<uint256, uint32_t>& pMint, bool fVerbose)
{
    mintPool.Add(pMint, fVerbose);
}

//Add the next 20 mints to the mint pool
void CzWallet::GenerateMintPool(uint32_t nCountStart, uint32_t nCountEnd)
{
    //Is locked
    if (mapMasterSeeds.empty())
        return;

    uint32_t n = nCountLastUsed + 1;

    if (nCountStart > 0)
        n = nCountStart;

    uint32_t nStop = n + 20;
    if (nCountEnd > 0)
        nStop = std::max(n, n + nCountEnd);

    bool fFound;

    if (!mapMasterSeeds.count(seedMasterID)) {
        LogPrintf("%s: do not have master seed with ID %s loaded!", __func__, seedMasterID.GetHex());
        return;
    }

    LogPrintf("%s : n=%d nStop=%d\n", __func__, n, nStop - 1);
    for (uint32_t i = n; i < nStop; ++i) {
        if (ShutdownRequested())
            return;

        fFound = false;

        // Prevent unnecessary repeated minted
        for (auto& pair : mintPool) {
            if(pair.second == i) {
                fFound = true;
                break;
            }
        }

        if(fFound)
            continue;

        uint512 seedZerocoin = GetZerocoinSeed(seedMasterID, i);
        CBigNum bnValue;
        CBigNum bnSerial;
        CBigNum bnRandomness;
        CKey key;
        SeedToZerocoin(seedZerocoin, bnValue, bnSerial, bnRandomness, key);

        mintPool.Add(bnValue, i);
        WalletBatch(*walletDatabase).WriteMintPoolPair(seedMasterID, GetPubCoinHash(bnValue), i);
        LogPrintf("%s : %s count=%d\n", __func__, bnValue.GetHex().substr(0, 6), i);
    }
}

// pubcoin hashes are stored to db so that a full accounting of mints belonging to the seed can be tracked without regenerating
bool CzWallet::LoadMintPoolFromDB()
{
    std::map<CKeyID, std::vector<std::pair<uint256, uint32_t> > > mapMintPool = WalletBatch(*walletDatabase).MapMintPool();

    for (auto& pair : mapMintPool[seedMasterID])
        mintPool.Add(pair);

    return true;
}

void CzWallet::RemoveMintsFromPool(const std::vector<uint256> &vPubcoinHashes)
{
    for (const uint256& hash : vPubcoinHashes)
        mintPool.Remove(hash);
}

void CzWallet::GetState(int& nCount, int& nLastGenerated)
{
    nCount = this->nCountLastUsed + 1;
    nLastGenerated = mintPool.CountOfLastGenerated();
}

//Catch the counter up with the chain
void CzWallet::SyncWithChain(bool fGenerateMintPool)
{
    uint32_t nLastCountUsed = 0;
    bool found = true;
    WalletBatch walletdb(*walletDatabase);
    auto pwalletmain = GetMainWallet();
    if (!pwalletmain || !pwalletmain->zTracker)
        return;

    set<uint256> setAddedTx;
    while (found) {
        found = false;
        if (fGenerateMintPool)
            GenerateMintPool();
        LogPrintf("%s: Mintpool size=%d\n", __func__, mintPool.size());

        std::set<uint256> setChecked;
        std::list<pair<uint256,uint32_t> > listMints = mintPool.List();
        for (pair<uint256, uint32_t> pMint : listMints) {
            LOCK(cs_main);
            if (setChecked.count(pMint.first))
                return;
            setChecked.insert(pMint.first);

            if (ShutdownRequested())
                return;

            if (pwalletmain->zTracker->HasPubcoinHash(pMint.first)) {
                mintPool.Remove(pMint.first);
                continue;
            }

            uint256 txHash;
            CZerocoinMint mint;
            if (pzerocoinDB->ReadCoinMint(pMint.first, txHash)) {
                //this mint has already occurred on the chain, increment counter's state to reflect this
                LogPrintf("%s : Found wallet coin mint=%s count=%d tx=%s\n", __func__, pMint.first.GetHex(), pMint.second, txHash.GetHex());
                found = true;

                uint256 hashBlock;
                CTransactionRef txRef;
                if (!GetTransaction(txHash, txRef, Params().GetConsensus(), hashBlock, true)) {
                    LogPrintf("%s : failed to get transaction for mint %s!\n", __func__, pMint.first.GetHex());
                    found = false;
                    nLastCountUsed = std::max(pMint.second, nLastCountUsed);
                    continue;
                }

                //Find the denomination
                CoinDenomination denomination = CoinDenomination::ZQ_ERROR;
                bool fFoundMint = false;
                CBigNum bnValue = 0;
                for (const auto& out : txRef->vpout) {
                    if (!out->IsZerocoinMint())
                        continue;

                    auto zerocoinParams = Params().Zerocoin_Params();
                    PublicCoin pubcoin(zerocoinParams);
                    if (!OutputToPublicCoin(out.get(), pubcoin)) {
                        LogPrintf("%s : failed to get mint from txout for %s!\n", __func__, pMint.first.GetHex());
                        continue;
                    }

                    // See if this is the mint that we are looking for
                    uint256 hashPubcoin = GetPubCoinHash(pubcoin.getValue());
                    if (pMint.first == hashPubcoin) {
                        denomination = pubcoin.getDenomination();
                        bnValue = pubcoin.getValue();
                        fFoundMint = true;
                        break;
                    }
                }

                if (!fFoundMint || denomination == ZQ_ERROR) {
                    LogPrintf("%s : failed to get mint %s from tx %s!\n", __func__, pMint.first.GetHex(), txRef->GetHash().GetHex());
                    found = false;
                    break;
                }

                CBlockIndex* pindex = nullptr;
                if (mapBlockIndex.count(hashBlock))
                    pindex = mapBlockIndex.at(hashBlock);

                if (!setAddedTx.count(txHash)) {
                    CBlock block;
                    CWalletTx wtx(pwalletmain.get(), txRef);
                    if (pindex && ReadBlockFromDisk(block, pindex, Params().GetConsensus())) {
                        int nIndex;
                        for (nIndex = 0; nIndex < (int)block.vtx.size(); nIndex++) {
                            if (block.vtx[nIndex]->GetHash() == txRef->GetHash())
                                break;
                        }

                        wtx.SetMerkleBranch(pindex, nIndex);
                    }

                    //Fill out wtx so that a transaction record can be created
                    wtx.nTimeReceived = pindex->GetBlockTime();
                    pwalletmain->AddToWallet(wtx);
                    setAddedTx.insert(txHash);
                }

                SetMintSeen(bnValue, pindex->nHeight, txHash, denomination);
                nLastCountUsed = std::max(pMint.second, nLastCountUsed);
                nCountLastUsed = std::max(nLastCountUsed, nCountLastUsed);
                LogPrintf("%s: updated count to %d\n", __func__, nCountLastUsed);
            }
        }
    }
}

bool CzWallet::SetMintSeen(const CBigNum& bnValue, const int& nHeight, const uint256& txid, const CoinDenomination& denom)
{
    if (!mintPool.Has(bnValue))
        return error("%s: value not in pool", __func__);
    std::pair<uint256, uint32_t> pMint = mintPool.Get(bnValue);
    auto pwalletmain = GetMainWallet();

    // Regenerate the mint
    if (!mapMasterSeeds.count(seedMasterID))
        return error("%s: Do not have master seed in mapmasterseeds", __func__);

    uint512 seedZerocoin = GetZerocoinSeed(seedMasterID, pMint.second);
    CBigNum bnValueGen;
    CBigNum bnSerial;
    CBigNum bnRandomness;
    CKey key;
    SeedToZerocoin(seedZerocoin, bnValueGen, bnSerial, bnRandomness, key);

    //Sanity check
    if (bnValueGen != bnValue)
        return error("%s: generated pubcoin and expected value do not match!", __func__);

    // Create mint object and database it);
    uint256 hashSerial = GetSerialHash(bnSerial);
    uint256 hashPubcoin = GetPubCoinHash(bnValue);
    uint256 nSerial = bnSerial.getuint256();
    uint256 hashStake = Hash(nSerial.begin(), nSerial.end());
    CDeterministicMint dMint(PrivateCoin::CURRENT_VERSION, pMint.second, seedMasterID, hashSerial, hashPubcoin, hashStake);
    dMint.SetDenomination(denom);
    dMint.SetHeight(nHeight);
    dMint.SetTxHash(txid);

    // Check if this is also already spent
    int nHeightTx;
    uint256 txidSpend;
    CTransactionRef txSpend;
    if (IsSerialInBlockchain(hashSerial, nHeightTx, txidSpend, txSpend)) {
        //Find transaction details and make a wallettx and add to wallet
        dMint.SetUsed(true);
        CWalletTx wtx(pwalletmain.get(), txSpend);
        CBlockIndex* pindex = chainActive[nHeightTx];
        CBlock block;
        if (ReadBlockFromDisk(block, pindex, Params().GetConsensus())) {
            int nIndex;
            for (nIndex = 0; nIndex < (int)block.vtx.size(); nIndex++) {
                if (block.vtx[nIndex]->GetHash() == txSpend->GetHash())
                    break;
            }

            wtx.SetMerkleBranch(pindex, nIndex);
        }

        wtx.nTimeReceived = pindex->nTime;
        pwalletmain->AddToWallet(wtx);
    }

    // Add to zTracker which also adds to database
    pwalletmain->zTracker->Add(dMint, true);

    //Update the count if it is less than the mint's count
    if (nCountLastUsed < pMint.second) {
        WalletBatch walletdb(*walletDatabase);
        nCountLastUsed = pMint.second;
        walletdb.WriteZCount(nCountLastUsed);
    }

    //remove from the pool
    mintPool.Remove(dMint.GetPubcoinHash());

    return true;
}

// Check if the value of the commitment meets requirements
bool IsValidCoinValue(const CBigNum& bnValue)
{
    auto zerocoinParams = Params().Zerocoin_Params();
    return bnValue >= zerocoinParams->accumulatorParams.minCoinValue &&
           bnValue <= zerocoinParams->accumulatorParams.maxCoinValue &&
           bnValue.isPrime();
}

void CzWallet::SeedToZerocoin(const uint512& seedZerocoin, CBigNum& bnValue, CBigNum& bnSerial, CBigNum& bnRandomness, CKey& key)
{
    auto zerocoinParams = Params().Zerocoin_Params();

    //convert state seed into a seed for the private key
    uint256 nSeedPrivKey = seedZerocoin.trim256();

    bool isValidKey = false;
    key = CKey();
    while (!isValidKey) {
        nSeedPrivKey = Hash(nSeedPrivKey.begin(), nSeedPrivKey.end());
        isValidKey = libzerocoin::GenerateKeyPair(zerocoinParams->coinCommitmentGroup.groupOrder, nSeedPrivKey, key, bnSerial);
    }

    //hash randomness seed with Bottom 256 bits of seedZerocoin & attempts256 which is initially 0
    arith_uint512 arithSeed = UintToArith512(seedZerocoin);
    uint256 randomnessSeed = ArithToUint512(arithSeed >> 256).trim256();
    uint256 hashRandomness = Hash(randomnessSeed.begin(), randomnessSeed.end());
    bnRandomness.setuint256(hashRandomness);
    bnRandomness = bnRandomness % zerocoinParams->coinCommitmentGroup.groupOrder;

    //See if serial and randomness make a valid commitment
    // Generate a Pedersen commitment to the serial number
    CBigNum commitmentValue = zerocoinParams->coinCommitmentGroup.g.pow_mod(bnSerial, zerocoinParams->coinCommitmentGroup.modulus).mul_mod(
            zerocoinParams->coinCommitmentGroup.h.pow_mod(bnRandomness, zerocoinParams->coinCommitmentGroup.modulus),
            zerocoinParams->coinCommitmentGroup.modulus);

    CBigNum random;
    arith_uint256 attempts256;
    // Iterate on Randomness until a valid commitmentValue is found
    while (true) {
        // Now verify that the commitment is a prime number
        // in the appropriate range. If not, we'll throw this coin
        // away and generate a new one.
        if (IsValidCoinValue(commitmentValue)) {
            bnValue = commitmentValue;
            return;
        }

        //Did not create a valid commitment value.
        //Change randomness to something new and random and try again
        attempts256++;
        uint256 hashAttempts = ArithToUint256(attempts256);
        hashRandomness = Hash(randomnessSeed.begin(), randomnessSeed.end(),
                              hashAttempts.begin(), hashAttempts.end());
        random.setuint256(hashRandomness);
        bnRandomness = (bnRandomness + random) % zerocoinParams->coinCommitmentGroup.groupOrder;
        commitmentValue = commitmentValue.mul_mod(zerocoinParams->coinCommitmentGroup.h.pow_mod(random,
                zerocoinParams->coinCommitmentGroup.modulus), zerocoinParams->coinCommitmentGroup.modulus);
    }
}

uint512 CzWallet::GetZerocoinSeed(const CKeyID& keyID, uint32_t n)
{
    CDataStream ss(SER_GETHASH, 0);
    ss << mapMasterSeeds.at(keyID).GetPrivKey_256() << n;
    uint512 zerocoinSeed = Hash512(ss.begin(), ss.end());
    return zerocoinSeed;
}

void CzWallet::UpdateCount()
{
    nCountLastUsed++;
    WalletBatch walletdb(*walletDatabase);
    walletdb.WriteZCount(nCountLastUsed);
}

void CzWallet::GenerateDeterministicZerocoin(CoinDenomination denom, PrivateCoin& coin, CDeterministicMint& dMint, bool fGenerateOnly)
{
    // Prevent creating mints to a null seed
    if (this->HasEmptySeed())
        throw std::runtime_error("Trying to create new deterministic mint, but master seed is not loaded!");

    while (true) {
        GenerateMint(seedMasterID, nCountLastUsed + 1, denom, coin, dMint);
        if (coin.getPublicCoin().validate())
            break;
    }
}

void CzWallet::GenerateMint(const CKeyID& seedID, const uint32_t& nCount, const CoinDenomination denom, PrivateCoin& coin, CDeterministicMint& dMint)
{
    auto zerocoinParams = Params().Zerocoin_Params();

    if (!mapMasterSeeds.count(seedID))
        throw std::runtime_error(strprintf("%s: no matching masterseed with ID %s", __func__, seedID.GetHex()));

    uint512 seedZerocoin = GetZerocoinSeed(seedID, nCount);
    CBigNum bnValue;
    CBigNum bnSerial;
    CBigNum bnRandomness;
    CKey key;
    SeedToZerocoin(seedZerocoin, bnValue, bnSerial, bnRandomness, key);
    coin = PrivateCoin(zerocoinParams, denom, bnSerial, bnRandomness);
    coin.setPrivKey(key.GetPrivKey());
    coin.setVersion(PrivateCoin::CURRENT_VERSION);

    uint160 hashSeed = seedID;
    uint256 hashSerial = GetSerialHash(bnSerial);
    uint256 nSerial = bnSerial.getuint256();
    uint256 hashStake = Hash(nSerial.begin(), nSerial.end());
    uint256 hashPubcoin = GetPubCoinHash(bnValue);
    dMint = CDeterministicMint(coin.getVersion(), nCount, hashSeed, hashSerial, hashPubcoin, hashStake);
    dMint.SetDenomination(denom);
}

bool CzWallet::RegenerateMint(const CDeterministicMint& dMint, CZerocoinMint& mint)
{
    //Check that the seed is correct    todo:handling of incorrect, or multiple seeds
    CKeyID hashSeed = static_cast<CKeyID>(dMint.GetSeedHash());
    if (mapMasterSeeds.count(hashSeed) == 0)
        return error("%s: master seed does not match!\ndmint:\n %s \nhashSeed: %s\n", __func__, dMint.ToString(), hashSeed.GetHex());

    //Generate the coin
    auto zerocoinParams = Params().Zerocoin_Params();

    PrivateCoin coin(zerocoinParams, dMint.GetDenomination(), false);
    CDeterministicMint dMintDummy;
    GenerateMint(hashSeed, dMint.GetCount(), dMint.GetDenomination(), coin, dMintDummy);

    //Fill in the zerocoinmint object's details
    CBigNum bnValue = coin.getPublicCoin().getValue();
    if (GetPubCoinHash(bnValue) != dMint.GetPubcoinHash())
        return error("%s: failed to correctly generate mint, pubcoin hash mismatch", __func__);
    mint.SetValue(bnValue);

    CBigNum bnSerial = coin.getSerialNumber();
    if (GetSerialHash(bnSerial) != dMint.GetSerialHash())
        return error("%s: failed to correctly generate mint, serial hash mismatch", __func__);
    mint.SetSerialNumber(bnSerial);

    mint.SetRandomness(coin.getRandomness());
    mint.SetPrivKey(coin.getPrivKey());
    mint.SetVersion(coin.getVersion());
    mint.SetDenomination(dMint.GetDenomination());
    mint.SetUsed(dMint.IsUsed());
    mint.SetTxHash(dMint.GetTxHash());
    mint.SetHeight(dMint.GetHeight());

    return true;
}
