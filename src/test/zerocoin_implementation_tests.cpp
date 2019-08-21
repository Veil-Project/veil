// Copyright (c) 2019 The Veil developers
// Copyright (c) 2017-2019 The PIVX developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "libzerocoin/Denominations.h"
#include "amount.h"
#include "chainparams.h"
//#include "main.h"
#include "txdb.h"
//#include "primitives/deterministicmint.h"
#include "key.h"
#include "libzerocoin/PubcoinSignature.h"
//#include "accumulatorcheckpoints.h"
#include "libzerocoin/bignum.h"
#include <boost/test/unit_test.hpp>
#include <iostream>
#include <wallet/deterministicmint.h>
#include <veil/zerocoin/zwallet.h>
#include <veil/zerocoin/accumulators.h>
#include "wallet/wallet.h"
#include "veil/zerocoin/zchain.h"
#include "consensus/tx_verify.h"
#include "validation.h"
#include <core_io.h>

using namespace libzerocoin;

//extern bool DecodeHexTx(CTransaction& tx, const std::string& strHexTx);

BOOST_AUTO_TEST_SUITE(zerocoin_implementation_tests)


BOOST_AUTO_TEST_CASE(zcparams_test)
{
    cout << "Running zcparams_test...\n";
    RandomInit();
    ECC_Start();
    bool fPassed = true;
    try{
        SelectParams(CBaseChainParams::MAIN);
        ZerocoinParams *ZCParams = Params().Zerocoin_Params();
        (void)ZCParams;
    } catch(std::exception& e) {
        fPassed = false;
        std::cout << e.what() << "\n";
    }
    BOOST_CHECK(fPassed);
}

std::string zerocoinModulus = "25195908475657893494027183240048398571429282126204032027777137836043662020707595556264018525880784"
"4069182906412495150821892985591491761845028084891200728449926873928072877767359714183472702618963750149718246911"
"6507761337985909570009733045974880842840179742910064245869181719511874612151517265463228221686998754918242243363"
"7259085141865462043576798423387184774447920739934236584823824281198163815010674810451660377306056201619676256133"
"8441436038339044149526344321901146575444541784240209246165157233507787077498171257724679629263863563732899121548"
"31438167899885040445364023527381951378636564391212010397122822120720357";

//ZQ_TEN mints
std::string rawTx1 = "0100000001983d5fd91685bb726c0ebc3676f89101b16e663fd896fea53e19972b95054c49000000006a473044022010fbec3e78f9c46e58193d481caff715ceb984df44671d30a2c0bde95c54055f0220446a97d9340da690eaf2658e5b2bf6a0add06f1ae3f1b40f37614c7079ce450d012103cb666bd0f32b71cbf4f32e95fa58e05cd83869ac101435fcb8acee99123ccd1dffffffff0200e1f5050000000086c10280004c80c3a01f94e71662f2ae8bfcd88dfc5b5e717136facd6538829db0c7f01e5fd793cccae7aa1958564518e0223d6d9ce15b1e38e757583546e3b9a3f85bd14408120cd5192a901bb52152e8759fdd194df230d78477706d0e412a66398f330be38a23540d12ab147e9fb19224913f3fe552ae6a587fb30a68743e52577150ff73042c0f0d8f000000001976a914d6042025bd1fff4da5da5c432d85d82b3f26a01688ac00000000";
std::string rawTxpub1 = "473ff507157523e74680ab37f586aae52e53f3f912492b19f7e14ab120d54238ae30b338f39662a410e6d707784d730f24d19dd9f75e85221b51b902a19d50c120844d15bf8a3b9e346355857e7381e5be19c6d3d22e01845565819aae7cacc93d75f1ef0c7b09d823865cdfa3671715e5bfc8dd8fc8baef26216e7941fa0c3";
std::string rawTxRand1 = "9fc222b16be09eb88affbdfbcc02d1c8b28f5e843c72eb06c89dd7aff0c60838";
std::string rawTxSerial1 = "b87754b165892c0f9634e3d03780ede24824125249cb8dfd4ad2c0be055cbead";

