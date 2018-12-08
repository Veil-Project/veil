#ifndef ADDRESSNEWCONTACT_H
#define ADDRESSNEWCONTACT_H

#include <interfaces/wallet.h>

#include <QWidget>
#include <QDialog>

class WalletModel;

namespace Ui {
class AddressNewContact;
}

class AddressNewContact : public QDialog
{
    Q_OBJECT

public:
    explicit AddressNewContact(QWidget *parent = nullptr,  WalletModel* _walletModel = nullptr);
    ~AddressNewContact();
private Q_SLOTS:
    void onEscapeClicked();
    void onBtnSaveClicked();
private:
    Ui::AddressNewContact *ui;
    WalletModel *walletModel;
};

#endif // ADDRESSNEWCONTACT_H
