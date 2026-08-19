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
    int minThreads;
    int maxThreads;
    int nThreads;
    int currentMiningAlgo;
    int64_t nLastStatsUpdate = 0;

    // Recommended number of threads
    const int RECMAX = GetNumCores() - 1;

    CPubKey newKey;
    QString qAddress;

    bool displayAddressSet = false;
    CTxDestination currentDisplayAddress;

    void setMineActiveTxt(bool mineActive);
    void setThreadSelectionValues(int algo);
    void updateMiningStats();

    static QString formatHashRate(double dRate);
    static QString formatDifficulty(double dDiff);
    static QString formatTimeSpan(double dSeconds);

private Q_SLOTS:
    void onUpdateAlgorithm();
    void onToggleMiningActive();
    void onToggleProgPowDag(bool fChecked);
    void onUseMaxThreads();
    void onChangeNumberOfThreads(int newNumThr);
};

#endif // MININGWIDGET_H
