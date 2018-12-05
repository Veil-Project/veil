#ifndef VEILSTATUSBAR_H
#define VEILSTATUSBAR_H

#include <QWidget>

class BitcoinGUI;
class WalletModel;

namespace Ui {
class VeilStatusBar;
}

class VeilStatusBar : public QWidget
{
    Q_OBJECT

public:
    explicit VeilStatusBar(QWidget *parent = 0, BitcoinGUI* gui = 0);
    ~VeilStatusBar();

    void updateSyncStatus(QString status);
    void setWalletModel(WalletModel *model);

private Q_SLOTS:
    void onBtnSyncClicked();
    void onBtnLockClicked();

private:
    Ui::VeilStatusBar *ui;
    BitcoinGUI* mainWindow;
    WalletModel *walletModel;
};

#endif // VEILSTATUSBAR_H
