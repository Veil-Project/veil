// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2019 The Bitcoin Core developers
// Copyright (c) 2019-2020 Veil developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <miner.h>

#include <amount.h>
#include <chain.h>
#include <chainparams.h>
#include <coins.h>
#include <consensus/consensus.h>
#include <consensus/tx_verify.h>
#include <consensus/merkle.h>
#include <consensus/validation.h>
#include <hash.h>
#include <net.h>
#include <policy/feerate.h>
#include <policy/policy.h>
#include <pow.h>
#include <primitives/transaction.h>
#include <script/standard.h>
#include <timedata.h>
#include <util/system.h>
#include <util/moneystr.h>
#include <validationinterface.h>
#include <key_io.h>
#ifdef ENABLE_WALLET
#include <wallet/wallet.h>
#endif
#include <shutdown.h>

#include <veil/budget.h>
#include <veil/proofoffullnode/proofoffullnode.h>
#include <veil/zerocoin/zchain.h>

#include <algorithm>
#include <atomic>
#include <deque>
#include <mutex>
#include <thread>
#include <queue>
#include <utility>
#include <vector>
#include <boost/thread.hpp>
#include "veil/zerocoin/accumulators.h"

// ProgPow
#include <crypto/ethash/lib/ethash/endianness.hpp>
#include <crypto/ethash/lib/ethash/ethash-internal.hpp>
#include <crypto/ethash/include/ethash/progpow.hpp>
#include <crypto/randomx/randomx.h>
#include "crypto/ethash/helpers.hpp"
#include "crypto/ethash/progpow_test_vectors.hpp"

const char * PROGPOW_STRING = "progpow";
const char * SHA256D_STRING = "sha256d";
const char * RANDOMX_STRING = "randomx";

int nMiningAlgorithm = MINE_RANDOMX;

bool fGenerateActive = false;

bool GenerateActive() { return fGenerateActive; };
void setGenerate(bool fGenerate) { fGenerateActive = fGenerate; };

std::map<uint256, int64_t>mapComputeTimeTransactions;

// Unconfirmed transactions in the memory pool often depend on other
// transactions in the memory pool. When we select transactions from the
// pool, we select by highest fee rate of a transaction combined with all
// its ancestors.
uint64_t nLastBlockTx = 0;
uint64_t nLastBlockWeight = 0;

int64_t UpdateTime(CBlock* pblock, const Consensus::Params& consensusParams, const CBlockIndex* pindexPrev)
{
    int64_t nOldTime = pblock->nTime;
    int64_t nNewTime = std::max(pindexPrev->GetMedianTimePast()+1, GetAdjustedTime());

    if (nOldTime < nNewTime)
        pblock->nTime = nNewTime;

    // Updating time can change work required on testnet:
    if (consensusParams.fPowAllowMinDifficultyBlocks)
        pblock->nBits = GetNextWorkRequired(pindexPrev, pblock, consensusParams, pblock->IsProofOfStake(), pblock->PowType());

    return nNewTime - nOldTime;
}

BlockAssembler::Options::Options() {
    blockMinFeeRate = CFeeRate(DEFAULT_BLOCK_MIN_TX_FEE);
    nBlockMaxWeight = DEFAULT_BLOCK_MAX_WEIGHT;
}

BlockAssembler::BlockAssembler(const CChainParams& params, const Options& options) : chainparams(params)
{
    blockMinFeeRate = options.blockMinFeeRate;
    // Limit weight to between 4K and MAX_BLOCK_WEIGHT-4K for sanity:
    nBlockMaxWeight = std::max<size_t>(4000, std::min<size_t>(MAX_BLOCK_WEIGHT/4, options.nBlockMaxWeight));
}

static BlockAssembler::Options DefaultOptions()
{
    // Block resource limits
    // If -blockmaxweight is not given, limit to DEFAULT_BLOCK_MAX_WEIGHT
    BlockAssembler::Options options;
    options.nBlockMaxWeight = gArgs.GetArg("-blockmaxweight", DEFAULT_BLOCK_MAX_WEIGHT);
    if (gArgs.IsArgSet("-blockmintxfee")) {
        CAmount n = 0;
        ParseMoney(gArgs.GetArg("-blockmintxfee", ""), n);
        options.blockMinFeeRate = CFeeRate(n);
    } else {
        options.blockMinFeeRate = CFeeRate(DEFAULT_BLOCK_MIN_TX_FEE);
    }
    return options;
}

BlockAssembler::BlockAssembler(const CChainParams& params) : BlockAssembler(params, DefaultOptions()) {}

void BlockAssembler::resetBlock()
{
    inBlock.clear();

    // Reserve space for coinbase tx
    nBlockWeight = 4000;
    nBlockSigOpsCost = 400;
    fIncludeWitness = false;

    // These counters do not include coinbase tx
    nBlockTx = 0;
    nFees = 0;
}

