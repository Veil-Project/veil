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
    explicit AddressReceive(QWidget *parent = nullptr, WalletModel* _walletModel = nullptr);
    ~AddressReceive();
private Q_SLOTS:
    void onEscapeClicked();
    void on_btnCopyAddress_clicked();
private:
    Ui::AddressReceive *ui;
    WalletModel *walletModel;

    CPubKey newKey;
    QString qAddress;

    void generateNewAddress();
};

#endif // ADDRESSRECEIVE_H
