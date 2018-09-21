#ifndef VEIL_BUDGET_H
#define VEIL_BUDGET_H

#include <amount.h>
#include <string>

class CTransaction;
class CValidationState;

namespace veil {

/** Required budget payment validity checks */
bool CheckBudgetTransaction(const CTransaction& tx, CValidationState& state);

class BudgetParams
{
private:
    explicit BudgetParams(std::string strNetwork);

    std::string budgetAddress;
    CAmount nBudgetAmount;
public:
    static BudgetParams* Get();
    std::string GetBudgetAddress() const { return budgetAddress; }
    CAmount GetBudgetAmount() const { return nBudgetAmount; }
};

BudgetParams& Budget();

}

#endif //VEIL_BUDGET_H
