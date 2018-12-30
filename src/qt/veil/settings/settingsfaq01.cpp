#include <qt/veil/settings/settingsfaq01.h>
#include <qt/veil/forms/ui_settingsfaq01.h>

SettingsFaq01::SettingsFaq01(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::SettingsFaq01)
{
    ui->setupUi(this);

    ui->label_2->setText("Veil is a full-time anonymity cryptocurrency based on two of the most highly-vetted privacy protocols.<br><br>"

                         "When the wallet is unlocked for <font color=#105aef>staking/autominting</font> , it will automatically mint Basecoin veil (essentially non-Zerocoin veil) above 10 into Zerocoin veil denominations of 10, 100, 1,000, and 10,000.<br><br>"

                         "Minting newly received veil has a negligible fee to deter spam and may take 30-80 minutes (Manual Minting is faster), but has the benefit of Zerocoin anonymization and letting you to earn staking rewards (only Zerocoin veil can stake).<br><br>"

                         "Zerocoin veil is automatically selected for spending and offers the highest levels of anonymity by being off-chain and obscuring your veil with thousands of other Zerocoin veil denominations.<br><br>"

                         "Any Basecoin veil, such as change/dust leftover from minting and spending Zerocoin veil, can be spent with the Ring Confidential Transactions (RingCT) protocol. This prevents non-Zerocoin spends from ever compromising the privacy of your Zerocoin spends.<br><br>"

                         "To spend any Basecoin veil, select the Coin Control button in the Send tab, and either check the checkbox beside a UTXO or right-click/hold an UTXO and turn it into your single ”Non-Autominting Address”.<br><br>"

                         "If the wallet is for an exchange, merchant, or other services, please see here."); // This line does not appear in the wallet FAQs. If it did, the 'please see here' is not a link as intended

    ui->label_2->setTextFormat( Qt::RichText );
    ui->label_2->setWordWrap(true);

}

SettingsFaq01::~SettingsFaq01()
{
    delete ui;
}