std::string rawTx2 = "01000000018c52504b2822c39dd7f4bd93e30562dc9d246e0b0dd4ee401ec2c24e9378be12000000006b483045022100e2628dbcd284dd4858e2c2d8e2d2d31eb222773b3026d39c79c489f5daf4ae2302200e0b1cb9a6d534dc86ea33afb8153a5a4c7cd4fb497c889fb991fbac8bf86802012103836a4868020f52f2ab9e5ec3634d2cd38794677fab47ae7a7128ea8102972ae0ffffffff022c0f0d8f000000001976a914e2e8e36a1a35da051341775315b1168494921acd88ac00e1f5050000000086c10280004c809d49caa17c3f1fb8bc93eabf54462c8ad1717ab646c8130ca0863ca5613f34751445cd7bde8ef1dd833645c7c205dd9b36171dc25209f46b04a34b5e06caa655eea9bd95b46f7d03ae60a97961dd6632c1050090ec1b6748199f0721eeec0822dd288c663020dd88ecda7c8abf8a409fa5c500c4188e52bfbe2ca77ce7b2700700000000";
std::string rawTxpub2 = "770b2e77ca72cbebf528e18c400c5a59f408abf8a7cdaec88dd2030668c28dd2208ecee21079f1948671bec900005c13266dd6179a960ae037d6fb495bda9ee55a6ca065e4ba3046bf40952c21d17369bdd05c2c7453683ddf18ede7bcd451475343f61a53c86a00c13c846b67a71d18a2c4654bfea93bcb81f3f7ca1ca499d";
std::string rawTxRand2 = "23040b1d889ca4a41cf50b88a380f3f3acffac750e221a268fedf700f063a886";
std::string rawTxSerial2 = "37393797cb39e5f22bdc4fba8108edb5ea497ba0d22aba0781e58a8555df285c";

std::string rawTx3 = "01000000014651d7ed09c01d26679dd8ad1ee1f704f63167544ca48bdd3b4577444d540514010000006a47304402207995f8e30a87b74f36146d80ab02198319240a2eb3f93018c740e91b6812ff23022002677250aa9f9c7b6c1258647b0b0c03f89c7495b82b9c4dd2dcdb0ced82412801210236e3e30dbb1d62c8872413b2a771cd611b8042dfb5d06feb6805ba934ba534ffffffffff0200e1f5050000000086c10280004c803dac1997d38ee8650bb87fae490f4684a7b023744c95cd5ef025bc7f4d1414aff96947cebf342cfbfaf217ec0088e489d722d494409494a011a452af55a8cd4d2cef97f3b0307b66238623ab02b148a9e20f36782c8b7ea47c0c0b8226ddb91ee8f1f94c8c04df5c834993f27175b20b1da99d8338c674b1741a696c54def8012c0f0d8f000000001976a914c7f81b8e5650af548f5d56ef064da5c2d1ee09ae88ac00000000";
std::string rawTxpub3 = "1f8de546c691a74b174c638839da91d0bb27571f29349835cdf048c4cf9f1e81eb9dd26820b0c7ca47e8b2c78360fe2a948b102ab238623667b30b0f397ef2c4dcda855af52a411a094944094d422d789e48800ec17f2fafb2c34bfce4769f9af14144d7fbc25f05ecd954c7423b0a784460f49ae7fb80b65e88ed39719ac3d";
std::string rawTxRand3 = "1953c2919d658c3f654566400ace91563105ad5acc4e4151bca1e762c0877d7b";
std::string rawTxSerial3 = "3abf349844720512325d129c95402edbc85d86fff89632a05dc18970560047a5";

std::vector<std::pair<std::string, std::string> > vecRawMints = {std::make_pair(rawTx1, rawTxSerial1), std::make_pair(rawTx2, rawTxSerial2), std::make_pair(rawTx3, rawTxSerial3)};