std::unique_ptr<CBlockTemplate> BlockAssembler::CreateNewBlock(const CScript& scriptPubKeyIn, bool fMineWitnessTx, bool fProofOfStake, bool fProofOfFullNode, int nPoWType)
{
    int64_t nTimeStart = GetTimeMicros();
    int64_t nComputeTimeStart = GetTimeMillis();

    resetBlock();
#ifdef ENABLE_WALLET
    //Need wallet if this is for proof of stake,
    std::shared_ptr<CWallet> pwalletMain = nullptr;
#endif
    if (fProofOfStake) {
#ifdef ENABLE_WALLET
        if (!gArgs.GetBoolArg("-disablewallet", DEFAULT_DISABLE_WALLET)) {
            pwalletMain = GetMainWallet();
        }
        if (!pwalletMain) {
#endif
            error("Failing to get the Main Wallet for CreateNewBlock with Proof of Stake\n");
            return nullptr;
#ifdef ENABLE_WALLET
        }
#endif
    }

    pblocktemplate.reset(new CBlockTemplate());
    pblocktemplate->nFlags = TF_FAIL;

    if(!pblocktemplate.get()) {
        error("Failing to get the block template\n");
        return nullptr;
    }
    pblock = &pblocktemplate->block; // pointer for convenience

    // Add dummy coinbase tx as first transaction
    pblock->vtx.emplace_back();
    pblock->fProofOfStake = fProofOfStake;
    pblocktemplate->vTxFees.push_back(-1); // updated at end
    pblocktemplate->vTxSigOpsCost.push_back(-1); // updated at end

    CMutableTransaction txCoinStake;
    CBlockIndex* pindexPrev;
    //Do not pass in the chain tip, because it can change. Instead pass the blockindex directly from mapblockindex, which is const.
    auto pindexTip = chainActive.Tip();
    if (!pindexTip)
        return nullptr;
    auto hashBest = pindexTip->GetBlockHash();
    {
        LOCK(cs_mapblockindex);
        pindexPrev = mapBlockIndex.at(hashBest);
    }
    if (fProofOfStake && pindexPrev->nHeight + 1 >= Params().HeightPoSStart()) {
        //POS block - one coinbase is null then non null coinstake
        //POW block - one coinbase that is not null
        pblock->nTime = GetAdjustedTime();
        pblock->nBits = GetNextWorkRequired(pindexPrev, pblock, chainparams.GetConsensus(), true, pblock->PowType());

        uint32_t nTxNewTime = 0;
#ifdef ENABLE_WALLET
        if (!gArgs.GetBoolArg("-disablewallet", DEFAULT_DISABLE_WALLET) && pwalletMain->CreateCoinStake(pindexPrev, pblock->nBits, txCoinStake, nTxNewTime, nComputeTimeStart)) {
            pblock->nTime = nTxNewTime;
        } else {
            return nullptr;
        }
#endif
    }

    // Add dummy coinstake tx as second transaction for PoS blocks
    // This reserves position 1 so addPackageTxs doesn't put mempool txs there.
    // Note: must be added after the coinstake creation above — while vtx[1] holds
    // a null placeholder, CBlock::IsProofOfStake()/PowType() dereference vtx[1]
    // when vtx.size() > 1 (pblock->PowType() is called for GetNextWorkRequired).
    if (fProofOfStake) {
        pblock->vtx.emplace_back();
        pblocktemplate->vTxFees.push_back(-1);
        pblocktemplate->vTxSigOpsCost.push_back(-1);
    }

    LOCK(cs_main);

    assert(pindexPrev != nullptr);
    nHeight = pindexPrev->nHeight + 1;

    // Get the time before Computing the block version
    if (!fProofOfStake) {
        pblock->nTime = GetAdjustedTime();
        if (pblock->nTime < pindexPrev->GetBlockTime() - MAX_PAST_BLOCK_TIME) {
            pblock->nTime = pindexPrev->GetBlockTime() - MAX_PAST_BLOCK_TIME + 1;
        }
    }

    pblock->nVersion = ComputeBlockVersion(pindexPrev, chainparams.GetConsensus(), pblock->nTime, !fProofOfStake, nPoWType);
    // -regtest only: allow overriding block.nVersion with
    // -blockversion=N to test forking scenarios
    if (chainparams.MineBlocksOnDemand())
        pblock->nVersion = gArgs.GetArg("-blockversion", pblock->nVersion);

    const int64_t nMedianTimePast = pindexPrev->GetMedianTimePast();

    nLockTimeCutoff = (STANDARD_LOCKTIME_VERIFY_FLAGS & LOCKTIME_MEDIAN_TIME_PAST)
                       ? nMedianTimePast
                       : pblock->GetBlockTime();

    // Decide whether to include witness transactions
    // This is only needed in case the witness softfork activation is reverted
    // (which would require a very deep reorganization).
    // Note that the mempool would accept transactions with witness data before
    // IsWitnessEnabled, but we would only ever mine blocks after IsWitnessEnabled
    // unless there is a massive block reorganization with the witness softfork
    // not activated.
    // TODO: replace this with a call to main to assess validity of a mempool
    // transaction (which in most cases can be a no-op).
    fIncludeWitness = true;

    int nPackagesSelected = 0;
    int nDescendantsUpdated = 0;
    {
        TRY_LOCK(mempool.cs, fLockMem);
        if (!fLockMem) {
            error("Failing to get the lock on the mempool\n");
            pblocktemplate->nFlags |= TF_MEMPOOLFAIL;
            return nullptr;
        }
        addPackageTxs(nPackagesSelected, nDescendantsUpdated);
    }

    // Sweep out anything selection found already confirmed. Nothing else will ever
    // remove these: removeForBlock only fires for transactions in an arriving block,
    // and one of these can never be in a future block. Done here, after the mempool
    // lock above is released and selection has finished iterating, so no entry is
    // erased while it is being walked.
    for (const CTransactionRef& tx : vAlreadyInChain) {
        mempool.removeRecursive(*tx, MemPoolRemovalReason::BLOCK);
        LogPrintf("CreateNewBlock: removed already confirmed tx %s from the mempool\n",
                  tx->GetHash().GetHex());
    }
    vAlreadyInChain.clear();

    int64_t nTime1 = GetTimeMicros();

    nLastBlockTx = nBlockTx;
    nLastBlockWeight = nBlockWeight;

    CAmount nNetworkRewardReserve = pindexPrev ? pindexPrev->nNetworkRewardReserve : 0;
    std::string strRewardAddress = Params().NetworkRewardAddress();
    CTxDestination rewardDest = DecodeDestination(strRewardAddress);
    CScript rewardScript = GetScriptForDestination(rewardDest);

    //! find any coins that are sent to the network address, also make sure no conflicting zerocoin spends are included
    // todo reiterating over the spends here is not ideal, the new mining code is so complicated that this is the easiest solution at the moment
    std::set<uint256> setSerials;
    std::set<uint256> setPubcoins;
    std::set<uint256> setDuplicate;
    std::map<libzerocoin::CoinDenomination, int> mapDenomsSpent;
    std::map<libzerocoin::CoinDenomination, int> mapTxDenomsSpent;
    for (auto denom : libzerocoin::zerocoinDenomList) {
        mapTxDenomsSpent[denom] = 0;
        mapDenomsSpent[denom] = 0;
    }

    for (unsigned int i = 0; i < pblock->vtx.size(); i++) {
        if (pblock->vtx[i] == nullptr)
            continue;

        //Don't overload the block with too many zerocoinmints that will slow down validation and propogation of the block
        if (setPubcoins.size() >= Params().Zerocoin_PreferredMintsPerBlock())
            continue;

        bool fRemove = false;
        const CTransaction* ptx = pblock->vtx[i].get();
        std::set<uint256> setTxSerialHashes;
        std::set<uint256> setTxPubcoinHashes;
        if (ptx->IsZerocoinSpend()) {
            TxToSerialHashSet(ptx, setTxSerialHashes);

            //Double check that including this zerocoinspend transaction will not overrun the accumulator balance
            for (auto& p : mapTxDenomsSpent)
                p.second = 0;
            for (const CTxIn& in : ptx->vin) {
                if (in.IsZerocoinSpend()) {
                    CAmount nAmountSpent = in.GetZerocoinSpent();
                    auto denom = libzerocoin::AmountToZerocoinDenomination(nAmountSpent);
                    int nDenomBalance = pindexPrev->mapZerocoinSupply[denom] - mapDenomsSpent[denom] - mapTxDenomsSpent[denom] - 1;
                    if (nDenomBalance <= 1) {
                        //Including this transaction will spend more than is available in the accumulator
                        fRemove = true;
                        setDuplicate.emplace(ptx->GetHash());
                        LogPrintf("%s: skip tx spending denom %d\n", __func__, (int)denom);
                        break;
                    }

                    mapTxDenomsSpent[denom]++;
                }
            }
            if (fRemove)
                continue;
        }
        if (ptx->IsZerocoinMint())
            TxToPubcoinHashSet(ptx, setTxPubcoinHashes);

        //double check all zerocoin spends for duplicates or for already spent serials
        fRemove = false;
        for (const uint256& hashSerial : setTxSerialHashes) {
            if (setSerials.count(hashSerial)) {
                setDuplicate.emplace(ptx->GetHash());
                LogPrint(BCLog::BLOCKCREATION, "%s: removing duplicate serial tx %s\n", __func__, ptx->GetHash().GetHex());
                fRemove = true;
                break;
            } else {
                uint256 txid;
                // Out param must not be the nHeight member: a hit would overwrite the
                // height of the block being assembled with the confirmed spend's height.
                int nHeightTx = 0;
                if (IsSerialInBlockchain(hashSerial, nHeightTx, txid)) {
                    setDuplicate.emplace(ptx->GetHash());
                    LogPrint(BCLog::BLOCKCREATION, "%s: removing serial that is already in chain, tx=%s\n", __func__, ptx->GetHash().GetHex());
                    fRemove = true;
                    break;
                }
            }
            setSerials.emplace(hashSerial);
        }
        if (fRemove)
            continue;

        //Double check for mint duplicates or already accumulated pubcoins
        for (const uint256& hashPubcoin : setTxPubcoinHashes) {
            if (setPubcoins.count(hashPubcoin)) {
                setDuplicate.emplace(ptx->GetHash());
                LogPrint(BCLog::BLOCKCREATION, "%s: removing duplicate pubcoin tx %s\n", __func__, ptx->GetHash().GetHex());
                fRemove = true;
                break;
            } else {
                uint256 txid;
                int nHeightTx = 0;
                // No reference index here: a reference excludes its own block, so passing
                // the tip made a pubcoin accumulated in the tip block invisible. The
                // template then carried the duplicate mint, TestBlockValidity failed on
                // it, and a staker that was the only block producer could never advance
                // the tip to clear it, bricking block production entirely.
                if (IsPubcoinInBlockchain(hashPubcoin, nHeightTx, txid, nullptr)) {
                    setDuplicate.emplace(ptx->GetHash());
                    LogPrint(BCLog::BLOCKCREATION, "%s: removing already in chain pubcoin : tx %s\n", __func__, ptx->GetHash().GetHex());
                    fRemove = true;
                    break;
                }
            }
            setPubcoins.emplace(hashPubcoin);
        }
        if (fRemove)
            continue;

        for (const auto& pout : ptx->vpout) {
            if (!pout->IsStandardOutput())
                continue;
            if (*pout->GetPScriptPubKey() == rewardScript) {
                nNetworkRewardReserve += pout->GetValue();
            }
        }

        for (auto denompair : mapTxDenomsSpent)
            mapDenomsSpent[denompair.first] += denompair.second;
    }

    //Remove duplicates
    std::vector<CTransactionRef> vtxReplace;
    CCoinsViewCache viewCheck(pcoinsTip.get());
    for (unsigned int i = 0; i < pblock->vtx.size(); i++) {
        if (pblock->vtx[i] == nullptr) {
            vtxReplace.emplace_back(pblock->vtx[i]);
            continue;
        }

        if (setDuplicate.count(pblock->vtx[i]->GetHash())) {
            mempool.removeRecursive(*pblock->vtx[i]);
            continue;
        }

        //Don't have inputs, skip this
        if (!pblock->vtx[i]->IsZerocoinSpend() && !pblock->vtx[i]->vin[0].IsAnonInput() && !viewCheck.HaveInputs(*pblock->vtx[i])) {
            continue;
        }

        //Make sure tx's that overwrite other tx's do not get in (BIP30)
        for (size_t o = 0; o < pblock->vtx[i]->GetNumVOuts(); o++) {
            if (viewCheck.HaveCoin(COutPoint(pblock->vtx[i]->GetHash(), o))) {
                continue;
            }
        }

        vtxReplace.emplace_back(pblock->vtx[i]);
    }
    pblock->vtx = vtxReplace;

    CAmount nNetworkReward = nNetworkRewardReserve > Params().MaxNetworkReward() ? Params().MaxNetworkReward() : nNetworkRewardReserve;

    //! Create coinbase transaction.
    CMutableTransaction coinbaseTx;
    coinbaseTx.vin.resize(1);
    coinbaseTx.vin[0].prevout.SetNull();

    CAmount nBlockReward, nFounderPayment, nFoundationPayment, nBudgetPayment;
    veil::Budget().GetBlockRewards(nHeight, nBlockReward, nFounderPayment, nFoundationPayment, nBudgetPayment);

    if (nBudgetPayment > 0 && nFounderPayment > 0)
        coinbaseTx.vpout.resize(fProofOfStake ? 3 : 4);
    else if (nBudgetPayment > 0)
        coinbaseTx.vpout.resize(fProofOfStake ? 2 : 3);
    else {
        coinbaseTx.vpout.resize(1);
    }
    coinbaseTx.vpout[0] = MAKE_OUTPUT<CTxOutStandard>();

    if (!fProofOfStake) {
        //Miner gets the block reward and any network reward
        CAmount nMinerReward = nBlockReward + nNetworkReward;
        OUTPUT_PTR<CTxOutStandard> outCoinbase = MAKE_OUTPUT<CTxOutStandard>();
        outCoinbase->scriptPubKey = scriptPubKeyIn;
        outCoinbase->nValue = nMinerReward;
        coinbaseTx.vpout[0] = (std::move(outCoinbase));
    }

    // Budget Payment
    if (nBudgetPayment) {
        std::string strBudgetAddress = veil::Budget().GetBudgetAddress(chainActive.Height()+1); // KeyID for now
        CBitcoinAddress addressFounder(strBudgetAddress);
        assert(addressFounder.IsValid());
        CTxDestination dest = DecodeDestination(strBudgetAddress);
        auto budgetScript = GetScriptForDestination(dest);

        OUTPUT_PTR<CTxOutStandard> outBudget = MAKE_OUTPUT<CTxOutStandard>();
        outBudget->scriptPubKey = budgetScript;
        outBudget->nValue = nBudgetPayment;
        coinbaseTx.vpout[fProofOfStake ? 0 : 1] = (std::move(outBudget));

        std::string strFoundationAddress = veil::Budget().GetFoundationAddress(chainActive.Height()+1); // KeyID for now
        CTxDestination destFoundation = DecodeDestination(strFoundationAddress);
        auto foundationScript = GetScriptForDestination(destFoundation);

        OUTPUT_PTR<CTxOutStandard> outFoundation = MAKE_OUTPUT<CTxOutStandard>();
        outFoundation->scriptPubKey = foundationScript;
        outFoundation->nValue = nFoundationPayment;
        coinbaseTx.vpout[fProofOfStake ? 1 : 2] = (std::move(outFoundation));

        std::string strFounderAddress = veil::Budget().GetFounderAddress(); // KeyID for now
        CTxDestination destFounder = DecodeDestination(strFounderAddress);
        auto founderScript = GetScriptForDestination(destFounder);

        if (nFounderPayment) { // Founder payment will eventually hit 0
            OUTPUT_PTR<CTxOutStandard> outFounder = MAKE_OUTPUT<CTxOutStandard>();
            outFounder->scriptPubKey = founderScript;
            outFounder->nValue = nFounderPayment;
            coinbaseTx.vpout[fProofOfStake ? 2 : 3] = (std::move(outFounder));
        }
    }

    //Must add the height to the coinbase scriptsig
    coinbaseTx.vin[0].scriptSig = CScript() << nHeight << OP_0;
    if (fProofOfStake) {
        if (pblock->vtx.size() < 2)
            pblock->vtx.resize(2);
        if (!nBudgetPayment) {
            coinbaseTx.vpout[0]->SetValue(0);
            coinbaseTx.vpout[0]->SetScriptPubKey(CScript());
        }
        pblock->vtx[1] = MakeTransactionRef(std::move(txCoinStake));
    }
    pblock->vtx[0] = MakeTransactionRef(std::move(coinbaseTx));

    pblocktemplate->vTxFees[0] = -nFees;

    LogPrint(BCLog::BLOCKCREATION, "CreateNewBlock(): block weight: %u txs: %u fees: %ld sigops %d Proof-Of-Stake:%d \n", GetBlockWeight(*pblock), nBlockTx, nFees, nBlockSigOpsCost, pblock->IsProofOfStake());

    // Fill in header
    pblock->hashPrevBlock  = pindexPrev->GetBlockHash();

    if (!fProofOfStake)
        UpdateTime(pblock, chainparams.GetConsensus(), pindexPrev);

    pblock->nBits          = GetNextWorkRequired(pindexPrev, pblock, chainparams.GetConsensus(), pblock->IsProofOfStake(), pblock->PowType());
    pblock->nNonce         = 0;
    pblock->hashMerkleRoot = BlockMerkleRoot(*pblock);
    pblock->hashWitnessMerkleRoot = BlockWitnessMerkleRoot(*pblock);
    pblocktemplate->vTxSigOpsCost[0] = WITNESS_SCALE_FACTOR * GetLegacySigOpCount(*pblock->vtx[0]);

    pblock->nNonce64       = 0;
    pblock->nHeight        = pindexPrev->nHeight + 1;
    pblock->mixHash        = uint256();

    //Calculate the accumulator checkpoint only if the previous cached checkpoint need to be updated
    AccumulatorMap mapAccumulators(Params().Zerocoin_Params());
    auto mapCheckpoints = mapAccumulators.GetCheckpoints(true);
    if (nHeight % 10 == 0) {
        if (!CalculateAccumulatorCheckpoint(nHeight, mapCheckpoints, mapAccumulators))
            LogPrint(BCLog::BLOCKCREATION, "%s: failed to get accumulator checkpoints\n", __func__);
        pblock->mapAccumulatorHashes = mapAccumulators.GetCheckpoints(true);
    } else {
        pblock->mapAccumulatorHashes = pindexPrev->mapAccumulatorHashes;
    }

    //Proof of full node
    if(fProofOfFullNode && !fProofOfStake)
        LogPrint(BCLog::BLOCKCREATION, "%s: A block can not be proof of full node and proof of work.\n", __func__);
    else if(fProofOfFullNode && fProofOfStake) {
        AssertLockHeld(cs_main);
        pblock->hashPoFN = veil::GetFullNodeHash(*pblock, pindexPrev);
    }

    // Once the merkleRoot, witnessMerkleRoot and mapAccumulatorHashes have been calculated we can calculate the hashVeilData
    pblock->hashVeilData = pblock->GetVeilDataHash();
    pblock->hashAccumulators = SerializeHash(pblock->mapAccumulatorHashes);

    //Sign block if this is a proof of stake block
    if (fProofOfStake) {
        if (!pblock->vtx[1]->IsZerocoinSpend()) {
            error("%s: invalid block created. Stake is not zerocoinspend!", __func__);
            return nullptr;
        }
        auto spend = TxInToZerocoinSpend(pblock->vtx[1]->vin[0]);
        if (!spend) {
            LogPrint(BCLog::STAKING, "%s: failed to get spend for txin", __func__);
            return nullptr;
        }

        auto bnSerial = spend->getCoinSerialNumber();

        CKey key;
#ifdef ENABLE_WALLET
        if (gArgs.GetBoolArg("-disablewallet", DEFAULT_DISABLE_WALLET) || !pwalletMain->GetZerocoinKey(bnSerial, key)) {
#endif
            LogPrint(BCLog::STAKING, "%s: Failed to get zerocoin key from wallet!\n", __func__);
            return nullptr;
#ifdef ENABLE_WALLET
        }
#endif

        if (!key.Sign(pblock->GetHash(), pblock->vchBlockSig)) {
            LogPrint(BCLog::STAKING, "%s: Failed to sign block hash\n", __func__);
            return nullptr;
        }
        LogPrint(BCLog::STAKING, "%s: FOUND STAKE!!\n block: \n%s\n", __func__, pblock->ToString());
    }

    if (pindexPrev && pindexPrev != chainActive.Tip()) {
        error("%s: stale tip.", __func__);
        pblocktemplate->nFlags |= TF_STAILTIP;
        return std::move(pblocktemplate);
    }

    CValidationState state;
    if (!TestBlockValidity(state, chainparams, *pblock, pindexPrev, false, false)) {
        error("%s: TestBlockValidity failed: %s", __func__, FormatStateMessage(state));
        return nullptr;
    }

    int64_t nTime2 = GetTimeMicros();
    int64_t nComputeTimeFinish = GetTimeMillis();

    LogPrint(BCLog::BENCH, "CreateNewBlock() packages: %.2fms (%d packages, %d updated descendants), validity: %.2fms (total %.2fms)\n", 0.001 * (nTime1 - nTimeStart), nPackagesSelected, nDescendantsUpdated, 0.001 * (nTime2 - nTime1), 0.001 * (nTime2 - nTimeStart));

    if (fProofOfStake) {
        mapComputeTimeTransactions.clear();
        mapComputeTimeTransactions[pblock->vtx[1]->GetHash()] = nComputeTimeFinish - nComputeTimeStart; //store the compute time of this transaction
    }

    pblocktemplate->nFlags = TF_SUCCESS;
    return std::move(pblocktemplate);
}

