#include <qt/veil/settings/settingsfaq04.h>
#include <qt/veil/forms/ui_settingsfaq04.h>

SettingsFaq04::SettingsFaq04(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::SettingsFaq04)
{
    ui->setupUi(this);

    ui->label_2->setText("Newly received veil requires ~6 minutes to fully confirm and become mintable (and spendable if using the “”Single Non-Autominting Address or Exchange-or-Service” mode).<br><br>"

                         "Newly minted Zerocoin veil takes ~20 minutes to become spendable. This delay is designed to protect users from ”timing attacks”, where their transactions are traceable by spending soon after minting.<br><br>"

                         "Autominting happens automatically when your wallet is unlocked for Staking/Autominting and will slowly mint one of each of the available Zerocoin denominations every 6-7 blocks. It is possible to mint Zerocoin all in one block with Manual Minting, possibly with a higher fee. To access it, go to Settings>Zerocoin Minting and input how many Basecoin veil you want to mint into Zerocoin veil.");

    ui->label_2->setTextFormat( Qt::RichText );
    ui->label_2->setWordWrap(true);
}

SettingsFaq04::~SettingsFaq04()
{
    delete ui;
}
