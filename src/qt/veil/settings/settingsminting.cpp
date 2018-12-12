#include <qt/veil/settings/settingsminting.h>
#include <qt/veil/forms/ui_settingsminting.h>

#include <util.h>
#include <qt/bitcoinunits.h>
#include <qt/guiconstants.h>
#include <qt/guiutil.h>
#include <qt/optionsmodel.h>
#include <QIntValidator>

#include <QString>

SettingsMinting::SettingsMinting(QWidget *parent, WalletModel *walletModel) :
    QDialog(parent),
    ui(new Ui::SettingsMinting)
{
    ui->setupUi(this);
    ui->btnEsc->setProperty("cssClass" , "btn-text-primary-inactive");


    ui->btnSendMint->setProperty("cssClass" , "btn-text-primary");
    ui->btnSendMint->setText("MINT");


    ui->btnEsc->setProperty("cssClass" , "btn-text-primary-inactive");

    ui->editAmount->setPlaceholderText("Enter amount here");
    ui->editAmount->setAttribute(Qt::WA_MacShowFocusRect, 0);
    ui->editAmount->setProperty("cssClass" , "edit-primary");

    // Balance
    interfaces::Wallet& wallet = walletModel->wallet();
    interfaces::WalletBalances balances = wallet.getBalances();
    int unit = walletModel->getOptionsModel()->getDisplayUnit();
    ui->labelZVeilBalance->setText(BitcoinUnits::formatWithUnit(unit, balances.zerocoin_balance, false, BitcoinUnits::separatorAlways));
    ui->labelConvertable->setText(BitcoinUnits::formatWithUnit(unit, balances.balance, false, BitcoinUnits::separatorAlways));

    switch (nPreferredDenom){
        case 10:
            ui->radioButton10->setChecked(true);
            break;
        case 100:
            ui->radioButton100->setChecked(true);
            break;
        case 1000:
            ui->radioButton1000->setChecked(true);
            break;
        case 100000:
            ui->radioButton100000->setChecked(true);
            break;
    }

    //
    ui->errorMessage->setVisible(false);

    ui->editAmount->setValidator(new QIntValidator(0, 100000000000, this) );

    connect(ui->btnEsc,SIGNAL(clicked()),this, SLOT(close()));
    connect(ui->radioButton10, SIGNAL(toggled(bool)), this, SLOT(onCheck10Clicked(bool)));
    connect(ui->radioButton100, SIGNAL(toggled(bool)), this, SLOT(onCheck100Clicked(bool)));
    connect(ui->radioButton1000, SIGNAL(toggled(bool)), this, SLOT(onCheck1000Clicked(bool)));
    connect(ui->radioButton100000, SIGNAL(toggled(bool)), this, SLOT(onCheck100000Clicked(bool)));
    connect(ui->editAmount, SIGNAL(textChanged(const QString &)), this, SLOT(mintAmountChange(const QString &)));

}


void SettingsMinting::mintAmountChange(const QString &amount){

}

void SettingsMinting::onCheck10Clicked(bool res) {
    if(res && nPreferredDenom != 10){
        nPreferredDenom = 10;
    }
}

void SettingsMinting::onCheck100Clicked(bool res){
    if(res && nPreferredDenom != 100){
        nPreferredDenom = 100;
    }
}

void SettingsMinting::onCheck1000Clicked(bool res){
    if(res && nPreferredDenom != 1000){
        nPreferredDenom = 1000;
    }
}

void SettingsMinting::onCheck100000Clicked(bool res){
    if(res && nPreferredDenom != 100000){
        nPreferredDenom = 1000000;
    }
}

void SettingsMinting::onEscapeClicked(){
}

SettingsMinting::~SettingsMinting()
{
    delete ui;
}
