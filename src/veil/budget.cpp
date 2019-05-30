#include <veil/budget.h>

#include <consensus/validation.h>
#include <key_io.h>

namespace veil {

bool CheckBudgetTransaction(const int nHeight, const CTransaction& tx, CValidationState& state)
{
    if (!BudgetParams::IsSuperBlock(nHeight))
        return true;

    CAmount nBlockReward, nFounderPayment, nLabPayment, nBudgetPayment;
    veil::Budget().GetBlockRewards(nHeight, nBlockReward, nFounderPayment, nLabPayment, nBudgetPayment);

    // Verify that the superblock rewards are being payed out to the correct addresses with the correct amounts
    std::string strBudgetAddress = Budget().GetBudgetAddress(); // KeyID for now
    CTxDestination dest = DecodeDestination(strBudgetAddress);
    auto budgetScript = GetScriptForDestination(dest);

    std::string strLabAddress = Budget().GetLabAddress(); // KeyID for now
    CTxDestination destLab = DecodeDestination(strLabAddress);
    auto labScript = GetScriptForDestination(destLab);

    std::string strFounderAddress = Budget().GetFounderAddress(); // KeyID for now
    CTxDestination destFounder = DecodeDestination(strFounderAddress);
    auto founderScript = GetScriptForDestination(destFounder);

    bool fBudgetPayment = false;
    bool fLabPayment = false;
    bool fFounderPayment = !nFounderPayment;

    for (auto pOut : tx.vpout) {
        if (pOut->nVersion != OUTPUT_STANDARD)
            continue;

        auto txOut = (CTxOutStandard*)pOut.get();
        if (txOut->scriptPubKey == budgetScript) {
            if (txOut->nValue == nBudgetPayment) {
                fBudgetPayment = true;
            }
        } else if (txOut->scriptPubKey == labScript) {
            if (txOut->nValue == nLabPayment) {
                fLabPayment = true;
            }
        } else if (txOut->scriptPubKey == founderScript) {
            if (txOut->nValue == nFounderPayment)
                fFounderPayment = true;
        }
    }

    if (!fBudgetPayment)
        LogPrintf("%s: Expected budget payment not found in coinbase transaction\n", __func__);
    if (!fLabPayment)
        LogPrintf("%s: Expected lab payment not found in coinbase transaction\n", __func__);
    if (!fFounderPayment)
        LogPrintf("%s: Expected founder payment not found in coinbase transaction\n", __func__);

    return fBudgetPayment && fLabPayment && fFounderPayment;
}

/**
 * Rewards are generated once a month. If not the right block height then the rewards are set to 0.
 * Rewards are based upon the height of the block.
 * @param nBlockHeight
 */
bool BudgetParams::IsSuperBlock(int nBlockHeight)
{
    return (
            (Params().NetworkIDString() == "main" && nBlockHeight % nBlocksPerPeriod == 0) ||
            (Params().NetworkIDString() == "test" && (nBlockHeight % nBlocksPerPeriod == 20000 || nBlockHeight == 1))
            );
}

void BudgetParams::GetBlockRewards(int nBlockHeight, CAmount& nBlockReward,
        CAmount& nFounderPayment, CAmount& nLabPayment, CAmount& nBudgetPayment)
{

    if (nBlockHeight <= 0 || nBlockHeight > Params().HeightSupplyCreationStop()) { // 43830 is the average size of a month in minutes when including leap years
        nBlockReward = 0;
        nFounderPayment = 0;
        nLabPayment = 0;
        nBudgetPayment = 0;
    } else if (nBlockHeight >= 1 && nBlockHeight <= 518399) {

        nBlockReward = 50;
        if(IsSuperBlock(nBlockHeight)) {
            nFounderPayment = 10 * nBlocksPerPeriod;
            nLabPayment = 10 * nBlocksPerPeriod;
            nBudgetPayment = 30 * nBlocksPerPeriod;
        } else {
            nFounderPayment = nLabPayment = nBudgetPayment = 0;
        }

    } else if (nBlockHeight >= 518400 && nBlockHeight <= 1036799) {

        nBlockReward = 40;
        if(IsSuperBlock(nBlockHeight)) {
            nFounderPayment = 8 * nBlocksPerPeriod;
            nLabPayment = 8 * nBlocksPerPeriod;
            nBudgetPayment = 24 * nBlocksPerPeriod;
        } else {
            nFounderPayment = nLabPayment = nBudgetPayment = 0;
        }

    } else if (nBlockHeight >= 1036800 && nBlockHeight <= 1555199) {

        nBlockReward = 30;
        if(IsSuperBlock(nBlockHeight)) {
            nFounderPayment = 6 * nBlocksPerPeriod;
            nLabPayment = 6 * nBlocksPerPeriod;
            nBudgetPayment = 18 * nBlocksPerPeriod;
        } else {
            nFounderPayment = nLabPayment = nBudgetPayment = 0;
        }

    } else if (nBlockHeight >= 1555200 && nBlockHeight <= 2073599) {

        nBlockReward = 20;
        if(IsSuperBlock(nBlockHeight)) {
            nFounderPayment = 4 * nBlocksPerPeriod;
            nLabPayment = 4 * nBlocksPerPeriod;
            nBudgetPayment = 12 * nBlocksPerPeriod;
        } else {
            nFounderPayment = nLabPayment = nBudgetPayment = 0;
        }

    } else if (nBlockHeight >= 2073600 && nBlockHeight <= 2591999) {

        nBlockReward = 10;
        if(IsSuperBlock(nBlockHeight)) {
            nFounderPayment = 2 * nBlocksPerPeriod;
            nLabPayment = 2 * nBlocksPerPeriod;
            nBudgetPayment = 6 * nBlocksPerPeriod;
        } else {
            nFounderPayment = nLabPayment = nBudgetPayment = 0;
        }

    } else {

        nBlockReward = 10;
        if(IsSuperBlock(nBlockHeight)) {
            nFounderPayment = 0 * nBlocksPerPeriod;
            nLabPayment = 2 * nBlocksPerPeriod;
            nBudgetPayment = 8 * nBlocksPerPeriod;
        } else {
            nFounderPayment = nLabPayment = nBudgetPayment = 0;
        }

    }

    nBlockReward *= COIN;
    nFounderPayment *= COIN;
    nLabPayment *= COIN;
    nBudgetPayment *= COIN;
}

BudgetParams::BudgetParams(std::string strNetwork)
{
    // Addresses must decode to be different, otherwise CheckBudgetTransaction() will fail
    if (strNetwork == "main") {
        budgetAddress = "3MvD3sxedwPzGSdLnehegDfBGfxpdMevk2";
        founderAddress = "bv1qnauaweq25552zjthwqxq0puhz2flqrmhzh24h4";
        labAddress = "341PYScHCbq5Y3G3orR14V1pSmhb8qamP5";
    } else if (strNetwork == "test") {
        budgetAddress = "mxd3ciTteXZAna4q2as6z69mL6F7EncYjr";
        founderAddress = "mhurm1WXr8QXxMZXzJRH61TSjcaDviKfqY";
        labAddress = "mw4P7NPXLVdCCZME8EGqTxbD2b4nF8sg1S";
    } else if (strNetwork == "regtest") {
        budgetAddress = "2NCMc2apMAx5vY6wyYM8UyqCmfx2vpFmm1J";
        founderAddress = "2N7WZtWTdoFQjheviiGn4smykiwxNbHYdci";
        labAddress = "2N2iEAT5o7TY9yrz6Li4j91j5L59sLqdFSf";
    }
}

BudgetParams* BudgetParams::Get()
{
    std::string strNetwork = Params().NetworkIDString();
    static BudgetParams instance(strNetwork);
    return &instance;
}


BudgetParams& Budget()
{
    return *BudgetParams::Get();
}

}