//create a zerocoin mint from vecsend
BOOST_AUTO_TEST_CASE(checkzerocoinmint_test)
{
    cout << "generating privkeys\n";

    //generate a privkey
    CKey key;
    key.MakeNewKey(true);
    CPrivKey privkey = key.GetPrivKey();

    //generate pubkey hash/serial
    CPubKey pubkey = key.GetPubKey();
    uint256 nSerial = Hash(pubkey.begin(), pubkey.end());
    CBigNum bnSerial(nSerial);

    //make sure privkey import to new keypair makes the same serial
    CKey key2;
    key2.SetPrivKey(privkey, true);
    CPubKey pubkey2 = key2.GetPubKey();
    uint256 nSerial2 = Hash(pubkey2.begin(), pubkey2.end());
    CBigNum bnSerial2(nSerial2);
    BOOST_CHECK_MESSAGE(bnSerial == bnSerial2, "Serials do not match!");


    cout << "Running check_zerocoinmint_test...\n";
//    CTransaction tx;
//    //BOOST_CHECK(DecodeHexTx(tx, rawTx1));

//    bool fFoundMint = false;
//    CValidationState state;
//    for(unsigned int i = 0; i < tx.vout.size(); i++){
//        if(!tx.vout[i].scriptPubKey.empty() && tx.vout[i].scriptPubKey.IsZerocoinMint()) {
//            BOOST_CHECK(CheckZerocoinMint(tx.vout[i], state));
//            fFoundMint = true;
//        }
//    }
//
//    BOOST_CHECK(fFoundMint);
}

bool CheckZerocoinSpendNoDB(const CTransaction tx, string& strError)
{
    //max needed non-mint outputs should be 2 - one for redemption address and a possible 2nd for change
    if (tx.vpout.size() > 2){
        int outs = 0;
        for (const auto& out : tx.vpout) {
            if (out->IsZerocoinMint())
                continue;
            outs++;
        }
        if (outs > 2) {
            strError = "CheckZerocoinSpend(): over two non-mint outputs in a zerocoinspend transaction";
            return false;
        }

    }

    //compute the txout hash that is used for the zerocoinspend signatures
    CMutableTransaction txTemp;
    for (const auto& out : tx.vpout) {
        txTemp.vpout.emplace_back(out);
    }
    //    uint256 hashTxOut = txTemp.GetHash();

    bool fValidated = false;
    set<CBigNum> serials;
    list<CoinSpend> vSpends;
    CAmount nTotalRedeemed = 0;
    for (const CTxIn& txin : tx.vin) {

        //only check txin that is a zcspend
        if (!txin.IsZerocoinSpend())
            continue;

        // extract the CoinSpend from the txin
        std::vector<char, zero_after_free_allocator<char> > dataTxIn;
        dataTxIn.insert(dataTxIn.end(), txin.scriptSig.begin() + 4, txin.scriptSig.end());
        CDataStream serializedCoinSpend(dataTxIn, SER_NETWORK, PROTOCOL_VERSION);

        CoinSpend newSpend(Params().Zerocoin_Params(), serializedCoinSpend);

        vSpends.push_back(newSpend);

        //check that the denomination is valid
        if (newSpend.getDenomination() == ZQ_ERROR) {
            strError = "Zerocoinspend does not have the correct denomination";
            return false;
        }

        //check that denomination is what it claims to be in nSequence
        if (newSpend.getDenomination() != txin.nSequence) {
            strError = "Zerocoinspend nSequence denomination does not match CoinSpend";
        }

        //make sure the txout has not changed
//        if (newSpend.getTxOutHash() != hashTxOut) {
//            strError = "Zerocoinspend does not use the same txout that was used in the SoK";
//            return false;
//        }

//        //see if we have record of the accumulator used in the spend tx
//        CBigNum bnAccumulatorValue = 0;
//        if (!GetAccumulatorValueFromChecksum(newSpend.getAccumulatorChecksum(), true, bnAccumulatorValue)) {
//            strError = "Zerocoinspend could not find accumulator associated with checksum";
//            return false;
//        }

 //       Accumulator accumulator(Params().Zerocoin_Params(true), newSpend.getDenomination(), bnAccumulatorValue);

//        //Check that the coin is on the accumulator
//        if (!newSpend.Verify(accumulator)) {
//            strError = "CheckZerocoinSpend(): zerocoin spend did not verify";
//            return false;
//        }

        if (serials.count(newSpend.getCoinSerialNumber())) {
            strError = "Zerocoinspend serial is used twice in the same tx";
            return false;
        }
        serials.insert(newSpend.getCoinSerialNumber());

        //cannot check this without database
       // if(!IsZerocoinSpendUnknown(newSpend, tx.GetHash(), state))
       //     return state.DoS(100, error("Zerocoinspend is already known"));

        //make sure that there is no over redemption of coins
        nTotalRedeemed += ZerocoinDenominationToAmount(newSpend.getDenomination());
        fValidated = true;
    }

    if (nTotalRedeemed < tx.GetValueOut()) {
        strError = "Transaction spend more than was redeemed in zerocoins";
        return false;
    }

    return fValidated;
}

