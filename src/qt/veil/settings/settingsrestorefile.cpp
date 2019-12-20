#include <qt/veil/settings/settingsrestorefile.h>
#include <qt/veil/forms/ui_settingsrestorefile.h>

#include <qt/guiutil.h>
#include <QDebug>
#include <QFileDialog>
#include <iostream>
#include <wallet/wallet.h>
#include <wallet/walletutil.h>

SettingsRestoreFile::SettingsRestoreFile(SettingsRestore *_parent, QWidget* widget) :
    QWidget(widget),
    ui(new Ui::SettingsRestoreFile),
    parent(_parent)
{
    ui->setupUi(this);
    ui->editPassword->setPlaceholderText("Enter password");
    ui->editPassword->setAttribute(Qt::WA_MacShowFocusRect, 0);

    connect(ui->btnFolder, SIGNAL(clicked()), this, SLOT(onRestoreClicked()));
}

void SettingsRestoreFile::onRestoreClicked(){
    //TODO: Import wallet from file. This code probably doesn't work.
    QFile walletFile(QFileDialog::getOpenFileName(0, "Choose wallet file"));
    if(!walletFile.exists())
        return;
    fs::path walletPath = walletFile.fileName().toStdString();
    CWallet::CreateWalletFromFile(walletFile.fileName().toStdString(), walletPath);
    parent->acceptFile();
}

SettingsRestoreFile::~SettingsRestoreFile()
{
    delete ui;
}
