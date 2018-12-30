#include <qt/veil/settings/settingsfaq10.h>
#include <qt/veil/forms/ui_settingsfaq10.h>

SettingsFaq10::SettingsFaq10(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::SettingsFaq10)
{
    ui->setupUi(this);

    ui->label->setText("The X16RT mining algorithm will be used for at least the first 12 months of mainnet going live (January 1st, 2019) to achieve wide coin supply distribution.<br><br>"

                         "Anyone with NVIDIA or AMD graphics cards will be able to solo mine or pool mine veil without concerns about ASICs and mining centralization during the mining phase.<br><br>"

                         "For the latest mining instructions, please see the Official Veil Announcement thread on the <font color=#105aef>BitcoinTalk forum</font>.");

    ui->label->setTextFormat( Qt::RichText );
    ui->label->setWordWrap(true);
}

SettingsFaq10::~SettingsFaq10()
{
    delete ui;
}
