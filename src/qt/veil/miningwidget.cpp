// Copyright (c) 2021-2026 The Veil developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qt/veil/miningwidget.h>
#include <qt/veil/forms/ui_miningwidget.h>

// SOME OF THESE NEED TO BE DELETED
#include <qt/guiconstants.h>
#include <qt/guiutil.h>
#include <qt/optionsmodel.h>
#include <qt/platformstyle.h>
#include <interfaces/node.h>
#include <qt/walletview.h>
#include <qt/walletmodel.h>
#include <key_io.h>
#include <wallet/wallet.h>
#include <QGraphicsOpacityEffect>
#include <QPropertyAnimation>
#include <QPixmap>
#include <QIcon>
#include <QClipboard>
#include <QMimeData>
#include <QImage>
#include <QLabel>
#include <QPainter>
#include <qt/veil/qtutils.h>
#include <miner.h>
#include <chainparams.h>
#include <pow.h>
#include <rpc/blockchain.h>
#include <validation.h>
#include <QDateTime>

MiningWidget::MiningWidget(QWidget *parent, WalletView* walletView) :
    QWidget(parent),
    ui(new Ui::MiningWidget),
    mainWindow(walletView) {
    ui->setupUi(this);
    ui->title->setProperty("cssClass" , "title");

    // SET THE NUMBER OF THREADS AND ALGORITHM HERE
    mineOn = GenerateActive();

    ui->cmbAlgoSelect->addItems({"RandomX","ProgPow","SHA256D"});
    ui->cmbAlgoSelect->setCurrentIndex(GetMiningAlgorithm());

    ui->chkProgPowDag->setChecked(GetProgPowFullDataset());
    connect(ui->chkProgPowDag, SIGNAL(toggled(bool)), this, SLOT(onToggleProgPowDag(bool)));

    ui->frame_8->setVisible(false);

    connect(ui->btnUpdateAlgo, SIGNAL(clicked()), this, SLOT(onUpdateAlgorithm()));
    connect(ui->btnAllThreads, SIGNAL(clicked()), this, SLOT(onUseMaxThreads()));
    connect(ui->btnActiveMine, SIGNAL(clicked()), this, SLOT(onToggleMiningActive()));
    connect(ui->numThreads, SIGNAL(valueChanged(int)), this, SLOT(onChangeNumberOfThreads(int)));
}

void MiningWidget::setMineActiveTxt(bool mineActive) {
    ui->lblMineActive->setText(mineActive ? "Mining Active" : "Mining Stopped");
    ui->lblMineActive->setStyleSheet(mineActive ? "QLabel{color:green;}" : "QLabel{color:red;}");
    ui->btnActiveMine->setText(mineActive ? "Stop Mining" : "Start Mining");
}

void MiningWidget::setWalletModel(WalletModel *model) {
    this->walletModel = model;

    connect(model, SIGNAL(updateMiningFields()), this, SLOT(updateMiningFields()));
}

MiningWidget::~MiningWidget()
{
    delete ui;
}

void MiningWidget::onUpdateAlgorithm() {
    if (mineOn) {
        openToastDialog(QString::fromStdString("Mining must be stopped to change algorithm!"), mainWindow->getGUI());
    } else {
        currentMiningAlgo = ui->cmbAlgoSelect->currentIndex(); 
        std::string algoStr = GetMiningType(currentMiningAlgo, false, false);
        bool setAlgoResult = SetMiningAlgorithm(algoStr); 

        if (setAlgoResult) {
            openToastDialog(QString::fromStdString("Algorithm Switch Success!"), mainWindow->getGUI());
            ui->lblCurrentAlgo->setText("Currently mining: " + ui->cmbAlgoSelect->itemText(currentMiningAlgo));
            // Restart the "blocks found this session" count for the new algorithm.
            nSessionBlocksBaseline = GetSessionBlocksFound();
        } else {
            openToastDialog(QString::fromStdString("Algorithm Switch Failed!"), mainWindow->getGUI());
        }

        setThreadSelectionValues(currentMiningAlgo);
    }
}

