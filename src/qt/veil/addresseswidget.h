#ifndef ADDRESSESWIDGET_H
#define ADDRESSESWIDGET_H

#include <qt/guiutil.h>
#include <qt/veil/qtutils.h>
#include <interfaces/wallet.h>
#include <memory>
#include <QWidget>
#include <qt/walletview.h>
#include <qt/addresstablemodel.h>
//#include <QtWidgets/QTableView>

class ClientModel;
class PlatformStyle;
class WalletModel;
class AddressViewDelegate;
class AddressSortFilterProxyModel;
class AddressTableModel;
class WalletView;
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

private Q_SLOTS:
    void onMyAddressClicked();
    void onContactsClicked();
    void onNewAddressClicked();
    void onButtonChanged();
    virtual void showEvent(QShowEvent *event) override;
    virtual void hideEvent(QHideEvent *event) override;

private:
    Ui::AddressesWidget *ui;
    WalletView *mainWindow;
    ClientModel *clientModel;
    WalletModel *walletModel;
    AddressViewDelegate *addressViewDelegate;
    AddressTableModel *model;

    AddressSortFilterProxyModel *proxyModel;
    AddressSortFilterProxyModel *proxyModelSend;

    bool isOnMyAddresses;

    void initAddressesView();
    virtual void resizeEvent(QResizeEvent* event);
    void reloadTab(bool _isOnMyAddresses);
    void showList(bool show);
};

#endif // ADDRESSESWIDGET_H