BOOST_AUTO_TEST_CASE(checkzerocoinspend_test)
{
    CBigNum bnTrustedModulus = 0;
    if (!bnTrustedModulus)
        bnTrustedModulus.SetDec(zerocoinModulus);
    libzerocoin::ZerocoinParams zerocoinParams = libzerocoin::ZerocoinParams(bnTrustedModulus);

    cout << "Running check_zerocoinspend_test...\n";

    libzerocoin::PrivateCoin coin(Params().Zerocoin_Params(), CoinDenomination::ZQ_TEN, true);
    PublicCoin pubCoin = coin.getPublicCoin();
    BOOST_CHECK_MESSAGE(pubCoin.validate(), "Failed to validate pubCoin created from hex string");

    //initialize and Accumulator and AccumulatorWitness
    Accumulator accumulator(Params().Zerocoin_Params(), CoinDenomination::ZQ_TEN);
    AccumulatorWitness witness(Params().Zerocoin_Params(), accumulator, pubCoin);
    accumulator += pubCoin;

    std::vector<libzerocoin::PublicCoin> vPubcoins;
    for (unsigned int i = 0; i < 10; i++) {
        PrivateCoin c(Params().Zerocoin_Params(), CoinDenomination::ZQ_TEN, true);
        vPubcoins.emplace_back(c.getPublicCoin());
    }

    //populate the witness and accumulators
    for(const auto& pubcoin : vPubcoins) {
        accumulator += pubcoin;
        witness += pubcoin;
    }

    //Get the checksum of the accumulator we use for the spend and also add it to our checksum map
    auto hashChecksum = GetChecksum(accumulator.getValue());
    CoinSpend* pspend = nullptr;
    try {
        pspend = new CoinSpend(Params().Zerocoin_Params(), coin, accumulator, hashChecksum, witness, uint256(), SpendType::SPEND, false);
    } catch (...) {
        BOOST_CHECK_MESSAGE(false, "Coinspend threw runtime error on creation");
    }

    CDataStream serializedCoinSpend2(SER_NETWORK, PROTOCOL_VERSION);
    bool fSerialize = false;
    if (pspend) {
        cout << pspend->ToString() << endl;
        std::string strError;
        CBigNum bnPubcoin;
        BOOST_CHECK_MESSAGE(pspend->Verify(accumulator, strError, bnPubcoin, true, false, true),
                            "Coinspend construction failed to create valid proof");

        CBigNum serial = pspend->getCoinSerialNumber();
        BOOST_CHECK_MESSAGE(serial, "Serial Number can't be 0");

        CoinDenomination denom = pspend->getDenomination();
        BOOST_CHECK_MESSAGE(denom == pubCoin.getDenomination(), "Spend denomination must match original pubCoin");
        BOOST_CHECK_MESSAGE(pspend->Verify(accumulator, strError, bnPubcoin, true, false, true), "CoinSpend object failed to validate");

        //serialize the spend
        try {
            fSerialize = true;
            serializedCoinSpend2 << *pspend;
        } catch (...) {
            fSerialize = false;
        }
    }
    BOOST_CHECK_MESSAGE(fSerialize, "failed to serialize coinspend object");

    std::vector<unsigned char> data(serializedCoinSpend2.begin(), serializedCoinSpend2.end());

    /** Check valid spend */

    CTxIn newTxIn;
    newTxIn.nSequence = 1;
    newTxIn.scriptSig = CScript() << OP_ZEROCOINSPEND << data.size();
    newTxIn.scriptSig.insert(newTxIn.scriptSig.end(), data.begin(), data.end());
    newTxIn.prevout.SetNull();

    // Deserialize the CoinSpend intro a fresh object
    std::vector<char, zero_after_free_allocator<char> > dataTxIn;
    dataTxIn.insert(dataTxIn.end(), newTxIn.scriptSig.begin() + 4, newTxIn.scriptSig.end());

    CDataStream serializedCoinSpend(dataTxIn, SER_NETWORK, PROTOCOL_VERSION);
    //old params for the V1 generated coin, new params for the accumulator. Emulates main-net transition.
    CoinSpend spend1(Params().Zerocoin_Params(), serializedCoinSpend);
    std::string strError;
    CBigNum bnPubcoin;
    BOOST_CHECK_MESSAGE(spend1.Verify(accumulator, strError, bnPubcoin, true, false, true), "Failed deserialized check of CoinSpend");

    CScript script;
    CTxOut txOut(1 * COIN, script);

    CMutableTransaction txNew;
    txNew.vin.push_back(newTxIn);
    txNew.vpout.push_back(txOut.GetSharedPtr());

    CTransaction txMintFrom;
    //BOOST_CHECK_MESSAGE(DecodeHexTx(txMintFrom, rawTx1), "Failed to deserialize hex transaction");
    if (!CheckZerocoinSpendNoDB(txNew, strError)) {
        BOOST_CHECK_MESSAGE(false, "checkzerocoinspend failed");
    }

    /**check an overspend*/

    CTxOut txOutOverSpend(100 * COIN, script);
    CMutableTransaction txOverSpend;
    txOverSpend.vin.push_back(newTxIn);
    txOverSpend.vpout.push_back(txOutOverSpend.GetSharedPtr());
    strError = "";
    CheckZerocoinSpendNoDB(txOverSpend, strError);
    string str = "Failed to detect overspend. Error Message: " + strError;
    BOOST_CHECK_MESSAGE(strError == "Transaction spend more than was redeemed in zerocoins", str);


    cout << "checking v2 spend\n";

    CMutableTransaction tx;
    uint256 txHash;
    CTxIn in(txHash, 0);
    tx.vin.emplace_back(in);

    // Create a New Zerocoin with specific denomination given by pubCoin
    PublicCoin pubcoin_v2 = coin.getPublicCoin();

    //initialize and Accumulator and AccumulatorWitness
    Accumulator accumulator_v2(Params().Zerocoin_Params(), CoinDenomination::ZQ_TEN);
    AccumulatorWitness witness_v2(Params().Zerocoin_Params(), accumulator_v2, pubcoin_v2);

    //populate the witness and accumulators - with old v1 params
    int64_t nTimeStart = GetTimeMillis();
    for(int i = 0; i < 5; i++) {
        PrivateCoin privTemp(Params().Zerocoin_Params(), CoinDenomination::ZQ_TEN, true);
        PublicCoin pubTemp = privTemp.getPublicCoin();
        accumulator_v2 += pubTemp;
        witness_v2 += pubTemp;
    }
    cout << (GetTimeMillis() - nTimeStart)/5 << "ms per mint\n";

    accumulator_v2 += pubcoin_v2;

    //Get the checksum of the accumulator we use for the spend and also add it to our checksum map
//    auto hashChecksum_v2 = GetChecksum(accumulator_v2.getValue());
//    uint256 ptxHash = CBigNum::randKBitBignum(256).getuint256();
//    try {
//        CoinSpend coinSpend_v2(Params().Zerocoin_Params(), coin, accumulator_v2, hashChecksum_v2, witness_v2,
//                               ptxHash, SpendType::SPEND);
//
//        BOOST_CHECK_MESSAGE(coinSpend_v2.HasValidSerial(Params().Zerocoin_Params()),
//                            "coinspend_v2 does not have a valid serial");
//        std::cout << "398\n";
//        BOOST_CHECK_MESSAGE(coinSpend_v2.Verify(accumulator_v2, strError, false), "coinspend_v2 failed to verify");
//        std::cout << "400\n";
//        BOOST_CHECK_MESSAGE(coinSpend_v2.HasValidSignature(), "coinspend_v2 does not have valid signature");
//        //BOOST_CHECK_MESSAGE(coinSpend_v2.getVersion() == 2, "coinspend_v2 version is wrong");
//        std::cout << "403\n";
//        BOOST_CHECK_MESSAGE(coinSpend_v2.getPubKey() == coin.getPubKey(), "pub keys do not match");
//        std::cout << "405\n";
//    } catch (...) {
//        BOOST_CHECK_MESSAGE(false, "Runtime error when creating coinspend_v2");
//    }
}

