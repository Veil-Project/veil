#include <qt/veil/settings/settingsfaq02.h>
#include <qt/veil/forms/ui_settingsfaq02.h>

SettingsFaq02::SettingsFaq02(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::SettingsFaq02)
{
    ui->setupUi(this);

    QPixmap imgLogoBittrex(210,76);

    imgLogoBittrex.load(":/icons/img-logo-bittrex");
    ui->imgBittrex->setPixmap(
                imgLogoBittrex.scaled(
                    210,
                    76,
                    Qt::KeepAspectRatio)
                );

    QPixmap imgLogoBinance(210,76);

    imgLogoBinance.load(":/icons/img-logo-binance");
    ui->imgBinance->setPixmap(
                imgLogoBinance.scaled(
                    210,
                    76,
                    Qt::KeepAspectRatio)
                );

    QPixmap imgLogoCoinbase(210,76);

    imgLogoCoinbase.load(":/icons/img-logo-coinbase");
    ui->imgCoinbase->setPixmap(
                imgLogoCoinbase.scaled(
                    210,
                    76,
                    Qt::KeepAspectRatio)
                );
}

SettingsFaq02::~SettingsFaq02()
{
    delete ui;
}
