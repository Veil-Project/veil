#include <veil/budget.h>

#include <consensus/validation.h>
#include <key_io.h>

namespace veil {

bool CheckBudgetTransaction(const CTransaction& tx, CValidationState& state)
{
    // Verify that the amount paid to the budget address is correct
    if (tx.vout[1].nValue != Budget().GetBudgetAmount()) {
        return state.DoS(10, false, REJECT_INVALID, "bad-budget-amount");
    }

    // Verify that the second output of the coinbase transaction goes to the budget address
    std::string strBudgetAddress = Budget().GetBudgetAddress(); // KeyID for now
    CTxDestination dest = DecodeDestination(strBudgetAddress);
    auto budgetScript = GetScriptForDestination(dest);

    if (tx.vout[1].scriptPubKey != budgetScript) {
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

    if (nBlockHeight <= 0 || (nBlockHeight % 43830) != 0) { // 43830 is the average size of a month in minutes when including leap years
                                                            // 44640 is the length of a month when not including leap years
        nBlockReward = 0;
        nFounderPayment = 0;
        nLabPayment = 0;
        nBudgetPayment = 0;

    } else if (nBlockHeight >= 1 && nBlockHeight <= 381599) {

        nBlockReward = 50 * 43830;
        nFounderPayment = 10 * 43830;
        nLabPayment = 10 * 43830;
        nBudgetPayment = 30 * 43830;

    } else if (nBlockHeight >= 381600 && nBlockHeight <= 763199) {

        nBlockReward = 40 * 43830;
        nFounderPayment = 8 * 43830;
        nLabPayment = 8 * 43830;
        nBudgetPayment = 24 * 43830;
    } else if (nBlockHeight >= 763200 && nBlockHeight <= 1144799) {

        nBlockReward = 30 * 43830;
        nFounderPayment = 6 * 43830;
        nLabPayment = 6 * 43830;
        nBudgetPayment = 18 * 43830;

    } else if (nBlockHeight >= 1144800 && nBlockHeight <= 1526399) {

        nBlockReward = 20 * 43830;
        nFounderPayment = 4 * 43830;
        nLabPayment = 4 * 43830;
        nBudgetPayment = 12 * 43830;

    } else if (nBlockHeight >= 1526400 && nBlockHeight <= 1907999) {

        nBlockReward = 10 * 43830;
        nFounderPayment = 2 * 43830;
        nLabPayment = 2 * 43830;
        nBudgetPayment = 6 * 43830;

    } else {

        nBlockReward = 10 * 43830;
        nFounderPayment = 0 * 43830;
        nLabPayment = 2 * 43830;
        nBudgetPayment = 8 * 43830;

    }


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