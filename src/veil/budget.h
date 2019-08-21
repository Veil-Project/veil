#ifndef VEIL_BUDGET_H
#define VEIL_BUDGET_H

#include <amount.h>
#include <string>

class CTransaction;
class CValidationState;

namespace veil {

/** Required budget payment validity checks */
bool CheckBudgetTransaction(const int nHeight, const CTransaction& tx, CValidationState& state);

class BudgetParams
{

private:
    explicit BudgetParams(std::string strNetwork);
    std::string budgetAddress_legacy;
    std::string budgetAddress;
    std::string founderAddress;
    std::string labAddress_legacy;
    std::string labAddress;
    int nHeightAddressChange;

public:
    static bool IsSuperBlock(int nBlockHeight);
    static BudgetParams* Get();

    static void GetBlockRewards(int nBlockHeight,
                                CAmount& nBlockReward,
                                CAmount& nFounderPayment,
                                CAmount& nLabPayment,
                                CAmount& nBudgetPayment);

    std::string GetBudgetAddress(int nHeight) const;
    std::string GetFounderAddress() const;
    std::string GetLabAddress(int nHeight) const;
    static const int nBlocksPerPeriod = 43200;
};

BudgetParams& Budget();

}

#endif //VEIL_BUDGET_H
