// Copyright (c) 2021 The Veil developers
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

    connect(ui->btnUpdateAlgo, SIGNAL(clicked()), this, SLOT(onUpdateAlgorithm()));
    connect(ui->btnAllThreads, SIGNAL(clicked()), this, SLOT(onUseMaxThreads()));
    connect(ui->btnActiveMine, SIGNAL(clicked()), this, SLOT(onToggleMiningActive()));

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
            ui->lblCurrentAlgo->setText(QString::fromStdString(algoStr));
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
            openToastDialog(QString::fromStdString("SHA256D limited to " + maxThreads), mainWindow->getGUI());

        if ((nAlgo == MINE_RANDOMX) && (nThreads < 4)) {
            openToastDialog(QString::fromStdString("RandomX must be at least 4 threads"), mainWindow->getGUI());
            //sWarning = "RandomX must be at least 4 threads";
            // Note this changes the nThreads input, for accuracy of the result
            // message, So this check needs to be below the threads check above
            nThreads = 4;
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

    ui->lblHashRate->setText(QString("Mining at %1 H/s").arg(QString::number(GetHashSpeed())));

    setThreadSelectionValues(currentMiningAlgo);
}

void MiningWidget::setThreadSelectionValues(int algo) {
    int minThreads = 1;
    if (MINE_RANDOMX == algo)
        minThreads = 4;
    if (MINE_SHA256D == algo) {
       maxThreads = (GetNumCores () - 1);
    } else { 
       maxThreads = GetNumCores();
    }

    ui->lblMaxThreadsAvailable->setText(QString::number(maxThreads));
    ui->numThreads->setRange(minThreads, maxThreads);
    ui->lblCurrentAlgo->setText(QString::fromStdString(GetMiningType(algo, false, false)));
}

void MiningWidget::onUseMaxThreads() {
    ui->numThreads->setValue(maxThreads);
}
