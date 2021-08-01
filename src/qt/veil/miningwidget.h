// Copyright (c) 2021 The Veil developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef MININGWIDGET_H
#define MININGWIDGET_H

#include <QWidget>
#include <interfaces/wallet.h>

class WalletModel;
class WalletView;

namespace Ui {
class MiningWidget;
}

class MiningWidget : public QWidget
{
    Q_OBJECT

public:
    explicit MiningWidget(QWidget *parent = nullptr, WalletView *mainWindow = nullptr);
    ~MiningWidget();

    void setWalletModel(WalletModel *model);

public Q_SLOTS:
    void updateMiningFields();

private:
    Ui::MiningWidget *ui;
    WalletModel *walletModel;
    WalletView *mainWindow;
    bool mineOn;
    int maxThreads;
    int nThreads;
    int currentMiningAlgo;

    CPubKey newKey;
    QString qAddress;

    bool displayAddressSet = false;
    CTxDestination currentDisplayAddress;

    void setMineActiveTxt(bool mineActive);
    void setThreadSelectionValues(int algo);

private Q_SLOTS:
    void onUpdateAlgorithm();
    void onToggleMiningActive();
    void onUseMaxThreads();
};

#endif // MININGWIDGET_H
