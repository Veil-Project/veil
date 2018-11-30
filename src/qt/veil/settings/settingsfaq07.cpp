#include <qt/veil/settings/settingsfaq07.h>
#include <qt/veil/forms/ui_settingsfaq07.h>

SettingsFaq07::SettingsFaq07(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::SettingsFaq07)
{
    ui->setupUi(this);

    ui->label_2->setText("VEIL wallets can automatically generate receiving addresses and QR codes which contain the receiving addresses.<br><br>"

                         "You can share your QR code with others so they can send VEIL to you, and others can send their QR code to you so you can send VEIL to them.<br><br>"

                         "The VEIL mobile app allows you to use your mobile device’s camera to scan QR codes, and send VEIL to the wallet that generated the QR code.<br><br>"

                         "There are also many QR code scanning mobile apps that can scan a VEIL QR Code and show you the address as text. This text address can be manually copied to clipboard and pasted in the ”Enter receiving VEIL address” field of your wallet’s Send tab.");

    ui->label_2->setTextFormat( Qt::RichText );
    ui->label_2->setWordWrap(true);
}

SettingsFaq07::~SettingsFaq07()
{
    delete ui;
}
