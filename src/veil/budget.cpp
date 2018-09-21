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