void BlockAssembler::onlyUnconfirmed(CTxMemPool::setEntries& testSet)
{
    for (CTxMemPool::setEntries::iterator iit = testSet.begin(); iit != testSet.end(); ) {
        // Only test txs not already in the block
        if (inBlock.count(*iit)) {
            testSet.erase(iit++);
        }
        else {
            iit++;
        }
    }
}

bool BlockAssembler::TestPackage(uint64_t packageSize, int64_t packageSigOpsCost) const
{
    // TODO: switch to weight-based accounting for packages instead of vsize-based accounting.
    if (nBlockWeight + WITNESS_SCALE_FACTOR * packageSize >= nBlockMaxWeight)
        return false;
    if (nBlockSigOpsCost + packageSigOpsCost >= MAX_BLOCK_SIGOPS_COST)
        return false;
    return true;
}

// Perform transaction-level checks before adding to block:
// - transaction finality (locktime)
// - premature witness (in case segwit transactions are added to mempool before
//   segwit activation)
bool BlockAssembler::TestPackageTransactions(const CTxMemPool::setEntries& package)
{
    for (CTxMemPool::txiter it : package) {
        if (!IsFinalTx(it->GetTx(), nHeight, nLockTimeCutoff))
            return false;
        if (!fIncludeWitness && it->GetTx().HasWitness())
            return false;
        // A transaction that is already in a block must never be selected again. Its
        // outputs are in the UTXO set, so ConnectBlock's BIP30 check rejects the whole
        // block ("tried to overwrite transaction") and TestBlockValidity fails, which
        // means the node produces no block at all rather than one block missing one
        // transaction. This is the same predicate ConnectBlock uses, so a template can
        // no longer be poisoned by a single stale mempool entry. Seen on testnet: one
        // confirmed zerocoin spend that stayed in the mempool cost the node every block
        // it won for three and a half days.
        const CTransaction& tx = it->GetTx();
        for (size_t o = 0; o < tx.GetNumVOuts(); o++) {
            if (pcoinsTip->HaveCoin(COutPoint(tx.GetHash(), o))) {
                LogPrintf("%s: skipping %s, already confirmed but still in the mempool\n",
                          __func__, tx.GetHash().GetHex());
                vAlreadyInChain.push_back(it->GetSharedTx());
                return false;
            }
        }
    }
    return true;
}