BOOST_AUTO_TEST_CASE(setup_exceptions_test)
{
    CBigNum bnTrustedModulus = 0;
    if (!bnTrustedModulus)
        bnTrustedModulus.SetDec(zerocoinModulus);
    libzerocoin::ZerocoinParams zerocoinParams = libzerocoin::ZerocoinParams(bnTrustedModulus);

    cout << "Running check_unitialized parameters,etc for setup exceptions...\n";

    CBigNum bnpubcoin;
    BOOST_CHECK(bnpubcoin.SetHexBool(rawTxpub1));

    // Check Modulus > 1023 Exception
    try {
        ZerocoinParams ZCParams(bnpubcoin);
        BOOST_CHECK_MESSAGE(false, "Didn't catch exception:  ZerocoinException: Modulus must be at least 1023 bit");
    }
    catch (...) {
        BOOST_CHECK_MESSAGE(true, "Caught exception: ZerocoinException: Modulus must be at least 1023 bit");
    }

    // Check Security Level < 80 Exception
    try {
        ZerocoinParams ZCParams(bnpubcoin,1);
        BOOST_CHECK_MESSAGE(false, "Didn't catch exception:  Security Level >= 80");
    }
    catch (...) {
        BOOST_CHECK_MESSAGE(true, "Caught exception: ZerocoinException: Security Level >= 80");
    }

    // Check unitialized params Exception for PublicCoin
    try {
        zerocoinParams.initialized = false;
        PublicCoin pubCoin(&zerocoinParams);
        BOOST_CHECK_MESSAGE(false, "Didn't catch exception checking for uninitialized Params");
    }
    catch (...) {
        BOOST_CHECK_MESSAGE(true, "Caught exception checking for initalized Params");
    }

    // Check unitialized params Exception for PublicCoin (alternate constructor)
    try {
        zerocoinParams.initialized = false;
        PublicCoin pubCoin(&zerocoinParams);
        BOOST_CHECK_MESSAGE(false, "Didn't catch exception checking for uninitialized Params");
    }
    catch (...) {
        BOOST_CHECK_MESSAGE(true, "Caught exception checking for initalized Params");
    }

    // Check unitialized params Exception for PrivateCoin
    try {
        zerocoinParams.initialized = false;
        PrivateCoin privCoin(&zerocoinParams, CoinDenomination::ZQ_TEN);
        BOOST_CHECK_MESSAGE(false, "Didn't catch exception checking for uninitialized Params");
    }
    catch (...) {
        BOOST_CHECK_MESSAGE(true, "Caught exception checking for initalized Params");
    }

}

