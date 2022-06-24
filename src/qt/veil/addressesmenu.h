// Copyright (c) 2019-2021 The Veil developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef ADDRESSESMENU_H
#define ADDRESSESMENU_H

#include <QWidget>
#include <qt/addresstablemodel.h>
#include <qt/walletview.h>
#include <qt/veil/updateaddress.h>

class WalletView;
class AddressTableModel;
class UpdateAddress;

namespace Ui {
class AddressesMenu;
}

class AddressesMenu : public QWidget
{
    Q_OBJECT

public:
    explicit AddressesMenu(
            const QString _type,
            const QModelIndex &_index,
            QWidget *parent = nullptr,
            WalletView *_mainWindow = nullptr,
            AddressTableModel *_model = nullptr);

    void setInitData(const QModelIndex &_index, AddressTableModel *_model,const QString _type);

    ~AddressesMenu();

public Q_SLOTS:
	virtual void showEvent(QShowEvent *event) override;
	virtual void enterEvent(QEvent *event) override;
	virtual void leaveEvent(QEvent *event) override;

private Q_SLOTS:
    void onBtnCopyAddressClicked();
    void onBtnEditAddressClicked();
    void onBtnDeleteAddressClicked();

private:
    Ui::AddressesMenu *ui;
    WalletView *mainWindow;
    QModelIndex index;
    QString type;
    AddressTableModel *model;
    QTimer *timeoutTimer;
};

#endif // ADDRESSESMENU_H
