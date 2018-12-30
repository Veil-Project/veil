#include <qt/veil/settings/settingsfaq05.h>
#include <qt/veil/forms/ui_settingsfaq05.h>

SettingsFaq05::SettingsFaq05(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::SettingsFaq05)
{
    ui->setupUi(this);

    ui->label_2->setText("Zerocoin veil is used automatically when spending.<br><br>"

                         "<font color=#105aef>To spend Basecoin veil, follow these instructions:</font><br><br>"

                            "1. Navigate to the Send tab.<br>"
                            "2. Click the Coin Control button.<br>"
                            "3. Select all Basecoin UTXO inputs and send a transaction.<br><br>"

                         "All Basecoin transactions are anonymized with RingCT. Although RingCT transactions have an anonymity set size of 11 versus Zerocoin’s thousands, it prevents Basecoin from compromising the privacy of Zerocoin transactions.");

    ui->label_2->setTextFormat( Qt::RichText );
    ui->label_2->setWordWrap(true);
}

SettingsFaq05::~SettingsFaq05()
{
    delete ui;
}