void BlockAssembler::AddToBlock(CTxMemPool::txiter iter)
{
    pblock->vtx.emplace_back(iter->GetSharedTx());
    pblocktemplate->vTxFees.push_back(iter->GetFee());
    pblocktemplate->vTxSigOpsCost.push_back(iter->GetSigOpCost());
    nBlockWeight += iter->GetTxWeight();
    ++nBlockTx;
    nBlockSigOpsCost += iter->GetSigOpCost();
    nFees += iter->GetFee();
    inBlock.insert(iter);

    bool fPrintPriority = gArgs.GetBoolArg("-printpriority", DEFAULT_PRINTPRIORITY);
    if (fPrintPriority) {
        LogPrintf("fee %s txid %s\n",
                  CFeeRate(iter->GetModifiedFee(), iter->GetTxSize()).ToString(),
                  iter->GetTx().GetHash().ToString());
    }
}

int BlockAssembler::UpdatePackagesForAdded(const CTxMemPool::setEntries& alreadyAdded,
        indexed_modified_transaction_set &mapModifiedTx)
{
    int nDescendantsUpdated = 0;
    for (CTxMemPool::txiter it : alreadyAdded) {
        CTxMemPool::setEntries descendants;
        mempool.CalculateDescendants(it, descendants);
        // Insert all descendants (not yet in block) into the modified set
        for (CTxMemPool::txiter desc : descendants) {
            if (alreadyAdded.count(desc))
                continue;
            ++nDescendantsUpdated;
            modtxiter mit = mapModifiedTx.find(desc);
            if (mit == mapModifiedTx.end()) {
                CTxMemPoolModifiedEntry modEntry(desc);
                modEntry.nSizeWithAncestors -= it->GetTxSize();
                modEntry.nModFeesWithAncestors -= it->GetModifiedFee();
                modEntry.nSigOpCostWithAncestors -= it->GetSigOpCost();
                mapModifiedTx.insert(modEntry);
            } else {
                mapModifiedTx.modify(mit, update_for_parent_inclusion(it));
            }
        }
    }
    return nDescendantsUpdated;
}

// Skip entries in mapTx that are already in a block or are present
// in mapModifiedTx (which implies that the mapTx ancestor state is
// stale due to ancestor inclusion in the block)
// Also skip transactions that we've already failed to add. This can happen if
// we consider a transaction in mapModifiedTx and it fails: we can then
// potentially consider it again while walking mapTx.  It's currently
// guaranteed to fail again, but as a belt-and-suspenders check we put it in
// failedTx and avoid re-evaluation, since the re-evaluation would be using
// cached size/sigops/fee values that are not actually correct.
bool BlockAssembler::SkipMapTxEntry(CTxMemPool::txiter it, indexed_modified_transaction_set &mapModifiedTx, CTxMemPool::setEntries &failedTx)
{
    assert (it != mempool.mapTx.end());
    return mapModifiedTx.count(it) || inBlock.count(it) || failedTx.count(it);
}

void BlockAssembler::SortForBlock(const CTxMemPool::setEntries& package, std::vector<CTxMemPool::txiter>& sortedEntries)
{
    // Sort package by ancestor count
    // If a transaction A depends on transaction B, then A's ancestor count
    // must be greater than B's.  So this is sufficient to validly order the
    // transactions for block inclusion.
    sortedEntries.clear();
    sortedEntries.insert(sortedEntries.begin(), package.begin(), package.end());
    std::sort(sortedEntries.begin(), sortedEntries.end(), CompareTxIterByAncestorCount());
}

// This transaction selection algorithm orders the mempool based
// on feerate of a transaction including all unconfirmed ancestors.
// Since we don't remove transactions from the mempool as we select them
// for block inclusion, we need an alternate method of updating the feerate
// of a transaction with its not-yet-selected ancestors as we go.
// This is accomplished by walking the in-mempool descendants of selected
// transactions and storing a temporary modified state in mapModifiedTxs.
// Each time through the loop, we compare the best transaction in
// mapModifiedTxs with the next transaction in the mempool to decide what
// transaction package to work on next.
void BlockAssembler::addPackageTxs(int &nPackagesSelected, int &nDescendantsUpdated)
{
    // mapModifiedTx will store sorted packages after they are modified
    // because some of their txs are already in the block
    indexed_modified_transaction_set mapModifiedTx;
    // Keep track of entries that failed inclusion, to avoid duplicate work
    CTxMemPool::setEntries failedTx;

    // Start by adding all descendants of previously added txs to mapModifiedTx
    // and modifying them for their already included ancestors
    UpdatePackagesForAdded(inBlock, mapModifiedTx);

    CTxMemPool::indexed_transaction_set::index<ancestor_score>::type::iterator mi = mempool.mapTx.get<ancestor_score>().begin();
    CTxMemPool::txiter iter;

    // Limit the number of attempts to add transactions to the block when it is
    // close to full; this is just a simple heuristic to finish quickly if the
    // mempool has a lot of entries.
    const int64_t MAX_CONSECUTIVE_FAILURES = 1000;
    int64_t nConsecutiveFailed = 0;

    while (mi != mempool.mapTx.get<ancestor_score>().end() || !mapModifiedTx.empty())
    {
        // First try to find a new transaction in mapTx to evaluate.
        if (mi != mempool.mapTx.get<ancestor_score>().end() &&
                SkipMapTxEntry(mempool.mapTx.project<0>(mi), mapModifiedTx, failedTx)) {
            ++mi;
            continue;
        }

        // Now that mi is not stale, determine which transaction to evaluate:
        // the next entry from mapTx, or the best from mapModifiedTx?
        bool fUsingModified = false;

        modtxscoreiter modit = mapModifiedTx.get<ancestor_score>().begin();
        if (mi == mempool.mapTx.get<ancestor_score>().end()) {
            // We're out of entries in mapTx; use the entry from mapModifiedTx
            iter = modit->iter;
            fUsingModified = true;
        } else {
            // Try to compare the mapTx entry to the mapModifiedTx entry
            iter = mempool.mapTx.project<0>(mi);
            if (modit != mapModifiedTx.get<ancestor_score>().end() &&
                    CompareTxMemPoolEntryByAncestorFee()(*modit, CTxMemPoolModifiedEntry(iter))) {
                // The best entry in mapModifiedTx has higher score
                // than the one from mapTx.
                // Switch which transaction (package) to consider
                iter = modit->iter;
                fUsingModified = true;
            } else {
                // Either no entry in mapModifiedTx, or it's worse than mapTx.
                // Increment mi for the next loop iteration.
                ++mi;
            }
        }

        // We skip mapTx entries that are inBlock, and mapModifiedTx shouldn't
        // contain anything that is inBlock.
        assert(!inBlock.count(iter));

        uint64_t packageSize = iter->GetSizeWithAncestors();
//        CAmount packageFees = iter->GetModFeesWithAncestors();
        int64_t packageSigOpsCost = iter->GetSigOpCostWithAncestors();
        if (fUsingModified) {
            packageSize = modit->nSizeWithAncestors;
//            packageFees = modit->nModFeesWithAncestors;
            packageSigOpsCost = modit->nSigOpCostWithAncestors;
        }

//        if (packageFees < blockMinFeeRate.GetFee(packageSize)) {
//            // Everything else we might consider has a lower fee rate
//            return;
//        }

        if (!TestPackage(packageSize, packageSigOpsCost)) {
            if (fUsingModified) {
                // Since we always look at the best entry in mapModifiedTx,
                // we must erase failed entries so that we can consider the
                // next best entry on the next loop iteration
                mapModifiedTx.get<ancestor_score>().erase(modit);
                failedTx.insert(iter);
            }

            ++nConsecutiveFailed;

            if (nConsecutiveFailed > MAX_CONSECUTIVE_FAILURES && nBlockWeight >
                    nBlockMaxWeight - 4000) {
                // Give up if we're close to full and haven't succeeded in a while
                break;
            }
            continue;
        }

        CTxMemPool::setEntries ancestors;
        uint64_t nNoLimit = std::numeric_limits<uint64_t>::max();
        std::string dummy;
        mempool.CalculateMemPoolAncestors(*iter, ancestors, nNoLimit, nNoLimit, nNoLimit, nNoLimit, dummy, false);

        onlyUnconfirmed(ancestors);
        ancestors.insert(iter);

        // Test if all tx's are Final
        if (!TestPackageTransactions(ancestors)) {
            if (fUsingModified) {
                mapModifiedTx.get<ancestor_score>().erase(modit);
                failedTx.insert(iter);
            }
            continue;
        }

        // This transaction will make it in; reset the failed counter.
        nConsecutiveFailed = 0;

        // Package can be added. Sort the entries in a valid order.
        std::vector<CTxMemPool::txiter> sortedEntries;
        SortForBlock(ancestors, sortedEntries);

        for (size_t i=0; i<sortedEntries.size(); ++i) {
            AddToBlock(sortedEntries[i]);
            // Erase from the modified set, if present
            mapModifiedTx.erase(sortedEntries[i]);
        }

        ++nPackagesSelected;

        // Update transactions that depend on each of these
        nDescendantsUpdated += UpdatePackagesForAdded(ancestors, mapModifiedTx);
    }
}