BOOST_AUTO_TEST_CASE(deterministic_tests)
{
    SelectParams(CBaseChainParams::TESTNET);
    cout << "Testing deterministic minting\n";
    uint256 seedMaster("3a1947364362e2e7c073b386869c89c905c0cf462448ffd6c2021bd03ce689f6");

    string strWalletFile = "unittestwallet.dat";

    CWallet wallet("dummy", WalletDatabase::CreateDummy());
    CzWallet zWallet(&wallet);
    CKey keySeed;
    keySeed.Set(seedMaster.begin(), true);
    zWallet.SetMasterSeed(keySeed);
    wallet.setZWallet(&zWallet);

    int64_t nTimeStart = GetTimeMillis();
    CoinDenomination denom = CoinDenomination::ZQ_ONE_HUNDRED;

    std::vector<PrivateCoin> vCoins;
    int nTests = 50;
    for (int i = 0; i < nTests; i++) {
        PrivateCoin coin(Params().Zerocoin_Params(), denom, false);
        CDeterministicMint dMint;
        zWallet.GenerateDeterministicZerocoin(denom, coin, dMint);
        vCoins.emplace_back(coin);
    }

    int64_t nTotalTime = GetTimeMillis() - nTimeStart;
    cout << "Total time:" << nTotalTime << "ms. Per Deterministic Mint:" << (nTotalTime/nTests) << "ms" << endl;

    cout << "Checking that mints are valid" << endl;
    CDataStream ss(SER_GETHASH, 0);
    for (PrivateCoin& coin : vCoins) {
        BOOST_CHECK_MESSAGE(coin.IsValid(), "Generated Mint is not valid");
        ss << coin.getPublicCoin().getValue();
    }

    cout << "Checking that mints are deterministic: sha256 checksum=";
    uint256 hash = Hash(ss.begin(), ss.end());
    cout << hash.GetHex() << endl;
    BOOST_CHECK_MESSAGE(hash == uint256("c90c225f2cbdee5ef053b1f9f70053dd83724c58126d0e1b8425b88091d1f73f"), "minting determinism isn't as expected");
}

    static inline void InsecureNewKey(CKey &key, bool fCompressed)
    {
        key.MakeNewKey(fCompressed);
        assert(key.IsValid()); // Failure should be very rare
    }
    void makeNewStealthKey(CStealthAddress &sxAddr, CBasicKeyStore &keystore)
    {
        InsecureNewKey(sxAddr.scan_secret, true);

        CKey spend_secret;
        InsecureNewKey(spend_secret, true);
        //sxAddr.spend_secret_id = spend_secret.GetPubKey().GetID();

        SecretToPublicKey(sxAddr.scan_secret, sxAddr.scan_pubkey);
        SecretToPublicKey(spend_secret, sxAddr.spend_pubkey);

        // verify
        CPubKey pkTemp = sxAddr.scan_secret.GetPubKey();
        BOOST_CHECK(pkTemp.size() == EC_COMPRESSED_SIZE);
        BOOST_CHECK(memcmp(&sxAddr.scan_pubkey[0], pkTemp.begin(), EC_COMPRESSED_SIZE) == 0);

        pkTemp = spend_secret.GetPubKey();
        BOOST_CHECK(pkTemp.size() == EC_COMPRESSED_SIZE);
        BOOST_CHECK(pkTemp.size() == EC_COMPRESSED_SIZE);
        BOOST_CHECK(memcmp(&sxAddr.spend_pubkey[0], pkTemp.begin(), EC_COMPRESSED_SIZE) == 0);

        keystore.AddKeyPubKey(spend_secret, pkTemp);
    }