void MiningWidget::onToggleMiningActive() {

    // need to define "coinbaseScript"
    std::shared_ptr<CReserveScript> coinbase_script;
    walletModel->wallet().getWalletPointer()->GetScriptForMining(coinbase_script);

    //throw an error if no script was provided
    if (!coinbase_script || coinbase_script->reserveScript.empty()) {
       openToastDialog("No coinbase script available", mainWindow->getGUI());
       return;
    }

    int nAlgo = GetMiningAlgorithm();
    int nThreads = ui->numThreads->text().toInt();

    if (!mineOn) {
            
        if ((nAlgo == MINE_SHA256D) && (nThreads > maxThreads))
            openToastDialog(QString("SHA256D limited to %1 threads").arg(maxThreads), mainWindow->getGUI());

        if ((nAlgo == MINE_RANDOMX) && (nThreads < 4)) {
            openToastDialog(QString::fromStdString("RandomX must be at least 4 threads"), mainWindow->getGUI());
            //sWarning = "RandomX must be at least 4 threads";
            // Note this changes the nThreads input, for accuracy of the result
            // message, So this check needs to be below the threads check above
            nThreads = 4;

        }

        if (RECMAX < nThreads) { 
            QMessageBox threadWarningBox;
            threadWarningBox.setText("You have selected more threads than is recommended.");
            threadWarningBox.setInformativeText("Do you want to proceed?");
            threadWarningBox.setStandardButtons(QMessageBox::Yes| QMessageBox::Cancel);
            threadWarningBox.setIcon(QMessageBox::Warning);

            if (threadWarningBox.exec() != QMessageBox::Yes) 
                return;
        }

    }
        
    mineOn = !mineOn;
    GenerateBitcoins(mineOn, nThreads, coinbase_script);

    setMineActiveTxt(mineOn);
}

void MiningWidget::updateMiningFields() {
    mineOn = GenerateActive();
    currentMiningAlgo = GetMiningAlgorithm();

    setMineActiveTxt(mineOn);

    if (mineOn && IsBuildingMinerDataset()) {
        ui->lblMineActive->setText("Preparing mining dataset...");
        ui->lblMineActive->setStyleSheet("QLabel{color:#b8860b;}");
        ui->lblHashRate->setText("Please wait");
    } else {
        ui->lblHashRate->setText(mineOn ? QString("Mining at %1").arg(formatHashRate(GetRecentHashSpeed()))
                                        : QString("Not mining"));
    }

    updateMiningStats();

    // Keep the DAG checkbox in sync with the global the miner actually reads, so
    // a second wallet view does not misrepresent what mining will do. Block
    // signals so this programmatic update does not re-enter onToggleProgPowDag.
    const bool fFullDag = GetProgPowFullDataset();
    if (ui->chkProgPowDag->isChecked() != fFullDag) {
        ui->chkProgPowDag->blockSignals(true);
        ui->chkProgPowDag->setChecked(fFullDag);
        ui->chkProgPowDag->blockSignals(false);
    }

    setThreadSelectionValues(currentMiningAlgo);
}

void MiningWidget::updateMiningStats() {
    // Difficulty needs cs_main, so refresh these on a slower cadence and skip
    // the tick when the lock is busy rather than stalling the GUI thread.
    // nLastStatsUpdate is a member: with two wallets open each MiningWidget must
    // keep its own cadence, not share one static across every instance.
    const int64_t nNow = QDateTime::currentMSecsSinceEpoch();
    if (nNow - nLastStatsUpdate < 2000)
        return;

    int nBlockType = CBlockHeader::RANDOMX_BLOCK;
    int64_t nSpacing = Params().GetConsensus().nRandomXTargetSpacing;
    if (currentMiningAlgo == MINE_PROGPOW) {
        nBlockType = CBlockHeader::PROGPOW_BLOCK;
        nSpacing = Params().GetConsensus().nProgPowTargetSpacing;
    } else if (currentMiningAlgo == MINE_SHA256D) {
        nBlockType = CBlockHeader::SHA256D_BLOCK;
        nSpacing = Params().GetConsensus().nSha256DTargetSpacing;
    }

    double dDiff = 0.0;
    {
        TRY_LOCK(cs_main, lockMain);
        if (!lockMain || !chainActive.Tip())
            return;
        dDiff = GetDifficulty(GetNextWorkRequired(chainActive.Tip(), nullptr, Params().GetConsensus(),
                                                  false, nBlockType));
    }
    nLastStatsUpdate = nNow;

    // Standard difficulty to hashrate conversion: one unit of difficulty is
    // 2^32 expected hashes.
    const double dExpectedHashes = dDiff * 4294967296.0;

    ui->lblDifficulty->setText(formatDifficulty(dDiff));
    ui->lblNetworkHash->setText(nSpacing > 0 ? formatHashRate(dExpectedHashes / nSpacing) : QString("..."));

    const uint64_t nTotalFound = GetSessionBlocksFound();
    const uint64_t nFound = nTotalFound >= nSessionBlocksBaseline
                                ? nTotalFound - nSessionBlocksBaseline : 0;
    QString sFound = QString::number(nFound);
    if (nFound > 0 && GetSessionLastBlockTime() > 0)
        sFound += QString(" (last %1)").arg(QDateTime::fromTime_t((uint)GetSessionLastBlockTime()).toString("hh:mm"));
    ui->lblBlocksFound->setText(sFound);

    const double dMyRate = GetRecentHashSpeed();
    if (mineOn && dMyRate > 0)
        ui->lblTimeToBlock->setText(QString("~%1").arg(formatTimeSpan(dExpectedHashes / dMyRate)));
    else
        ui->lblTimeToBlock->setText("...");
}