void IncrementExtraNonce(CBlock* pblock, unsigned int nHeight, unsigned int& nExtraNonce)
{
    // Update nExtraNonce
    ++nExtraNonce;
    CMutableTransaction txCoinbase(*pblock->vtx[0]);
    // Height first in coinbase required for block.version=2
    txCoinbase.vin[0].scriptSig = (CScript() << (nHeight+1) << CScriptNum(nExtraNonce)) + COINBASE_FLAGS;
    assert(txCoinbase.vin[0].scriptSig.size() <= 100);

    pblock->vtx[0] = MakeTransactionRef(std::move(txCoinbase));
    pblock->hashMerkleRoot = BlockMerkleRoot(*pblock);

    bool malleated = false;
    pblock->hashWitnessMerkleRoot = BlockWitnessMerkleRoot(*pblock, &malleated);
}

bool fMintableCoins = false;
int nMintableLastCheck = 0;

CCriticalSection cs_nonce;
static int32_t nNonce_base = 0;
static arith_uint256 nHashes = 0;
static int32_t nTimeStart = 0;
static int32_t nTimeDuration = 0;

double GetHashSpeed() {
    LOCK(cs_nonce);
    if (!nTimeDuration) return 0;
    return arith_uint256(nHashes/nTimeDuration).getdouble();
}

class ThreadHashSpeed {
  public:
    ThreadHashSpeed() {}
    ThreadHashSpeed(ThreadHashSpeed&& ths) {
        LOCK(ths.cs);
        nHashes = ths.nHashes;
        nTimeDuration = ths.nTimeDuration;
    }
    CCriticalSection cs;
    arith_uint256 nHashes = 0;
    int32_t nTimeDuration = 0;
};

CCriticalSection cs_hashspeeds;
std::vector<ThreadHashSpeed> vHashSpeeds;

// Live counters for user facing mining stats. The mining threads add their
// attempts here as they go, so the recent rate below responds within seconds
// instead of once per template round.
static std::atomic<uint64_t> nLiveHashCounter{0};
static std::atomic<uint64_t> nSessionBlocksFound{0};
static std::atomic<int64_t> nLastBlockFoundTime{0};
static std::atomic<bool> fBuildingMinerDataset{false};

static void CountHashesMined(uint64_t n)
{
    nLiveHashCounter.fetch_add(n, std::memory_order_relaxed);
}

uint64_t GetSessionBlocksFound() { return nSessionBlocksFound.load(); }
int64_t GetSessionLastBlockTime() { return nLastBlockFoundTime.load(); }
void SetBuildingMinerDataset(bool fBuilding) { fBuildingMinerDataset = fBuilding; }
bool IsBuildingMinerDataset() { return fBuildingMinerDataset.load(); }

// Sliding window over the live hash counter. A sample is taken whenever a
// caller asks for the rate (the GUI polls a few times a second), and the rate
// is measured across roughly the last minute of samples.
static CCriticalSection cs_hashrate_window;
struct HashRateSample { int64_t nTimeMillis; uint64_t nHashes; };
static std::deque<HashRateSample> vHashRateWindow;

void ClearHashSpeed() {
    {
        LOCK(cs_nonce);
        nHashes = 0;
        nTimeStart = 0;
        nTimeDuration = 0;
    }
    {
        LOCK(cs_hashspeeds);
        for (auto& ths : vHashSpeeds) {
            LOCK(ths.cs);
            ths.nHashes = 0;
            ths.nTimeDuration = 0;
        }
    }
    {
        LOCK(cs_hashrate_window);
        vHashRateWindow.clear();
    }
    nLiveHashCounter = 0;
}

double GetRecentHashSpeed() {
    LOCK(cs_hashrate_window);
    if (!GenerateActive() || IsBuildingMinerDataset()) {
        // Not actually hashing: either stopped, or every thread is parked while
        // one builds the ProgPow DAG. Drop samples so the measurement restarts
        // fresh when hashing resumes, instead of averaging in the dead build
        // interval and under-reporting the rate for a minute afterwards.
        vHashRateWindow.clear();
        return 0.0;
    }

    const int64_t nNow = GetTimeMillis();
    const uint64_t nCount = nLiveHashCounter.load(std::memory_order_relaxed);

    if (vHashRateWindow.empty() || nNow - vHashRateWindow.back().nTimeMillis >= 200)
        vHashRateWindow.push_back({nNow, nCount});

    while (vHashRateWindow.size() > 2 && nNow - vHashRateWindow.front().nTimeMillis > 60000)
        vHashRateWindow.pop_front();

    const HashRateSample& front = vHashRateWindow.front();
    if (nCount <= front.nHashes || nNow <= front.nTimeMillis)
        return 0.0;
    return (nCount - front.nHashes) * 1000.0 / (nNow - front.nTimeMillis);
}

/**
 * Mining side ProgPow context handling. Validation keeps its own light context
 * in hash.cpp; the miner never touches it, so hashing for blocks does not
 * contend with block validation.
 *
 * By default the miner uses a light context. With -progpowdag (or the GUI
 * checkbox) it builds the full DAG once per epoch, which makes CPU mining
 * roughly two orders of magnitude faster at the cost of several GB of memory.
 * The dataset is built eagerly and in parallel here because the library's lazy
 * per item fill is not thread safe under concurrent miners.
 */
static CCriticalSection cs_progpow_mining;
static std::shared_ptr<ethash::epoch_context_full> spProgPowContextFull;
static std::shared_ptr<ethash::epoch_context> spProgPowContextLight;
static int nProgPowMiningEpoch = -1;
// These are read and written from the GUI thread as well as from miner threads,
// so keep them atomic. In particular the GUI setter must never take
// cs_progpow_mining: a miner thread can hold that lock for minutes while it
// builds the DAG, and blocking the GUI thread on it would freeze the whole UI.
static std::atomic<bool> fProgPowDagFailed{false};
static std::atomic<bool> fProgPowUseFullDag{false};
static std::once_flag progpow_dag_init;

void SetProgPowFullDataset(bool fUse)
{
    // Mark as initialised so a later GetProgPowFullDataset does not clobber this
    // explicit choice with the -progpowdag default.
    std::call_once(progpow_dag_init, [](){});
    fProgPowUseFullDag.store(fUse);
    fProgPowDagFailed.store(false);
}

bool GetProgPowFullDataset()
{
    std::call_once(progpow_dag_init, [](){
        fProgPowUseFullDag.store(gArgs.GetBoolArg("-progpowdag", false));
    });
    return fProgPowUseFullDag.load();
}

/** Fill the full DAG in parallel. Returns false if aborted by shutdown or the
 * miner being switched off. Items the L1 prefill already wrote are skipped. */
