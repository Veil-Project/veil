#ifndef ADDRESSESWIDGET_H
#define ADDRESSESWIDGET_H

#include <qt/guiutil.h>
#include <qt/veil/qtutils.h>
#include <interfaces/wallet.h>
#include <memory>
#include <QWidget>
#include <qt/walletview.h>
#include <qt/addresstablemodel.h>
#include <qt/veil/addressesmenu.h>
#include <qt/veil/addressnewcontact.h>

class ClientModel;
class PlatformStyle;
class WalletModel;
class AddressViewDelegate;
class AddressSortFilterProxyModel;
class AddressTableModel;
class WalletView;
class AddressNewContact;
class AddressesMenu;
struct AddressTableEntry;

namespace Ui {
class AddressesWidget;
}


QT_BEGIN_NAMESPACE
class QModelIndex;
QT_END_NAMESPACE

class AddressesWidget : public QWidget
{
    Q_OBJECT

public:
    explicit AddressesWidget(const PlatformStyle *platformStyle, WalletView *parent = nullptr);
    ~AddressesWidget();

    void onForeground();

    void setWalletModel(WalletModel *model);
    void setModel(AddressTableModel *model);

    void refreshWalletStatus();

private Q_SLOTS:
    void onMyAddressClicked();
    void onContactsClicked();
    void onNewAddressClicked();
    void onNewMinerAddressClicked();
    void onButtonChanged();
    void handleAddressClicked(const QModelIndex &index);
    virtual void showEvent(QShowEvent *event) override;
    virtual void hideEvent(QHideEvent *event) override;

private:
    Ui::AddressesWidget *ui;
    WalletView *mainWindow = nullptr;
    ClientModel *clientModel = nullptr;
    WalletModel *walletModel = nullptr;
    AddressViewDelegate *addressViewDelegate = nullptr;
    AddressTableModel *model = nullptr;

    AddressSortFilterProxyModel *proxyModel = nullptr;
    AddressSortFilterProxyModel *proxyModelSend = nullptr;

    AddressesMenu *menu = nullptr;

    bool isOnMyAddresses;

    void initAddressesView();
    virtual void resizeEvent(QResizeEvent* event);
    void reloadTab(bool _isOnMyAddresses);
    void showList(bool show);

    void showHideMineAddressBtn(bool show);
};

#endif // ADDRESSESWIDGET_H
