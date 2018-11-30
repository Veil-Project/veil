#include <qt/veil/settings/settingsfaq06.h>
#include <qt/veil/forms/ui_settingsfaq06.h>

SettingsFaq06::SettingsFaq06(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::SettingsFaq06)
{
    ui->setupUi(this);

    ui->label_2->setText("<font color=#105aef>Single Non-Autominting Address</font><br><br>"

                         "The Single Non-Autominting Address allows users to avoid automatic minting of Basecoin VEIL into Zerocoin VEIL by keeping Basecoin VEIL inside it. To designate an address as their Single Non-Autominting Address, right-click/hold a transaction and convert it. The default wallet mode only allows one such address.<br><br>"

                         "<font color=#105aef>Exchange or Service mode</font><br><br>"

                         "Exchanges, merchants, and other services may want to avoid the Zerocoin autominting and/or staking process. By enabling Exchange or Service mode, the wallet will no longer automint or stake, with the advantage of only requiring ~6 minutes for received VEIL to become spendable again. It is highly discouraged for normal users to activate this mode due to the inability to stake and the relatively less anonymous RingCT transaction method.<br><br>"

                         "<font color=#105aef>To change to Exchange or Service mode, follow these instructions:</font><br><br>"

                            "1. Go to Settings>Open Configuration File<br>"
                            "2. Enter the following as a new line in the veil.config file: exchange-or-service=1<br>"
                            "3. Save the file and restart the wallet	<br><br>");

    ui->label_2->setTextFormat( Qt::RichText );
    ui->label_2->setWordWrap(true);
}

SettingsFaq06::~SettingsFaq06()
{
    delete ui;
}
