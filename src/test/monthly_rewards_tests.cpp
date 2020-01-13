#include <chainparams.h>
#include <veil/budget.h>
#include <test/test_veil.h>
#include <boost/test/unit_test.hpp>


BOOST_FIXTURE_TEST_SUITE(monthly_rewards_tests, BasicTestingSetup)


/**
 *  Tests to make sure that the proper rewards are sent with the given block height
 */

BOOST_AUTO_TEST_CASE(testRewardAfterSet)
{
    CAmount nBlockReward, nFounderPayment, nFoundationPayment, nBudgetPayment = 0;
    int nBlocksPerPeriod = 43200;
    veil::Budget().GetBlockRewards(1, nBlockReward, nFounderPayment, nFoundationPayment, nBudgetPayment);

    BOOST_CHECK(nBlockReward == 50 * COIN);
    BOOST_CHECK(nFounderPayment == 0);
    BOOST_CHECK(nFoundationPayment == 0);
    BOOST_CHECK(nBudgetPayment == 0);

    veil::Budget().GetBlockRewards(2, nBlockReward, nFounderPayment, nFoundationPayment, nBudgetPayment);

    BOOST_CHECK(nBlockReward == 50 * COIN);
    BOOST_CHECK(nFounderPayment == 0);
    BOOST_CHECK(nFoundationPayment == 0);
    BOOST_CHECK(nBudgetPayment == 0);

    veil::Budget().GetBlockRewards(nBlocksPerPeriod, nBlockReward, nFounderPayment, nFoundationPayment, nBudgetPayment);

    BOOST_CHECK(nBlockReward == 50 * COIN);
    BOOST_CHECK(nFounderPayment == 10 * nBlocksPerPeriod * COIN);
    BOOST_CHECK(nFoundationPayment == 10 * nBlocksPerPeriod * COIN);
    BOOST_CHECK(nBudgetPayment == 30 * nBlocksPerPeriod * COIN);

    veil::Budget().GetBlockRewards(nBlocksPerPeriod + 1, nBlockReward, nFounderPayment, nFoundationPayment, nBudgetPayment);

    BOOST_CHECK(nBlockReward == 50 * COIN);
    BOOST_CHECK(nFounderPayment == 0);
    BOOST_CHECK(nFoundationPayment == 0);
    BOOST_CHECK(nBudgetPayment == 0);

    veil::Budget().GetBlockRewards(12 * nBlocksPerPeriod, nBlockReward, nFounderPayment, nFoundationPayment, nBudgetPayment);

    BOOST_CHECK(nBlockReward == 40 * COIN);
    BOOST_CHECK(nFounderPayment == 8 * nBlocksPerPeriod * COIN);
    BOOST_CHECK(nFoundationPayment == 8 * nBlocksPerPeriod * COIN);
    BOOST_CHECK(nBudgetPayment == 24 * nBlocksPerPeriod * COIN);

    veil::Budget().GetBlockRewards(12 * nBlocksPerPeriod + 1, nBlockReward, nFounderPayment, nFoundationPayment, nBudgetPayment);

    BOOST_CHECK(nBlockReward == 40 * COIN);
    BOOST_CHECK(nFounderPayment == 0);
    BOOST_CHECK(nFoundationPayment == 0);
    BOOST_CHECK(nBudgetPayment == 0);

    veil::Budget().GetBlockRewards(24 * nBlocksPerPeriod, nBlockReward, nFounderPayment, nFoundationPayment, nBudgetPayment);

    BOOST_CHECK(nBlockReward == 30 * COIN);
    BOOST_CHECK(nFounderPayment == 6 * nBlocksPerPeriod * COIN);
    BOOST_CHECK(nFoundationPayment == 6 * nBlocksPerPeriod * COIN);
    BOOST_CHECK(nBudgetPayment == 18 * nBlocksPerPeriod * COIN);

    veil::Budget().GetBlockRewards(24 * nBlocksPerPeriod + 1, nBlockReward, nFounderPayment, nFoundationPayment, nBudgetPayment);

    BOOST_CHECK(nBlockReward == 30 * COIN);
    BOOST_CHECK(nFounderPayment == 0);
    BOOST_CHECK(nFoundationPayment == 0);
    BOOST_CHECK(nBudgetPayment == 0);

    veil::Budget().GetBlockRewards(36 * nBlocksPerPeriod, nBlockReward, nFounderPayment, nFoundationPayment, nBudgetPayment);

    BOOST_CHECK(nBlockReward == 20 * COIN);
    BOOST_CHECK(nFounderPayment == 4 * nBlocksPerPeriod * COIN);
    BOOST_CHECK(nFoundationPayment == 4 * nBlocksPerPeriod * COIN);
    BOOST_CHECK(nBudgetPayment == 12 * nBlocksPerPeriod * COIN);

    veil::Budget().GetBlockRewards(36 * nBlocksPerPeriod + 1, nBlockReward, nFounderPayment, nFoundationPayment, nBudgetPayment);

    BOOST_CHECK(nBlockReward == 20 * COIN);
    BOOST_CHECK(nFounderPayment == 0);
    BOOST_CHECK(nFoundationPayment == 0);
    BOOST_CHECK(nBudgetPayment == 0);

    veil::Budget().GetBlockRewards(48 * nBlocksPerPeriod, nBlockReward, nFounderPayment, nFoundationPayment, nBudgetPayment);

    BOOST_CHECK(nBlockReward == 10 * COIN);
    BOOST_CHECK(nFounderPayment == 2 * nBlocksPerPeriod * COIN);
    BOOST_CHECK(nFoundationPayment == 2 * nBlocksPerPeriod * COIN);
    BOOST_CHECK(nBudgetPayment == 6 * nBlocksPerPeriod * COIN);

    veil::Budget().GetBlockRewards(48 * nBlocksPerPeriod + 1, nBlockReward, nFounderPayment, nFoundationPayment, nBudgetPayment);

    BOOST_CHECK(nBlockReward == 10 * COIN);
    BOOST_CHECK(nFounderPayment == 0);
    BOOST_CHECK(nFoundationPayment == 0);
    BOOST_CHECK(nBudgetPayment == 0);

    veil::Budget().GetBlockRewards(60 * nBlocksPerPeriod, nBlockReward, nFounderPayment, nFoundationPayment, nBudgetPayment);

    BOOST_CHECK(nBlockReward == 10 * COIN);
    BOOST_CHECK(nFounderPayment == 0);
    BOOST_CHECK(nFoundationPayment == 2 * nBlocksPerPeriod * COIN);
    BOOST_CHECK(nBudgetPayment == 8 * nBlocksPerPeriod * COIN);

    veil::Budget().GetBlockRewards(60 * nBlocksPerPeriod + 1, nBlockReward, nFounderPayment, nFoundationPayment, nBudgetPayment);

    BOOST_CHECK(nBlockReward == 10 * COIN);
    BOOST_CHECK(nFounderPayment == 0);
    BOOST_CHECK(nFoundationPayment == 0);
    BOOST_CHECK(nBudgetPayment == 0);
}

