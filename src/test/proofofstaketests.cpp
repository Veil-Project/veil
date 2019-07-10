// Copyright (c) 2019 The Veil developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <boost/test/unit_test.hpp>

#include <chainparams.h>
#include <consensus/merkle.h>
#include <consensus/validation.h>
#include <miner.h>
#include <pow.h>
#include <random.h>
#include <test/test_veil.h>
#include <validation.h>
#include <validationinterface.h>
#include <veil/budget.h>
#include <script/standard.h>
#include <key_io.h>
#include <veil/zerocoin/accumulators.h>


BOOST_FIXTURE_TEST_SUITE(proofofstake_tests, BasicTestingSetup)

BOOST_AUTO_TEST_CASE(ringctstake)
{

}

BOOST_AUTO_TEST_CASE(proofofstake_block)
{

    CMutableTransaction txCoinBase;
    txCoinBase.vin.resize(1);
    txCoinBase.vin[0].prevout.SetNull();
    txCoinBase.vin[0].scriptSig = CScript() << 1 << OP_0;
    txCoinBase.vpout.resize(1);
    txCoinBase.vpout[0] = MAKE_OUTPUT<CTxOutStandard>();

    CTxOut txOut_coinbase(0, CScript());
    txCoinBase.vpout.clear();
    txCoinBase.vpout.emplace_back(txOut_coinbase.GetSharedPtr());
//    txCoinBase.vpout[0]->SetValue(0);
//    txCoinBase.vpout[0]->SetScriptPubKey(CScript());
    CBlock block;
    block.vtx.emplace_back(MakeTransactionRef(std::move(txCoinBase)));
        block.vtx.resize(2);

    //load our serialized pubcoin
    libzerocoin::PrivateCoin privateCoin_stake(Params().Zerocoin_Params(), libzerocoin::CoinDenomination::ZQ_TEN, true);
    auto pubCoin = privateCoin_stake.getPublicCoin();
    BOOST_CHECK_MESSAGE(pubCoin.validate(), "Failed to validate pubCoin created from hex string");

    //initialize and Accumulator and AccumulatorWitness
    libzerocoin::Accumulator accumulator(Params().Zerocoin_Params(), libzerocoin::CoinDenomination::ZQ_TEN);
    libzerocoin::AccumulatorWitness witness(Params().Zerocoin_Params(), accumulator, pubCoin);

    //Add random zerocoins to the accumulator
    for (unsigned int i = 0; i < 5; i++) {
        libzerocoin::PrivateCoin pc(Params().Zerocoin_Params(), libzerocoin::CoinDenomination::ZQ_TEN, true);
        auto pub = pc.getPublicCoin();
        accumulator.accumulate(pub);
        witness.AddElement(pub);
    }

    //Add our own coin
    accumulator.accumulate(pubCoin);

    //Get the checksum of the accumulator we use for the spend and also add it to our checksum map
    auto hashChecksum = GetChecksum(accumulator.getValue());
    libzerocoin::CoinSpend spend(Params().Zerocoin_Params(), privateCoin_stake, accumulator, hashChecksum, witness, uint256(), libzerocoin::SpendType::STAKE, false);

    // Deserialize the CoinSpend intro a fresh object
    CDataStream serializedCoinSpend(SER_NETWORK, PROTOCOL_VERSION);
    serializedCoinSpend << spend;
    std::vector<unsigned char> data(serializedCoinSpend.begin(), serializedCoinSpend.end());

    CTxIn txin;
    //Add the coin spend into a transaction
    txin.scriptSig = CScript() << OP_ZEROCOINSPEND << data.size();
    txin.scriptSig.insert(txin.scriptSig.end(), data.begin(), data.end());
    txin.prevout.SetNull();

    //use nSequence as a shorthand lookup of denomination
    //NOTE that this should never be used in place of checking the value in the final blockchain acceptance/verification
    //of the transaction
    txin.nSequence = spend.getDenomination();
    txin.nSequence |= CTxIn::SEQUENCE_LOCKTIME_DISABLE_FLAG; //Don't use any relative locktime for zerocoin spend

    CMutableTransaction txCoinStake;
    txCoinStake.vin.emplace_back(txin);

    //Coinstake uses empty vout[0]
    txCoinStake.vpout.emplace_back(CTxOut(0, CScript()).GetSharedPtr());

    //Add mints to tx
    for (unsigned int i = 0; i < 5; i++) {
        libzerocoin::PrivateCoin pc(Params().Zerocoin_Params(), libzerocoin::CoinDenomination::ZQ_TEN, true);
        auto pub = pc.getPublicCoin();
        CScript scriptSerializedCoin = CScript() << OP_ZEROCOINMINT << pub.getValue().getvch().size() << pub.getValue().getvch();
        txCoinStake.vpout.emplace_back(CTxOut(libzerocoin::ZerocoinDenominationToAmount(pub.getDenomination()), scriptSerializedCoin).GetSharedPtr());
    }

    block.vtx[1] = MakeTransactionRef(std::move(txCoinStake));

    //This is done in incrementextranonce for PoW blocks
    block.hashMerkleRoot = BlockMerkleRoot(block);

    std::cout << "vpout size: "<<block.vtx[0]->vpout.size()<<"\n Block \n " << block.ToString() << std::endl;
    BOOST_CHECK_MESSAGE(block.IsProofOfStake(), "block is not marked as proof of stake");

    auto hashBlock = block.GetHash();
    CDataStream ss(SER_DISK, 0);
    ss << block;
    std::cout << "vpout size after serialize: "<<block.vtx[0]->vpout.size()<<"\n";
    CBlock block2;
    ss >> block2;

    BOOST_CHECK_MESSAGE(block2.GetHash() == hashBlock, "Block hash does not match after serialization");
    //BOOST_CHECK_MESSAGE(block2 == block, "Block changed after serialization");
    std::cout << "Block 2\n " << block2.ToString() << std::endl;

        BOOST_CHECK_MESSAGE(block.vtx[0]->GetHash() == block2.vtx[0]->GetHash(),
                            "Coinbase tx was malleated during serialization");
    try {
        auto txin1 = block.vtx[0]->vin[0];
        auto txin2 = block2.vtx[0]->vin[0];
        std::cout << "1\n";
        BOOST_CHECK_MESSAGE(txin1 == txin2, "Coinbase input was malleated during serialization");
        BOOST_CHECK_MESSAGE(txin1.scriptSig == txin2.scriptSig, "txin scriptsig mismatch");
        BOOST_CHECK_MESSAGE(txin1.prevout == txin2.prevout, "txin prevout mismatch");
        BOOST_CHECK_MESSAGE(txin1.nSequence == txin2.nSequence, "txin sequence mismatch");
        BOOST_CHECK_MESSAGE(txin1.scriptData.stack == txin2.scriptData.stack, "txin scriptdata mismatch");
        BOOST_CHECK_MESSAGE(txin1.scriptWitness.stack == txin2.scriptWitness.stack, "txin scriptwitness mismatch");

        std::cout << "2\n";


        CTxOut out1;
        block.vtx[0]->vpout[0]->GetTxOut(out1);
        CTxOut out2;
        block2.vtx[0]->vpout[0]->GetTxOut(out2);
        BOOST_CHECK_MESSAGE(out1 == out2, "Coinbase output was malleated during serialization");
    } catch (...) {
        BOOST_CHECK(false);
    }

    auto txCoinBase2 = block2.vtx[0];
    auto txCoinBase_block = block.vtx[0];
    BOOST_CHECK_MESSAGE(txCoinBase2->nLockTime == txCoinBase_block->nLockTime, "locktime incorrect");
    BOOST_CHECK_MESSAGE(txCoinBase2->nVersion == txCoinBase_block->nVersion, "version incorrect");
    BOOST_CHECK_MESSAGE(txCoinBase2->vin == txCoinBase_block->vin, "vin incorrect");
    BOOST_CHECK_MESSAGE(txCoinBase2->vpout.size() == txCoinBase_block->vpout.size(), "vpoutsize incorrect");
    std::cout << "vpoutsize2:"<< txCoinBase_block->vpout.size() << " vpoutsize1:" << txCoinBase_block->vpout.size() << std::endl;
    BOOST_CHECK_MESSAGE(txCoinBase2->HasWitness() == txCoinBase_block->HasWitness(), "witness flag incorrect");


}

BOOST_AUTO_TEST_SUITE_END()
