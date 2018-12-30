#include <qt/veil/settings/settingsfaq11.h>
#include <qt/veil/forms/ui_settingsfaq11.h>

SettingsFaq11::SettingsFaq11(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::SettingsFaq11)
{
    ui->setupUi(this);

    ui->label_2->setText("Anyone can earn veil through the Veil Bounty Program. A wide variety of tasks can be performed to earn veil, ranging from traditional bounty tasks, high reward contests, bug bounty, dev bounty, and adopting Veil as a merchant or service.<br><br>"

                         "For the latest information, please see the Official Veil Bounty Program thread on the <font color=#105aef>BitcoinTalk forum.</font>");

    ui->label_2->setTextFormat( Qt::RichText );
    ui->label_2->setWordWrap(true);
}

SettingsFaq11::~SettingsFaq11()
{
    delete ui;
}