BOOST_AUTO_TEST_CASE(testRewardAfterSet_testnet)
{
    SelectParams("test");
    CAmount nBlockReward, nFounderPayment, nFoundationPayment, nBudgetPayment = 0;
    int nBlocksPerPeriod = 43200;
    veil::Budget().GetBlockRewards(1, nBlockReward, nFounderPayment, nFoundationPayment, nBudgetPayment);

    BOOST_CHECK(nBlockReward == 50 * COIN);
    BOOST_CHECK(nFounderPayment == 10 * nBlocksPerPeriod * COIN);
    BOOST_CHECK(nFoundationPayment == 10 * nBlocksPerPeriod * COIN);
    BOOST_CHECK(nBudgetPayment == 30 * nBlocksPerPeriod * COIN);

    veil::Budget().GetBlockRewards(2, nBlockReward, nFounderPayment, nFoundationPayment, nBudgetPayment);

    BOOST_CHECK(nBlockReward == 50 * COIN);
    BOOST_CHECK(nFounderPayment == 0);
    BOOST_CHECK(nFoundationPayment == 0);
    BOOST_CHECK(nBudgetPayment == 0);

    veil::Budget().GetBlockRewards(20000, nBlockReward, nFounderPayment, nFoundationPayment, nBudgetPayment);

    BOOST_CHECK(nBlockReward == 50 * COIN);
    BOOST_CHECK(nFounderPayment == 10 * nBlocksPerPeriod * COIN);
    BOOST_CHECK(nFoundationPayment == 10 * nBlocksPerPeriod * COIN);
    BOOST_CHECK(nBudgetPayment == 30 * nBlocksPerPeriod * COIN);

    veil::Budget().GetBlockRewards(20001, nBlockReward, nFounderPayment, nFoundationPayment, nBudgetPayment);

    BOOST_CHECK(nBlockReward == 50 * COIN);
    BOOST_CHECK(nFounderPayment == 0);
    BOOST_CHECK(nFoundationPayment == 0);
    BOOST_CHECK(nBudgetPayment == 0);

    veil::Budget().GetBlockRewards(12 * nBlocksPerPeriod + 20000, nBlockReward, nFounderPayment, nFoundationPayment, nBudgetPayment);

    BOOST_CHECK(nBlockReward == 40 * COIN);
    BOOST_CHECK(nFounderPayment == 8 * nBlocksPerPeriod * COIN);
    BOOST_CHECK(nFoundationPayment == 8 * nBlocksPerPeriod * COIN);
    BOOST_CHECK(nBudgetPayment == 24 * nBlocksPerPeriod * COIN);

    veil::Budget().GetBlockRewards(12 * nBlocksPerPeriod + 20001, nBlockReward, nFounderPayment, nFoundationPayment, nBudgetPayment);

    BOOST_CHECK(nBlockReward == 40 * COIN);
    BOOST_CHECK(nFounderPayment == 0);
    BOOST_CHECK(nFoundationPayment == 0);
    BOOST_CHECK(nBudgetPayment == 0);

    veil::Budget().GetBlockRewards(24 * nBlocksPerPeriod + 20000, nBlockReward, nFounderPayment, nFoundationPayment, nBudgetPayment);

    BOOST_CHECK(nBlockReward == 30 * COIN);
    BOOST_CHECK(nFounderPayment == 6 * nBlocksPerPeriod * COIN);
    BOOST_CHECK(nFoundationPayment == 6 * nBlocksPerPeriod * COIN);
    BOOST_CHECK(nBudgetPayment == 18 * nBlocksPerPeriod * COIN);

    veil::Budget().GetBlockRewards(24 * nBlocksPerPeriod + 20001, nBlockReward, nFounderPayment, nFoundationPayment, nBudgetPayment);

    BOOST_CHECK(nBlockReward == 30 * COIN);
    BOOST_CHECK(nFounderPayment == 0);
    BOOST_CHECK(nFoundationPayment == 0);
    BOOST_CHECK(nBudgetPayment == 0);

    veil::Budget().GetBlockRewards(36 * nBlocksPerPeriod + 20000, nBlockReward, nFounderPayment, nFoundationPayment, nBudgetPayment);

    BOOST_CHECK(nBlockReward == 20 * COIN);
    BOOST_CHECK(nFounderPayment == 4 * nBlocksPerPeriod * COIN);
    BOOST_CHECK(nFoundationPayment == 4 * nBlocksPerPeriod * COIN);
    BOOST_CHECK(nBudgetPayment == 12 * nBlocksPerPeriod * COIN);

    veil::Budget().GetBlockRewards(36 * nBlocksPerPeriod + 20001, nBlockReward, nFounderPayment, nFoundationPayment, nBudgetPayment);

    BOOST_CHECK(nBlockReward == 20 * COIN);
    BOOST_CHECK(nFounderPayment == 0);
    BOOST_CHECK(nFoundationPayment == 0);
    BOOST_CHECK(nBudgetPayment == 0);

    veil::Budget().GetBlockRewards(48 * nBlocksPerPeriod + 20000, nBlockReward, nFounderPayment, nFoundationPayment, nBudgetPayment);

    BOOST_CHECK(nBlockReward == 10 * COIN);
    BOOST_CHECK(nFounderPayment == 2 * nBlocksPerPeriod * COIN);
    BOOST_CHECK(nFoundationPayment == 2 * nBlocksPerPeriod * COIN);
    BOOST_CHECK(nBudgetPayment == 6 * nBlocksPerPeriod * COIN);

    veil::Budget().GetBlockRewards(48 * nBlocksPerPeriod + 20001, nBlockReward, nFounderPayment, nFoundationPayment, nBudgetPayment);

    BOOST_CHECK(nBlockReward == 10 * COIN);
    BOOST_CHECK(nFounderPayment == 0);
    BOOST_CHECK(nFoundationPayment == 0);
    BOOST_CHECK(nBudgetPayment == 0);

    veil::Budget().GetBlockRewards(60 * nBlocksPerPeriod + 20000, nBlockReward, nFounderPayment, nFoundationPayment, nBudgetPayment);

    BOOST_CHECK(nBlockReward == 10 * COIN);
    BOOST_CHECK(nFounderPayment == 0);
    BOOST_CHECK(nFoundationPayment == 2 * nBlocksPerPeriod * COIN);
    BOOST_CHECK(nBudgetPayment == 8 * nBlocksPerPeriod * COIN);

    veil::Budget().GetBlockRewards(60 * nBlocksPerPeriod + 20001, nBlockReward, nFounderPayment, nFoundationPayment, nBudgetPayment);

    BOOST_CHECK(nBlockReward == 10 * COIN);
    BOOST_CHECK(nFounderPayment == 0);
    BOOST_CHECK(nFoundationPayment == 0);
    BOOST_CHECK(nBudgetPayment == 0);


}

/**
 * Tests to make sure that in a 12 month period, 12 rewards have been
 * given
 */
BOOST_AUTO_TEST_CASE(countRwards)
{

    CAmount nBlockReward, nFounderPayment, nFoundationPayment, nBudgetPayment = 0;
    int nBlockRewardsCount = 0;
    int nRewardsCount = 0;

    for(int i = 0; i <= 518400; i++) {
        veil::Budget().GetBlockRewards(i, nBlockReward, nFounderPayment, nFoundationPayment, nBudgetPayment);
        if(nFounderPayment > 0)
            nRewardsCount++;
        if(nBlockReward > 0)
            nBlockRewardsCount++;
    }

    BOOST_CHECK(nRewardsCount == 12);
    BOOST_CHECK(nBlockRewardsCount == 518400);
}

BOOST_AUTO_TEST_SUITE_END()
