#include <qt/veil/settings/settingsfaq09.h>
#include <qt/veil/forms/ui_settingsfaq09.h>

SettingsFaq09::SettingsFaq09(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::SettingsFaq09)
{
    ui->setupUi(this);

    ui->label_2->setText("VEIL secures its network with an exclusively anonymous Zerocoin-based Proof-of-Stake system. Basecoin VEIL cannot stake. Only Zerocoin VEIL can stake.<br><br>"

                         "<font color=#105aef>Follow these simple steps to begin staking VEIL:</font><br><br>"

                            "1. Send VEIL to the QR code or receiving address displayed in the overview tab to deposit VEIL to yourself.<br>"
                            "2. Click the Staking Toggle at the bottom left of the Overview screen and input your password to unlock your wallet for Staking/Autominting.<br>"
                            "3. Allow the wallet to automatically mind your Basecoin VEIL into Zerocoin VEIL.<br>"
                            "4. After newly minted Zerocoin VEIL receives 200 confirmations, it will begin staking and can earn staking rewards. <br>"
                         "5. Ensure the staking icon at the bottom left of the Overview screen is turned on. Exiting your wallet will stop staking.");

    ui->label_2->setTextFormat( Qt::RichText );
    ui->label_2->setWordWrap(true);
}

SettingsFaq09::~SettingsFaq09()
{
    delete ui;
}
