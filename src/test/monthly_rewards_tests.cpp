
#include <veil/budget.h>
#include <test/test_veil.h>
#include <boost/test/unit_test.hpp>


BOOST_FIXTURE_TEST_SUITE(monthly_rewards_tests, BasicTestingSetup)


/**
 *  Tests to make suret that the proper rewards are sent with the given block height
 */

BOOST_AUTO_TEST_CASE(testRewardAfterSet)
{
    CAmount nBlockReward, nFounderPayment, nLabPayment, nBudgetPayment = 0;
    veil::Budget().GetBlockRewards(43830, nBlockReward, nFounderPayment, nLabPayment, nBudgetPayment);

    BOOST_CHECK(nBlockReward == 50);
    BOOST_CHECK(nFounderPayment == 10 * 43830);
    BOOST_CHECK(nLabPayment == 10 * 43830);
    BOOST_CHECK(nBudgetPayment == 30 * 43830);

    veil::Budget().GetBlockRewards(43831, nBlockReward, nFounderPayment, nLabPayment, nBudgetPayment);

    BOOST_CHECK(nBlockReward == 50);
    BOOST_CHECK(nFounderPayment == 0);
    BOOST_CHECK(nLabPayment == 0);
    BOOST_CHECK(nBudgetPayment == 0);

    veil::Budget().GetBlockRewards(525960, nBlockReward, nFounderPayment, nLabPayment, nBudgetPayment);

    BOOST_CHECK(nBlockReward == 40);
    BOOST_CHECK(nFounderPayment == 8 * 43830);
    BOOST_CHECK(nLabPayment == 8 * 43830);
    BOOST_CHECK(nBudgetPayment == 24 * 43830);

    veil::Budget().GetBlockRewards(525961, nBlockReward, nFounderPayment, nLabPayment, nBudgetPayment);

    BOOST_CHECK(nBlockReward == 40);
    BOOST_CHECK(nFounderPayment == 0);
    BOOST_CHECK(nLabPayment == 0);
    BOOST_CHECK(nBudgetPayment == 0);

    veil::Budget().GetBlockRewards(876600, nBlockReward, nFounderPayment, nLabPayment, nBudgetPayment);

    BOOST_CHECK(nBlockReward == 30);
    BOOST_CHECK(nFounderPayment == 6 * 43830);
    BOOST_CHECK(nLabPayment == 6 * 43830);
    BOOST_CHECK(nBudgetPayment == 18 * 43830);

    veil::Budget().GetBlockRewards(876601, nBlockReward, nFounderPayment, nLabPayment, nBudgetPayment);

    BOOST_CHECK(nBlockReward == 30);
    BOOST_CHECK(nFounderPayment == 0);
    BOOST_CHECK(nLabPayment == 0);
    BOOST_CHECK(nBudgetPayment == 0);

    veil::Budget().GetBlockRewards(1314900, nBlockReward, nFounderPayment, nLabPayment, nBudgetPayment);

    BOOST_CHECK(nBlockReward == 20);
    BOOST_CHECK(nFounderPayment == 4 * 43830);
    BOOST_CHECK(nLabPayment == 4 * 43830);
    BOOST_CHECK(nBudgetPayment == 12 * 43830);

    veil::Budget().GetBlockRewards(1314902, nBlockReward, nFounderPayment, nLabPayment, nBudgetPayment);

    BOOST_CHECK(nBlockReward == 20);
    BOOST_CHECK(nFounderPayment == 0);
    BOOST_CHECK(nLabPayment == 0);
    BOOST_CHECK(nBudgetPayment == 0);

    veil::Budget().GetBlockRewards(1534050, nBlockReward, nFounderPayment, nLabPayment, nBudgetPayment);

    BOOST_CHECK(nBlockReward == 10);
    BOOST_CHECK(nFounderPayment == 2 * 43830);
    BOOST_CHECK(nLabPayment == 2 * 43830);
    BOOST_CHECK(nBudgetPayment == 6 * 43830);

    veil::Budget().GetBlockRewards(1534051, nBlockReward, nFounderPayment, nLabPayment, nBudgetPayment);

    BOOST_CHECK(nBlockReward == 10);
    BOOST_CHECK(nFounderPayment == 0);
    BOOST_CHECK(nLabPayment == 0);
    BOOST_CHECK(nBudgetPayment == 0);

    veil::Budget().GetBlockRewards(1972350, nBlockReward, nFounderPayment, nLabPayment, nBudgetPayment);

    BOOST_CHECK(nBlockReward == 10);
    BOOST_CHECK(nFounderPayment == 0);
    BOOST_CHECK(nLabPayment == 2 * 43830);
    BOOST_CHECK(nBudgetPayment == 8 * 43830);

    veil::Budget().GetBlockRewards(1972351, nBlockReward, nFounderPayment, nLabPayment, nBudgetPayment);

    BOOST_CHECK(nBlockReward == 10);
    BOOST_CHECK(nFounderPayment == 0);
    BOOST_CHECK(nLabPayment == 0);
    BOOST_CHECK(nBudgetPayment == 0);


}

/**
 * Tests to make sure that in a 12 month period, 12 rewards have been
 * given
 */
BOOST_AUTO_TEST_CASE(countRwards)
{

    CAmount nBlockReward, nFounderPayment, nLabPayment, nBudgetPayment = 0;
    int nBlockRewardsCount = 0;
    int nRewardsCount = 0;

    for(int i = 0; i <= 525969; i++) {
        veil::Budget().GetBlockRewards(i, nBlockReward, nFounderPayment, nLabPayment, nBudgetPayment);
        if(nFounderPayment > 0)
            nRewardsCount++;
        if(nBlockReward > 0)
            nBlockRewardsCount++;
    }

    BOOST_CHECK(nRewardsCount == 12);
    BOOST_CHECK(nBlockRewardsCount == 525969);
}

BOOST_AUTO_TEST_SUITE_END()
