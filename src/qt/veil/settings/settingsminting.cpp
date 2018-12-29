#include <qt/veil/settings/settingsminting.h>
#include <qt/veil/forms/ui_settingsminting.h>

#include <util.h>
#include <qt/bitcoinunits.h>
#include <qt/guiconstants.h>
#include <qt/walletmodel.h>
#include <qt/guiutil.h>
#include <qt/optionsmodel.h>
#include <QIntValidator>

#include <QString>
#include <iostream>
#include <qt/veil/qtutils.h>

SettingsMinting::SettingsMinting(QWidget *parent, WalletView *mainWindow, WalletModel *_walletModel) :
    QDialog(parent),
    mainWindow(mainWindow),
    walletModel(_walletModel),
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
    ui->labelConvertable->setText(BitcoinUnits::formatWithUnit(unit, balances.basecoin_balance, false, BitcoinUnits::separatorAlways));

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
    connect(ui->btnSendMint,SIGNAL(clicked()),this, SLOT(btnMint()));
    connect(ui->radioButton10, SIGNAL(toggled(bool)), this, SLOT(onCheck10Clicked(bool)));
    connect(ui->radioButton100, SIGNAL(toggled(bool)), this, SLOT(onCheck100Clicked(bool)));
    connect(ui->radioButton1000, SIGNAL(toggled(bool)), this, SLOT(onCheck1000Clicked(bool)));
    connect(ui->radioButton100000, SIGNAL(toggled(bool)), this, SLOT(onCheck100000Clicked(bool)));
    connect(ui->editAmount, SIGNAL(textChanged(const QString &)), this, SLOT(mintAmountChange(const QString &)));

}

/**
 * Parse a string into a number of base monetary units and
 * return validity.
 * @note Must return 0 if !valid.
 */
CAmount SettingsMinting::parseAmount(const QString &text, bool *valid_out) const {
    CAmount val = 0;
    bool valid = BitcoinUnits::parse(BitcoinUnits::VEIL, text, &val);
    if(valid)
    {
        if(val < 0 || val > BitcoinUnits::maxMoney())
            valid = false;
    }
    if(valid_out)
        *valid_out = valid;
    return valid ? val : 0;
}

void SettingsMinting::mintAmountChange(const QString &amount){

}

void SettingsMinting::btnMint(){
    mintzerocoins();
}

void SettingsMinting::mintzerocoins(){
    int64_t nTime = GetTimeMillis();

    // check if wallet is unlocked..
    bool isAmountValid;
    CAmount nAmount = parseAmount(ui->editAmount->text(), &isAmountValid);

    if(!isAmountValid){
        // notify user
    }

    interfaces::Wallet& wallet = walletModel->wallet();

    //CWalletTx wtx(&dynamic_cast<CWallet&>(wallet), nullptr);
    std::vector<CDeterministicMint> vDMints;
    std::string strError;
    std::vector<COutPoint> vOutpts;

    bool fAllowBasecoin = ui->useBasecoin->isChecked();

    strError = wallet.mintZerocoin(nAmount, vDMints, fAllowBasecoin, NULL);
    std::cout << "mint: " << strError << std::endl;
    if(strError.empty()){
        openToastDialog("Mint completed", this);
    } else{
        openToastDialog(QString::fromStdString(strError), this);
    }

    //if (strError != "")
        //throw JSONRPCError(RPC_WALLET_ERROR, strError);
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
    if(res && nPreferredDenom != 10000){
        nPreferredDenom = 10000;
    }
}

void SettingsMinting::onEscapeClicked(){
}

SettingsMinting::~SettingsMinting()
{
    delete ui;
}
