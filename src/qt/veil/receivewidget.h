#ifndef RECEIVEWIDGET_H
#define RECEIVEWIDGET_H

#include <QWidget>
#include <interfaces/wallet.h>

class WalletModel;
class ClientModel;
class WalletView;

namespace Ui {
class ReceiveWidget;
}

class ReceiveWidget : public QWidget
{
    Q_OBJECT

public:
    explicit ReceiveWidget(QWidget *parent = nullptr, WalletView *mainWindow = nullptr);
    ~ReceiveWidget();

    virtual void showEvent(QShowEvent *event) override;
    virtual void hideEvent(QHideEvent *event) override;
    void setWalletModel(WalletModel *model);


    void refreshWalletStatus();

public Q_SLOTS:
    void on_btnCopyAddress_clicked();
    void generateNewAddressClicked();

private:
    Ui::ReceiveWidget *ui;
    ClientModel *clientModel;
    WalletModel *walletModel;
    WalletView *mainWindow;

    CPubKey newKey;
    QString qAddress;

    bool generateNewAddress();
};

#endif // RECEIVEWIDGET_H
