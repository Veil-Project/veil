#include <qt/veil/settings/settingsfaq08.h>
#include <qt/veil/forms/ui_settingsfaq08.h>

SettingsFaq08::SettingsFaq08(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::SettingsFaq08)
{
    ui->setupUi(this);

    ui->label->setText("Extreme Privacy Mode allows you to set a password to protect sensitive data stored on your device.<br><br> When enabled, the password will be required to view your balances, transactions, and addresses.<br><br>"

                       "Losing this password will not result in loss of funds.");
    ui->label->setTextFormat( Qt::RichText );
    ui->label->setWordWrap(true);
}

SettingsFaq08::~SettingsFaq08()
{
    delete ui;
}