static bool BuildProgPowDataset(ethash::epoch_context_full* ctx, int nThreads)
{
    const uint32_t nItems2048 = static_cast<uint32_t>(ctx->full_dataset_num_items) / 2;
    const uint32_t nFirst = progpow::l1_cache_size / sizeof(ethash::hash2048);
    auto* pDataset = reinterpret_cast<ethash::hash2048*>(ctx->full_dataset);

    if (nThreads < 1) nThreads = 1;
    std::atomic<bool> fAborted{false};
    std::vector<std::thread> vThreads;
    const uint32_t nPerThread = (nItems2048 - nFirst) / nThreads;
    for (int t = 0; t < nThreads; ++t) {
        const uint32_t nStart = nFirst + t * nPerThread;
        const uint32_t nEnd = (t == nThreads - 1) ? nItems2048 : nStart + nPerThread;
        vThreads.emplace_back([ctx, pDataset, nStart, nEnd, &fAborted]() {
            for (uint32_t i = nStart; i < nEnd; ++i) {
                if ((i & 0xfff) == 0 && (ShutdownRequested() || !GenerateActive() || fAborted)) {
                    fAborted = true;
                    return;
                }
                pDataset[i] = ethash::calculate_dataset_item_2048(*ctx, i);
            }
        });
    }
    for (auto& t : vThreads)
        t.join();
    return !fAborted;
}

/** Get shared mining contexts for the epoch of nHeight. On success exactly one
 * of the two out pointers is set. Both empty means the caller should give up
 * this round (shutdown or mining switched off during a DAG build). */
static void GetProgPowMiningContext(int nHeight,
                                    std::shared_ptr<ethash::epoch_context_full>& ctxFull,
                                    std::shared_ptr<ethash::epoch_context>& ctxLight)
{
    const int nEpoch = Params().GetProgPowEpochNumber(nHeight);
    const bool fWantFull = GetProgPowFullDataset();
    LOCK(cs_progpow_mining);
    const bool fHaveRight = nEpoch == nProgPowMiningEpoch &&
                            (spProgPowContextFull || spProgPowContextLight) &&
                            ((fWantFull && !fProgPowDagFailed) == (spProgPowContextFull != nullptr));
    if (!fHaveRight) {
        spProgPowContextFull.reset();
        spProgPowContextLight.reset();
        nProgPowMiningEpoch = -1;
        // Do not start a multi minute, multi GB build if mining is already on its
        // way out. Without this liveness check, every miner thread queued on
        // cs_progpow_mining would kick off its own fresh build after a stop, one
        // after another, each one aborting only once its worker threads notice.
        if (fWantFull && !fProgPowDagFailed && GenerateActive() && !ShutdownRequested()) {
            const size_t nBytes = static_cast<size_t>(ethash_calculate_full_dataset_num_items(nEpoch)) * sizeof(ethash::hash1024);
            LogPrintf("%s: Building ProgPow DAG for epoch %d (%d MB), this can take a few minutes\n",
                      __func__, nEpoch, nBytes >> 20);
            SetBuildingMinerDataset(true);
            auto nTime1 = GetTimeMillis();
            // Guard the null case explicitly: wrapping a null pointer in a
            // shared_ptr with this deleter would call the destroy function on
            // null when the temporary dies, which is undefined behaviour.
            ethash::epoch_context_full* pRaw = ethash_create_epoch_context_full(nEpoch);
            if (!pRaw) {
                LogPrintf("%s: ProgPow DAG allocation failed, falling back to light mining\n", __func__);
                fProgPowDagFailed = true;
            } else {
                std::shared_ptr<ethash::epoch_context_full> ctx(pRaw, ethash_destroy_epoch_context_full);
                if (!BuildProgPowDataset(ctx.get(), GetNumCores())) {
                    // Aborted part way. Drop it so a later start rebuilds from scratch.
                    SetBuildingMinerDataset(false);
                    return;
                }
                LogPrintf("%s: Finished ProgPow DAG %.2fms\n", __func__, (double)(GetTimeMillis() - nTime1));
                spProgPowContextFull = ctx;
            }
            SetBuildingMinerDataset(false);
        }
        if (!spProgPowContextFull)
            spProgPowContextLight = std::shared_ptr<ethash::epoch_context>(
                ethash_create_epoch_context(nEpoch), ethash_destroy_epoch_context);
        nProgPowMiningEpoch = nEpoch;
    }
    ctxFull = spProgPowContextFull;
    ctxLight = spProgPowContextLight;
}

void FreeProgPowMiningContext()
{
    LOCK(cs_progpow_mining);
    spProgPowContextFull.reset();
    spProgPowContextLight.reset();
    nProgPowMiningEpoch = -1;
    fProgPowDagFailed = false;
}