QString MiningWidget::formatHashRate(double dRate) {
    static const char* units[] = {"H/s", "kH/s", "MH/s", "GH/s", "TH/s", "PH/s"};
    int i = 0;
    while (dRate >= 1000.0 && i < 5) {
        dRate /= 1000.0;
        ++i;
    }
    const int prec = dRate < 10 ? 2 : (dRate < 100 ? 1 : 0);
    return QString("%1 %2").arg(dRate, 0, 'f', prec).arg(units[i]);
}

QString MiningWidget::formatDifficulty(double dDiff) {
    static const char* units[] = {"", "k", "M", "G", "T"};
    int i = 0;
    while (dDiff >= 1000.0 && i < 4) {
        dDiff /= 1000.0;
        ++i;
    }
    const int prec = dDiff < 10 ? 3 : (dDiff < 100 ? 2 : 1);
    return QString("%1%2").arg(dDiff, 0, 'f', prec).arg(units[i]);
}

QString MiningWidget::formatTimeSpan(double dSeconds) {
    if (dSeconds < 90)
        return QString("%1 seconds").arg(qRound(dSeconds));
    const double dMinutes = dSeconds / 60.0;
    if (dMinutes < 90)
        return QString("%1 minutes").arg(qRound(dMinutes));
    const double dHours = dMinutes / 60.0;
    if (dHours < 36)
        return QString("%1 hours").arg(dHours, 0, 'f', 1);
    const double dDays = dHours / 24.0;
    if (dDays < 365)
        return QString("%1 days").arg(dDays, 0, 'f', 1);
    const double dYears = dDays / 365.25;
    return QString("%1 years").arg(dYears, 0, 'f', 1);
}

void MiningWidget::setThreadSelectionValues(int algo) {
    minThreads = (MINE_RANDOMX == algo) ? 4 : 1;

    // The offered maximum leaves one core free so the desktop stays responsive.
    // Keeping it equal to the recommended ceiling (RECMAX) means "Use Max
    // Threads" no longer lands one over and trips the warning. RandomX still
    // needs its floor of 4. This also replaces the old INT_MAX "no limit", which
    // let the button jam billions of threads into the spinbox.
    maxThreads = (RECMAX > minThreads) ? RECMAX : minThreads;

    ui->lblMaxThreadsAvailable->setText(QString::number(maxThreads));
    ui->numThreads->setRange(minThreads, maxThreads);
    ui->lblCurrentAlgo->setText("Currently mining: " + ui->cmbAlgoSelect->itemText(algo));
    ui->chkProgPowDag->setVisible(MINE_PROGPOW == algo);

    onChangeNumberOfThreads(ui->numThreads->text().toInt());
}

void MiningWidget::onToggleProgPowDag(bool fChecked) {
    SetProgPowFullDataset(fChecked);
    if (fChecked && mineOn && currentMiningAlgo == MINE_PROGPOW)
        openToastDialog("Building the DAG now, hashing pauses until it is ready", mainWindow->getGUI());
    else if (!fChecked && mineOn && currentMiningAlgo == MINE_PROGPOW)
        openToastDialog("Switching to light mining on the next round", mainWindow->getGUI());
}

void MiningWidget::onUseMaxThreads() {
    ui->numThreads->setValue(maxThreads);
}

void MiningWidget::onChangeNumberOfThreads(int newNumThr) {
    if (RECMAX < ui->numThreads->value()) {
        if ((MINE_RANDOMX == currentMiningAlgo) && ((RECMAX + 1) == minThreads) && (RECMAX + 1 == ui->numThreads->value())) {
            // don't show the exceeding warning
            ui->frame_8->setVisible(false);
        } else {
            ui->frame_8->setVisible(true);
        }
    } else {
        ui->frame_8->setVisible(false);
    }
}
