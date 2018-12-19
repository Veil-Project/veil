#ifndef UPDATEADDRESS_H
#define UPDATEADDRESS_H

#include <interfaces/wallet.h>
#include <qt/addresstablemodel.h>
#include <QWidget>
#include <QDialog>
#include <QString>

class WalletModel;
class AddressTableModel;

namespace Ui {
class UpdateAddress;
}

class UpdateAddress : public QDialog
{
    Q_OBJECT

public:
    explicit UpdateAddress(const QModelIndex &_index, QWidget *parent = nullptr,  WalletModel* _walletModel = nullptr,
                           AddressTableModel *_model = nullptr);
    ~UpdateAddress();
private Q_SLOTS:
    void onEscapeClicked();
    void onBtnSaveClicked();
private:
    Ui::UpdateAddress *ui;
    WalletModel *walletModel;
    QModelIndex index;
    QString addressStr;
    AddressTableModel *model;
};

#endif // UPDATEADDRESS_H