void BitcoinMiner(std::shared_ptr<CReserveScript> coinbaseScript, bool fProofOfStake = false, bool fProofOfFullNode = false, ThreadHashSpeed* pThreadHashSpeed = nullptr) {
    LogPrintf("Veil Miner started\n");

    unsigned int nExtraNonce = 0;
    static const int nInnerLoopCount = 0x010000;
    static uint32_t nStakeHashesLast = 0;
    int32_t nLocalStartTime = 0;
    bool enablewallet = false;
#ifdef ENABLE_WALLET
    enablewallet = !gArgs.GetBoolArg("-disablewallet", DEFAULT_DISABLE_WALLET);
#endif

    while (!ShutdownRequested() && (GenerateActive() || (fProofOfStake && enablewallet)))
    {
        boost::this_thread::interruption_point();
#ifdef ENABLE_WALLET
        if (enablewallet && fProofOfStake) {
            if (IsInitialBlockDownload()) {
                UninterruptibleSleep(std::chrono::milliseconds{5000});
                continue;
            }

            //Need wallet if this is for proof of stake
            auto pwallet = GetMainWallet();

            int nHeight;
            int64_t nTimeLastBlock = 0;
            uint256 hashBestBlock;
            {
                LOCK(cs_main);
                nHeight = chainActive.Height();
                nTimeLastBlock = chainActive.Tip()->GetBlockTime();
                hashBestBlock = chainActive.Tip()->GetBlockHash();
            }

            if (!gArgs.GetBoolArg("-genoverride", false) && (GetAdjustedTime() - nTimeLastBlock > 60*60 || IsInitialBlockDownload() || !g_connman->GetNodeCount(CConnman::NumConnections::CONNECTIONS_ALL) || nHeight < Params().HeightPoSStart())) {
                UninterruptibleSleep(std::chrono::milliseconds{5000});
                continue;
            }

            if (!pwallet || !pwallet->IsStakingEnabled() || (pwallet->IsLocked() && !pwallet->IsUnlockedForStakingOnly())) {
                mapHashedBlocks.clear();
                UninterruptibleSleep(std::chrono::milliseconds{5000});
                continue;
            }

            //control the amount of times the client will check for mintable coins
            if ((GetTime() - nMintableLastCheck > 5 * 60)) // 5 minute check time
            {
                nMintableLastCheck = GetTime();
                fMintableCoins = pwallet->MintableCoins();
            }

            bool fNextIter = false;
            while (!fMintableCoins || GetAdjustedTime() < nTimeLastBlock - MAX_PAST_BLOCK_TIME) {
                boost::this_thread::interruption_point();
                // Do a separate 1 minute check here to ensure fMintableCoins is updated
                if (!fMintableCoins) {
                    if (GetTime() - nMintableLastCheck > 1 * 60) // 1 minute check time
                    {
                        nMintableLastCheck = GetTime();
                        fMintableCoins = pwallet->MintableCoins();
                    }
                    if (!fMintableCoins)
                        fNextIter = true;
                }
                UninterruptibleSleep(std::chrono::milliseconds{2500});
                break;
            }
            if (fNextIter)
                continue;

            //search our map of hashed blocks, see if bestblock has been hashed yet
            if (mapHashedBlocks.count(hashBestBlock)) {
                auto it = mapStakeHashCounter.find(nHeight);
                if (it != mapStakeHashCounter.end() && it->second != nStakeHashesLast) {
                    nStakeHashesLast = it->second;
                    LogPrint(BCLog::STAKING, "%s: Tried %d stake hashes for block %d last=%d\n", __func__, nStakeHashesLast, nHeight+1, mapHashedBlocks.at(hashBestBlock));
                }
                // wait half of the nHashDrift with max wait of 3 minutes
                int rand = GetRandInt(20); // add small randomness to prevent all nodes from being on too similar of timing
                if (GetAdjustedTime() + MAX_FUTURE_BLOCK_TIME - mapHashedBlocks[hashBestBlock] < (60+rand)) {
                    UninterruptibleSleep(std::chrono::milliseconds{GetRandInt(10)*1000});
                    continue;
                }
            }
        }
#endif

        if (GenerateActive() && !fProofOfStake) {
            // If the miner was turned on and we are in IsInitialBlockDownload(),
            // sleep 60 seconds, before trying again
            if (IsInitialBlockDownload() && !gArgs.GetBoolArg("-genoverride", false)) {
                UninterruptibleSleep(std::chrono::milliseconds{5000});
                continue;
            }
        }

        CScript scriptMining;
        if (coinbaseScript)
            scriptMining = coinbaseScript->reserveScript;
        std::unique_ptr<CBlockTemplate> pblocktemplate(BlockAssembler(Params()).CreateNewBlock(scriptMining, false, fProofOfStake, fProofOfFullNode));
        if (!pblocktemplate || !pblocktemplate.get())
            continue;
        if (!(pblocktemplate->nFlags & TF_SUCCESS)) {
            continue;
        }

        CBlock *pblock = &pblocktemplate->block;

        if (!fProofOfStake)
        {
            {
                LOCK(cs_nonce);
                nExtraNonce = nNonce_base++;
                nLocalStartTime = GetTime();
                if (!nTimeStart)
                    nTimeStart = nLocalStartTime;
            }

            pblock->nNonce = 0;
            pblock->nNonce64 = rand();
            IncrementExtraNonce(pblock, chainActive.Height(), nExtraNonce);

            int nTries = 0;
            bool success = false;
            // As SHA runs much faster than the other algorithms, run more of them in this section
            // to avoid lock contention from creating block templates. Use a separate counter
            // nMidLoopCount to keep track of this, so that we can check every nInnerLoopCount hashes
            // if the block is done.
            int nMidTries = 0;
            static const int nMidLoopCount = 0x1000;
            if (pblock->IsProgPow() && pblock->nTime >= Params().PowUpdateTimestamp()) {
                // Check the target the same way CheckProofOfWork will
                arith_uint256 bnTarget;
                bool fNegative, fOverflow;
                bnTarget.SetCompact(pblock->nBits, &fNegative, &fOverflow);
                if (fNegative || bnTarget == 0 || fOverflow ||
                    bnTarget > UintToArith256(Params().GetConsensus().powLimit)) {
                    LogPrint(BCLog::MINING, "%s: invalid ProgPow target in template\n", __func__);
                    continue;
                }

                std::shared_ptr<ethash::epoch_context_full> ctxFull;
                std::shared_ptr<ethash::epoch_context> ctxLight;
                GetProgPowMiningContext(pblock->nHeight, ctxFull, ctxLight);
                if (!ctxFull && !ctxLight)
                    continue;

                // The ProgPow header hash does not cover the nonce, so hash it
                // once per template instead of once per attempt.
                const auto header_hash = to_hash256(pblock->GetProgPowHeaderHash().GetHex());
                // CheckProofOfWork accepts hash < target while the searcher
                // accepts hash <= boundary, so pass target minus one.
                const auto boundary = to_hash256(ArithToUint256(bnTarget - 1).GetHex());

                // Search in chunks so the thread stays interruptible, the live
                // hash counter keeps moving and a stale template gets dropped.
                const int nChunkSize = ctxFull ? 256 : 16;
                while (nTries < nInnerLoopCount) {
                    boost::this_thread::interruption_point();
                    if (chainActive.Height() >= pblock->nHeight)
                        break;
                    const int nChunk = std::min(nChunkSize, nInnerLoopCount - nTries);
                    progpow::search_result res;
                    if (ctxFull)
                        res = progpow::search(*ctxFull, pblock->nHeight, header_hash, boundary,
                                              pblock->nNonce64, nChunk);
                    else
                        res = progpow::search_light(*ctxLight, pblock->nHeight, header_hash, boundary,
                                                    pblock->nNonce64, nChunk);
                    if (res.solution_found) {
                        const int nUsed = static_cast<int>(res.nonce - pblock->nNonce64) + 1;
                        CountHashesMined(nUsed);
                        nTries += nUsed;
                        pblock->nNonce64 = res.nonce;
                        pblock->mixHash = uint256S(to_hex(res.mix_hash));
                        success = true;
                        break;
                    }
                    CountHashesMined(nChunk);
                    nTries += nChunk;
                    pblock->nNonce64 += nChunk;
                }
            } else if (pblock->IsSha256D() && pblock->nTime >= Params().PowUpdateTimestamp()) {
                uint256 midStateHash = pblock->GetSha256dMidstate();
                // Exit loop when nMidLoopCount loops are done, or when a new block is found.
                // Either way, success will be false.
                while (nMidTries < nMidLoopCount && chainActive.Height() < pblock->nHeight) {
                    while (nTries < nInnerLoopCount &&
                           !CheckProofOfWork(pblock->GetSha256D(midStateHash), pblock->nBits,
                                             Params().GetConsensus(), CBlockHeader::SHA256D_BLOCK)) {
                        boost::this_thread::interruption_point();
                        ++nTries;
                        ++pblock->nNonce64;
                    }
                    CountHashesMined(nTries);
                    if (nTries != nInnerLoopCount) {
                        success = true;
                        break;
                    }
                    ++nMidTries;
                    nTries = 0;
                }
            } else if (pblock->nTime < Params().PowUpdateTimestamp()) {
                while (nTries < nInnerLoopCount &&
                       !CheckProofOfWork(pblock->GetX16RTPoWHash(), pblock->nBits, Params().GetConsensus())) {
                    boost::this_thread::interruption_point();
                    ++nTries;
                    ++pblock->nNonce;
                    CountHashesMined(1);
                }
                success = nTries != nInnerLoopCount;
            } else {
                LogPrintf("%s: Unknown hashing algorithm found!\n", __func__);
                return;
            }

            double nHashSpeed = 0;
            {
                LOCK(cs_nonce);
                nHashes += nTries + (nMidTries * nInnerLoopCount);
                nTimeDuration = GetTime() - nTimeStart;
                if (!nTimeDuration) nTimeDuration = 1;
                nHashSpeed = arith_uint256(nHashes/1000/nTimeDuration).getdouble();
            }
            if (pThreadHashSpeed != nullptr) {
                double nRecentHashSpeed = 0;
                {
                    LOCK(pThreadHashSpeed->cs);
                    pThreadHashSpeed->nHashes = nTries + (nMidTries * nInnerLoopCount);
                    pThreadHashSpeed->nTimeDuration = std::max<int32_t>(GetTime() - nLocalStartTime, 1);
                    nRecentHashSpeed = pThreadHashSpeed->nHashes.getdouble() / 1000 / pThreadHashSpeed->nTimeDuration;
                }
                LogPrint(BCLog::MINING, "%s: PoW Hashspeed %d kh/s (this thread this round: %.03f khs)\n", __func__,  nHashSpeed, nRecentHashSpeed);
            } else {
                LogPrint(BCLog::MINING, "%s: PoW Hashspeed %d kh/s\n", __func__,  nHashSpeed);
            }

            if (!success) {
                continue;
            }
        }

        std::shared_ptr<const CBlock> shared_pblock = std::make_shared<const CBlock>(*pblock);
        if (!ProcessNewBlock(Params(), shared_pblock, true, nullptr)) {
            LogPrint(BCLog::MINING, "%s: Failed to process new block\n", __func__);
            continue;
        }
        LogPrint(BCLog::MINING, "%s: Found block\n", __func__);

        if (!fProofOfStake) {
            coinbaseScript->KeepScript();
            ++nSessionBlocksFound;
            nLastBlockFoundTime = GetTime();
        }
    }
}

