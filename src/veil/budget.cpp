#include <veil/budget.h>

#include <consensus/validation.h>
#include <key_io.h>

namespace veil {

bool CheckBudgetTransaction(const int nHeight, const CTransaction& tx, CValidationState& state)
{
    CAmount nBlockReward, nFounderPayment, nLabPayment, nBudgetPayment;
    veil::Budget().GetBlockRewards(nHeight, nBlockReward, nFounderPayment, nLabPayment, nBudgetPayment);

    if (nHeight % BudgetParams::nBlocksPerPeriod)
        return true;

    // Verify that the amount paid to the budget address is correct
    if (tx.vpout.size() != 2) {
        return state.DoS(100, false, REJECT_INVALID, "bad-vpout-size");
    }

    if (tx.vpout[1]->nVersion != OUTPUT_STANDARD) {
        return state.DoS(100, false, REJECT_INVALID, "non-standard-budget-output");
    }

    auto txout = (CTxOutStandard*) tx.vpout[1].get();
    if (txout->nValue != Budget().GetBudgetAmount()) {
        return false;
    }

    // Verify that the second output of the coinbase transaction goes to the budget address
    std::string strBudgetAddress = Budget().GetBudgetAddress(); // KeyID for now
    CTxDestination dest = DecodeDestination(strBudgetAddress);
    auto budgetScript = GetScriptForDestination(dest);

    if (txout->scriptPubKey != budgetScript) {
        return state.DoS(10, false, REJECT_INVALID, "bad-budget-output");
    }

    return true;
}

/**
 * Rewards are generated once a month. If not the right block height then the rewards are set to 0.
 * Rewards are based upon the height of the block.
 * @param nBlockHeight
 */
void BudgetParams::GetBlockRewards(int nBlockHeight, CAmount& nBlockReward,
        CAmount& nFounderPayment, CAmount& nLabPayment, CAmount& nBudgetPayment)
{

    if (nBlockHeight <= 0) { // 43830 is the average size of a month in minutes when including leap years
        nBlockReward = 0;
        nFounderPayment = 0;
        nLabPayment = 0;
        nBudgetPayment = 0;
    } else if (nBlockHeight >= 1 && nBlockHeight <= 381599) {

        nBlockReward = 50;
        if((nBlockHeight % nBlocksPerPeriod) == 0) {
            nFounderPayment = 10 * nBlocksPerPeriod;
            nLabPayment = 10 * nBlocksPerPeriod;
            nBudgetPayment = 30 * nBlocksPerPeriod;
        } else {
            nFounderPayment = nLabPayment = nBudgetPayment = 0;
        }

    } else if (nBlockHeight >= 381600 && nBlockHeight <= 763199) {

        nBlockReward = 40;
        if((nBlockHeight % nBlocksPerPeriod) == 0) {
            nFounderPayment = 8 * nBlocksPerPeriod;
            nLabPayment = 8 * nBlocksPerPeriod;
            nBudgetPayment = 24 * nBlocksPerPeriod;
        } else {
            nFounderPayment = nLabPayment = nBudgetPayment = 0;
        }

    } else if (nBlockHeight >= 763200 && nBlockHeight <= 1144799) {

        nBlockReward = 30;
        if((nBlockHeight % nBlocksPerPeriod) == 0) {
            nFounderPayment = 6 * nBlocksPerPeriod;
            nLabPayment = 6 * nBlocksPerPeriod;
            nBudgetPayment = 18 * nBlocksPerPeriod;
        } else {
            nFounderPayment = nLabPayment = nBudgetPayment = 0;
        }

    } else if (nBlockHeight >= 1144800 && nBlockHeight <= 1526399) {

        nBlockReward = 20;
        if((nBlockHeight % nBlocksPerPeriod) == 0) {
            nFounderPayment = 4 * nBlocksPerPeriod;
            nLabPayment = 4 * nBlocksPerPeriod;
            nBudgetPayment = 12 * nBlocksPerPeriod;
        } else {
            nFounderPayment = nLabPayment = nBudgetPayment = 0;
        }

    } else if (nBlockHeight >= 1526400 && nBlockHeight <= 1907999) {

        nBlockReward = 10;
        if((nBlockHeight % nBlocksPerPeriod) == 0) {
            nFounderPayment = 2 * nBlocksPerPeriod;
            nLabPayment = 2 * nBlocksPerPeriod;
            nBudgetPayment = 6 * nBlocksPerPeriod;
        } else {
            nFounderPayment = nLabPayment = nBudgetPayment = 0;
        }

    } else {

        nBlockReward = 10;
        if((nBlockHeight % nBlocksPerPeriod) == 0) {
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
    if (strNetwork == "main") {
        budgetAddress = "8bf9de7aa440c87e9e3352fe3e74d579e3aa8049";
        nBudgetAmount = 100 * COIN;
    } else if (strNetwork == "test") {
        budgetAddress = "Budget Address testnet";
        nBudgetAmount = 100 * COIN;
    } else if (strNetwork == "regtest") {
        budgetAddress = "Budget Address regtest";
        nBudgetAmount = 100 * COIN;
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