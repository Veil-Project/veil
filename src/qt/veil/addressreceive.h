#ifndef ADDRESSRECEIVE_H
#define ADDRESSRECEIVE_H

#include <QWidget>
#include <QDialog>
#include <interfaces/wallet.h>

// TODO: Save address label.

class WalletModel;

namespace Ui {
class AddressReceive;
}

class AddressReceive : public QDialog
{
    Q_OBJECT

public:
    explicit AddressReceive(QWidget *parent = nullptr, WalletModel* _walletModel = nullptr, bool isMinerAddress = false);
    ~AddressReceive();
private Q_SLOTS:
    void onEscapeClicked();
    void on_btnCopyAddress_clicked();
    void onBtnSaveClicked();
private:
    Ui::AddressReceive *ui;
    WalletModel *walletModel;

    CPubKey newKey;
    QString qAddress;
    CTxDestination dest;

    void generateNewAddress(bool isMinerAddress);
};

#endif // ADDRESSRECEIVE_H