void BitcoinRandomXMiner(std::shared_ptr<CReserveScript> coinbaseScript, int vm_index, uint32_t startNonce, ThreadHashSpeed* pThreadHashSpeed) {
    LogPrintf("Veil RandomX Miner started\n");

    unsigned int nExtraNonce = 0;
    static const int nInnerLoopCount = RANDOMX_INNER_LOOP_COUNT;
    int32_t nLocalStartTime = 0;
    bool fBlockFoundAlready = false;

    while (GenerateActive())
    {
        boost::this_thread::interruption_point();

        if (GenerateActive()) { // If the miner was turned on and we are in IsInitialBlockDownload(), sleep 60 seconds, before trying again
            if (IsInitialBlockDownload() && !gArgs.GetBoolArg("-genoverride", false)) {
                UninterruptibleSleep(std::chrono::milliseconds{60000});
                continue;
            }
        }

        CScript scriptMining;
        if (coinbaseScript)
            scriptMining = coinbaseScript->reserveScript;
        std::unique_ptr<CBlockTemplate> pblocktemplate(BlockAssembler(Params()).CreateNewBlock(scriptMining, false));
        if (!pblocktemplate || !pblocktemplate.get())
            continue;
        if (!(pblocktemplate->nFlags & TF_SUCCESS)) {
            continue;
        }

        if (fKeyBlockedChanged)
            continue;

        CBlock *pblock = &pblocktemplate->block;

        {
            LOCK(cs_nonce);
            nExtraNonce = nNonce_base++;
            nLocalStartTime = GetTime();
            if (!nTimeStart)
                nTimeStart = nLocalStartTime;
        }

        pblock->nNonce = startNonce;
        IncrementExtraNonce(pblock, chainActive.Height(), nExtraNonce);

        int nTries = 0;
        if (pblock->IsRandomX() && pblock->nTime >= Params().PowUpdateTimestamp()) {
            arith_uint256 bnTarget;
            bool fNegative;
            bool fOverflow;

            if (fKeyBlockedChanged || CheckIfMiningKeyShouldChange(GetKeyBlock(pblock->nHeight))) {
                fKeyBlockedChanged = true;
                continue;
            }

            bnTarget.SetCompact(pblock->nBits, &fNegative, &fOverflow);

            // Hoist things that do not change while we grind this template. The
            // key block only rotates every KEY_CHANGE blocks and a new tip lands
            // only occasionally, so check both on an interval rather than taking
            // cs_randomx_validator and walking the chain on every hash. That lock,
            // taken once per hash by every mining thread, was the main thing
            // throttling multi threaded RandomX. The network id never changes.
            const bool fRegtest = Params().NetworkIDString() == "regtest";
            const int nCheckInterval = 128;
            // Start at 1 so the first pass through the loop checks. Starting at
            // the interval would spend the first 128 hashes on a template
            // without once asking whether the tip or the key block moved.
            int nSinceCheck = 1;

            while (nTries < nInnerLoopCount) {
                boost::this_thread::interruption_point();

                if (--nSinceCheck <= 0) {
                    nSinceCheck = nCheckInterval;
                    if (fKeyBlockedChanged || CheckIfMiningKeyShouldChange(GetKeyBlock(pblock->nHeight))) {
                        fKeyBlockedChanged = true;
                        break;
                    }
                    if (pblock->nHeight <= chainActive.Height()) {
                        fBlockFoundAlready = true;
                        break;
                    }
                }

                char hash[RANDOMX_HASH_SIZE];
                // Build the header_hash (covers the nonce, so it must be rebuilt each try)
                uint256 nHeaderHash = pblock->GetRandomXHeaderHash();

                randomx_calculate_hash(vecRandomXVM[vm_index], &nHeaderHash, sizeof uint256(), hash);
                CountHashesMined(1);

                // Bypass regtest check, actually allows us to generate blocks in regtest mode instantly
                if (fRegtest) {
                    break;
                }

                // Check proof of work matches claimed amount
                if (UintToArith256(RandomXHashToUint256(hash)) < bnTarget) {
                    break;
                }

                ++nTries;
                ++pblock->nNonce;
            }
        }

        double nHashSpeed = 0;
        {
            LOCK(cs_nonce);
            nTimeDuration = GetTime() - nTimeStart;
            if (!nTimeDuration) nTimeDuration = 1;
            {
                nHashes += nTries;
                nHashSpeed = arith_uint256(nHashes/nTimeDuration).getdouble();
            }
        }
        if (pThreadHashSpeed != nullptr) {
            double nRecentHashSpeed = 0;
            {
                LOCK(pThreadHashSpeed->cs);
                pThreadHashSpeed->nHashes = nTries;
                pThreadHashSpeed->nTimeDuration = std::max<int32_t>(GetTime() - nLocalStartTime, 1);
                nRecentHashSpeed = pThreadHashSpeed->nHashes.getdouble() / pThreadHashSpeed->nTimeDuration;
            }
            LogPrint(BCLog::MINING, "%s: RandomX PoW Hashspeed %d hashes/s (this thread this round: %.03f hashes/s\n", __func__, nHashSpeed, nRecentHashSpeed);
        } else {
            LogPrint(BCLog::MINING, "%s: RandomX PoW Hashspeed %d hashes/s\n", __func__, nHashSpeed);
        }

        if (nTries == nInnerLoopCount) {
            continue;
        }

        if (fBlockFoundAlready) {
            fBlockFoundAlready = false;
            continue;
        }

        std::shared_ptr<const CBlock> shared_pblock = std::make_shared<const CBlock>(*pblock);
        if (!ProcessNewBlock(Params(), shared_pblock, true, nullptr)) {
            LogPrint(BCLog::MINING, "%s: Failed to process new block\n", __func__);
            continue;
        }

        coinbaseScript->KeepScript();
        ++nSessionBlocksFound;
        nLastBlockFoundTime = GetTime();
    }
}

void static ThreadBitcoinMiner(std::shared_ptr<CReserveScript> coinbaseScript, ThreadHashSpeed* pThreadHashSpeed)
{
    LogPrintf("%s: starting\n", __func__);
    boost::this_thread::interruption_point();
    try {
        BitcoinMiner(coinbaseScript, false, false, pThreadHashSpeed);
        boost::this_thread::interruption_point();
    } catch (std::exception& e) {
        LogPrintf("%s: exception\n", __func__);
    } catch (boost::thread_interrupted) {
       LogPrintf("%s: interrupted\n", __func__);
    }

    LogPrintf("%s: exiting\n", __func__);
}

void ThreadRandomXBitcoinMiner(std::shared_ptr<CReserveScript> coinbaseScript, const int vm_index, const uint32_t startNonce)
{
    LogPrintf("%s: starting\n", __func__);
    boost::this_thread::interruption_point();
    try {
        ThreadHashSpeed* pThreadHashSpeed = nullptr;
        {
            LOCK(cs_hashspeeds);
            if (0 <= vm_index && vm_index < vHashSpeeds.size())
                pThreadHashSpeed = &vHashSpeeds[vm_index];
        }
        BitcoinRandomXMiner(coinbaseScript, vm_index, startNonce, pThreadHashSpeed);
        boost::this_thread::interruption_point();
    } catch (std::exception& e) {
        LogPrintf("%s: exception\n", __func__);
    } catch (boost::thread_interrupted) {
       LogPrintf("%s: interrupted\n", __func__);
    }

    LogPrintf("%s: exiting\n", __func__);
}

void ThreadStakeMiner()
{
    LogPrintf("%s: starting\n", __func__);
    while (true) {
        boost::this_thread::interruption_point();
        if (ShutdownRequested())
            break;
        try {
            std::shared_ptr<CReserveScript> coinbase_script;
            bool fProofOfFullNode = true;
            BitcoinMiner(coinbase_script, true, fProofOfFullNode);
            boost::this_thread::interruption_point();
        } catch (std::exception& e) {
            LogPrintf("%s: exception\n", __func__);
        } catch (boost::thread_interrupted) {
            LogPrintf("%s: interrupted\n", __func__);
        }
    }
    mapHashedBlocks.clear();
    LogPrintf("%s: exiting\n", __func__);
}

boost::thread_group* pthreadGroupPoW;
void LinkPoWThreadGroup(void* pthreadgroup)
{
    pthreadGroupPoW = (boost::thread_group*)pthreadgroup;
}

boost::thread_group* pthreadGroupRandomX;
void LinkRandomXThreadGroup(void* pthreadgroup)
{
    pthreadGroupRandomX = (boost::thread_group*)pthreadgroup;
}

void GenerateBitcoins(bool fGenerate, int nThreads, std::shared_ptr<CReserveScript> coinbaseScript)
{
    if (!pthreadGroupPoW) {
        error("%s: pthreadGroupPoW is null! Cannot mine.", __func__);
        return;
    }
    setGenerate(fGenerate);

    if (nThreads < 0) {
        // In regtest threads defaults to 1
        nThreads = 1;
    }

    // Set a minimum of 4 threads when mining randomx
    if (GetMiningAlgorithm() == MINE_RANDOMX && nThreads < 4) {
        nThreads = 4;
    }

    // Close any active mining threads before starting new threads
    if (pthreadGroupPoW->size() > 0) {
        pthreadGroupPoW->interrupt_all();
        pthreadGroupPoW->join_all();

        DeallocateVMVector();
        DeallocateDataSet();
    }

    if (pthreadGroupRandomX->size() > 0) {
        pthreadGroupRandomX->interrupt_all();
        pthreadGroupRandomX->join_all();
    }

    SetBuildingMinerDataset(false);
    FreeProgPowMiningContext();

    if (nThreads == 0 || !fGenerate)
        return;

    LOCK(cs_hashspeeds);
    vHashSpeeds.resize(nThreads);

    // XXX - Todo - find a way to clean out the old threads or reuse the threads already created
    if (GetMiningAlgorithm() == MINE_RANDOMX && GetTime() >= Params().PowUpdateTimestamp()) {
        pthreadGroupRandomX->create_thread(boost::bind(&StartRandomXMining, pthreadGroupPoW,
                                           nThreads, coinbaseScript));
    } else {
        for (int i = 0; i < nThreads; i++)
            pthreadGroupPoW->create_thread(boost::bind(&ThreadBitcoinMiner, coinbaseScript, &vHashSpeeds[i]));
    }
}

int GetMiningAlgorithm() {
    return nMiningAlgorithm;
}

bool SetMiningAlgorithm(const std::string& algo, bool fSet) {
    int setAlgo = -1;

    if (algo == PROGPOW_STRING)      setAlgo = MINE_PROGPOW;
    else if (algo == SHA256D_STRING) setAlgo = MINE_SHA256D;
    else if (algo == RANDOMX_STRING) setAlgo = MINE_RANDOMX;

    if (setAlgo != -1) {
        if (fSet) nMiningAlgorithm = setAlgo;
        return true;
    }

    return false;
}
