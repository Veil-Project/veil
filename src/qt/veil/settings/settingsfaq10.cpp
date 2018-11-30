#include <qt/veil/settings/settingsfaq10.h>
#include <qt/veil/forms/ui_settingsfaq10.h>

SettingsFaq10::SettingsFaq10(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::SettingsFaq10)
{
    ui->setupUi(this);

    ui->label->setText("The X16R mining algorithm will be used for at least the first 12 months of mainnet going live (December 8th, 2018) to achieve wide coin supply distribution.<br><br>"

                         "Anyone with NVIDIA or AMD graphics cards will be able to solo mine or pool mine VEIL without concerns about ASIC’s and mining centralization during the mining phase.<br><br>"

                         "For the latest mining instructions, please see the Official VEIL Announcement thread on the <font color=#105aef>BitcoinTalk forum</font>.");

    ui->label->setTextFormat( Qt::RichText );
    ui->label->setWordWrap(true);
}

SettingsFaq10::~SettingsFaq10()
{
    delete ui;
}