BOOST_AUTO_TEST_CASE(test_zerocoinspend_ringct_change)
{
    cout << "Testing zerocoin ringct change\n";
    //initialize and Accumulator and AccumulatorWitness
    PrivateCoin coin(Params().Zerocoin_Params(), CoinDenomination::ZQ_TEN, true);
    auto pubCoin = coin.getPublicCoin();
    Accumulator accumulator(Params().Zerocoin_Params(), CoinDenomination::ZQ_TEN);
    AccumulatorWitness witness(Params().Zerocoin_Params(), accumulator, pubCoin);


    std::vector<libzerocoin::PublicCoin> vPubcoins;
    for (unsigned int i = 0; i < 10; i++) {
        PrivateCoin c(Params().Zerocoin_Params(), CoinDenomination::ZQ_TEN, true);
        vPubcoins.emplace_back(c.getPublicCoin());
    }

    //populate the witness and accumulators
    accumulator += pubCoin;
    for (const auto& pub : vPubcoins) {
        BOOST_CHECK_MESSAGE(accumulator.accumulate(pub), "accumulator failed accumulating pubcoin");
        witness += pub;
    }

    uint256 nChecksum = GetChecksum(accumulator.getValue());
    uint256 ptxHash = CBigNum::randKBitBignum(256).getuint256();
    bool fSpendValid = true;
    CoinSpend* pspend;
    try {
        pspend = new CoinSpend(Params().Zerocoin_Params(), coin, accumulator, nChecksum, witness, ptxHash, SpendType::SPEND, false);
    } catch (...) {
        fSpendValid = false;
    }
    BOOST_CHECK_MESSAGE(fSpendValid, "Spend construction failed");

    CDataStream ss(SER_NETWORK, 0);
    ss << *pspend;

    std::vector<unsigned char> data(ss.begin(), ss.end());

    CTxIn newTxIn;
    newTxIn.nSequence = 1;
    newTxIn.scriptSig = CScript() << OP_ZEROCOINSPEND << data.size();
    newTxIn.scriptSig.insert(newTxIn.scriptSig.end(), data.begin(), data.end());
    newTxIn.prevout.SetNull();

    CMutableTransaction tx;
    tx.vin.emplace_back(newTxIn);
    tx.vpout.resize(2);


    CBasicKeyStore keystore;
    ECC_Start_Stealth();

    CStealthAddress sxAddr;
    makeNewStealthKey(sxAddr, keystore);

    CKey sEphem;
    CKey secretShared;
    ec_point pkSendTo;

    // Send, secret = ephem_secret, pubkey = scan_pubkey
    // NOTE: StealthSecret can fail if hash is out of range, retry with new ephemeral key
    int k, nTries = 24;
    for (k = 0; k < nTries; ++k)
    {
        InsecureNewKey(sEphem, true);
        if (StealthSecret(sEphem, sxAddr.scan_pubkey, sxAddr.spend_pubkey, secretShared, pkSendTo) == 0)
            break;
    };

    auto txbout = MAKE_OUTPUT<CTxOutRingCT>();
    CTxOutRingCT *txout = (CTxOutRingCT*)txbout.get();
//
//    txout->pk = r.pkTo;
//
//    CPubKey pkEphem = r.sEphem.GetPubKey();
//    SetCTOutVData(txout->vData, pkEphem, r.nStealthPrefix);

}

BOOST_AUTO_TEST_CASE(test_coinspendv4)
{
    cout << "Testing coinspendv4\n";

    //initialize and Accumulator and AccumulatorWitness
    PrivateCoin coin(Params().Zerocoin_Params(), CoinDenomination::ZQ_TEN, true);
    auto pubCoin = coin.getPublicCoin();
    Accumulator accumulator(Params().Zerocoin_Params(), CoinDenomination::ZQ_TEN);
    AccumulatorWitness witness(Params().Zerocoin_Params(), accumulator, pubCoin);

    std::vector<libzerocoin::PublicCoin> vPubcoins;
    for (unsigned int i = 0; i < 10; i++) {
        PrivateCoin c(Params().Zerocoin_Params(), CoinDenomination::ZQ_TEN, true);
        vPubcoins.emplace_back(c.getPublicCoin());
    }

    //populate the witness and accumulators
    accumulator += pubCoin;
    for (const auto& pub : vPubcoins) {
        BOOST_CHECK_MESSAGE(accumulator.accumulate(pub), "accumulator failed accumulating pubcoin");
        witness += pub;
    }

    uint256 ptxHash = uint256();
    uint256 checksum = uint256();
    CoinSpend spend(Params().Zerocoin_Params(), coin, accumulator, checksum, witness, ptxHash, SpendType::SPEND, CoinSpend::V4_LIMP, false);

    std::string strError;
    CBigNum bnPubcoin;
    bool fValid = spend.Verify(accumulator, strError, bnPubcoin, true, false, true);
    strError = std::string("Valid v4 coinspend failed to verify: ") + strError;
    BOOST_CHECK_MESSAGE(fValid, strError);

    //try serializing
    CDataStream ss(0,0);
    bool threw = false;
    try {
        ss << spend;
    } catch (...) {
        threw = true;
    }

    BOOST_CHECK_MESSAGE(!threw, "Serialization error with version 4 CoinSpend object");

    threw = false;
    try {
        CoinSpend spendss(Params().Zerocoin_Params(), ss);
        BOOST_CHECK_MESSAGE(spendss.getPubcoinValue() == coin.getPublicCoin().getValue(), "deserialized spend contains the wrong pubcoin");
    } catch (...) {
        threw = true;
    }

    BOOST_CHECK_MESSAGE(!threw, "Deserialization error with version 4 CoinSpend object");
}



BOOST_AUTO_TEST_SUITE_END()
