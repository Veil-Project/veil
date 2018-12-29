#include <qt/veil/tutorialwidget.h>
#include <qt/veil/forms/ui_tutorialwidget.h>
#include <qt/guiutil.h>
#include <QDebug>
#include <QFileDialog>
#include <iostream>
#include <wallet/wallet.h>
#include <wallet/walletutil.h>
#include <veil/mnemonic/mnemonic.h>
#include <veil/mnemonic/generateseed.h>
#include <qt/veil/qtutils.h>

TutorialWidget::TutorialWidget(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::TutorialWidget)
{
    ui->setupUi(this);
    this->parent = parent;

    this->setStyleSheet(GUIUtil::loadStyleSheet());

    this->setWindowTitle("Veil Wallet Setup");

    this->setContentsMargins(0,0,0,0);
    this->ui->containerLeft->setContentsMargins(0,0,0,0);
    ui->QStackTutorialContainer->setContentsMargins(0,0,0,10);

    // TODO: Complete this with the specific language words
    dictionary dic = string_to_lexicon("english");
    for(unsigned long i=0; i< dic.size() ; i++){
        wordList << dic[i];
    }
    tutorialMnemonicRevealed = new TutorialMnemonicRevealed(wordList, this);

    tutorialLanguageWidget = new TutorialLanguagesWidget(this);

    ui->QStackTutorialContainer->addWidget(tutorialMnemonicRevealed);
    ui->QStackTutorialContainer->addWidget(tutorialLanguageWidget);
    ui->QStackTutorialContainer->setCurrentWidget(tutorialLanguageWidget);

    loadLeftContainer(":/icons/img-start-logo", "Welcome to VEIL", "");
    ui->btnBack->setVisible(false);
    ui->btnLineSeparator->setVisible(false);

    ui->btnNext->setFocusPolicy(Qt::NoFocus);
    ui->btnBack->setFocusPolicy(Qt::NoFocus);

    connect(ui->btnNext, SIGNAL(clicked()), this, SLOT(on_next_triggered()));
    connect(ui->btnBack, SIGNAL(clicked()), this, SLOT(on_back_triggered()));

}

// left side
void TutorialWidget::loadLeftContainer(QString imgPath, QString topMessage,  QString bottomMessage){
    QPixmap icShield(120,120);
    icShield.load(imgPath);
    ui->imgTutorial->setPixmap(icShield);
    ui->messageTop->setText(topMessage);
    ui->messageBottom->setText(bottomMessage);
}


// Actions
std::vector<std::string> mnemonicWordList;
void TutorialWidget::on_next_triggered(){
    QWidget *qWidget = nullptr;
    switch (position) {
        case 0:
            {

                strLanguageSelection = QString::fromStdString(tutorialLanguageWidget->GetLanguageSelection());

                tutorialCreateWallet = new TutorialCreateWalletWidget(this);
                ui->QStackTutorialContainer->addWidget(tutorialCreateWallet);
                qWidget = tutorialCreateWallet;
                //img-start-wallet
                loadLeftContainer(":/icons/img-wallet","Setup your\nVEIL wallet","");
                ui->btnLineSeparator->setVisible(true);
                ui->btnBack->setVisible(true);
                break;
            }
        case 1:
            {
                if (tutorialCreateWallet && tutorialCreateWallet->GetButtonClicked()) {
                    switch (tutorialCreateWallet->GetButtonClicked()) {
                        case 1:
                        {
                            //Generate mnemonic phrase from fresh entropy
                            mnemonic = "";
                            veil::GenerateNewMnemonicSeed(mnemonic, strLanguageSelection.toStdString());

                            std::stringstream ss(mnemonic);
                            std::istream_iterator<std::string> begin(ss);
                            std::istream_iterator<std::string> end;
                            mnemonicWordList.clear();
                            mnemonicWordList = std::vector<std::string>(begin, end);

                            tutorialMnemonicCode = new TutorialMnemonicCode(mnemonicWordList, this);
                            ui->QStackTutorialContainer->addWidget(tutorialMnemonicCode);
                            qWidget = tutorialMnemonicCode;
                            loadLeftContainer(":/icons/img-start-backup","Backup your \n recovery seed phrase","Your 24-word seed phrase \n can  be used to restore your wallet.");
                            break;
                        }
                        case 2:
                        {
                            qWidget = tutorialMnemonicRevealed;
                            loadLeftContainer(":/icons/img-start-confirm","Enter your \n seed phrase","");
                            break;
                        }
                        case 3:
                        {
                            //TODO: Import wallet from file. This code probably doesn't work.
                            QFile walletFile(QFileDialog::getOpenFileName(0, "Choose wallet file"));
                            if(!walletFile.exists())
                                return;
                            fs::path walletPath = walletFile.fileName().toStdString();
                            CWallet::CreateWalletFromFile(walletFile.fileName().toStdString(), walletPath);
                            shutdown = false;
                            accept();
                            return;
                        }
                    }
                }

                break;
            }
        case 2:
            {
                if (tutorialMnemonicRevealed && tutorialCreateWallet->GetButtonClicked() == 2) {
                    mnemonic = "";
                    std::string strWalletFile = "wallet.dat";
                    std::list<QString> q_word_list = tutorialMnemonicRevealed->getOrderedStrings();
                    for (QString &q_word : q_word_list) {
                        if (mnemonic.empty())
                            mnemonic = q_word.toStdString();
                        else
                            mnemonic += " " + q_word.toStdString();
                    }

                    shutdown = false;
                    accept();
                } else {
                    qWidget = tutorialMnemonicRevealed;
                    loadLeftContainer(":/icons/img-start-confirm","Confirm your \n seed phrase","");
                }
                break;
            }
        case 3:
            {
                if (tutorialCreateWallet->GetButtonClicked() == 2) {
                    shutdown = false;
                    accept();
                    return;
                } else {
                    // Check mnemonic first
                    std::list<QString> q_word_list = tutorialMnemonicRevealed->getOrderedStrings();
                    std::string mnemonicTest;
                    for (QString &q_word : q_word_list) {
                        if (mnemonicTest.empty())
                            mnemonicTest = q_word.toStdString();
                        else
                            mnemonicTest += " " + q_word.toStdString();
                    }

                    if(mnemonicTest != mnemonic){
                        openToastDialog(
                                QString::fromStdString("Invalid mnemonic, please write the words on the same order as before"),
                                this
                        );
                        return;
                    }

                    shutdown = false;
                    accept();
                }
                break;
            }
        case 4:
            shutdown = false;
            accept();
            break;

    }
    if(qWidget){
        position++;
        if(ui->QStackTutorialContainer->currentWidget() != qWidget) {
            ui->QStackTutorialContainer->setCurrentWidget(qWidget);
        }
    }
}

void TutorialWidget::on_back_triggered(){
    if(position != 0){
        QWidget *qWidget = nullptr;
        position--;
        switch (position) {
            case 0:
                {
                    qWidget = tutorialLanguageWidget;
                    loadLeftContainer(":/icons/img-start-logo", "Welcome to VEIL", "");
                    ui->btnBack->setVisible(false);
                    ui->btnLineSeparator->setVisible(false);
                    break;
                }
            case 1:
                {
                    qWidget = tutorialCreateWallet;
                    loadLeftContainer(":/icons/img-start-wallet","Setup your\nVEIL wallet","");
                    ui->btnLineSeparator->setVisible(true);
                    ui->btnBack->setVisible(true);
                    break;
                }
            case 2:
                {
                    if (tutorialMnemonicRevealed && tutorialCreateWallet->GetButtonClicked() == 2) {
                        qWidget = tutorialMnemonicRevealed;
                        loadLeftContainer(":/icons/img-start-confirm","Enter your \n seed phrase","");
                    } else {
                        qWidget = tutorialMnemonicCode;
                        loadLeftContainer(":/icons/img-start-confirm","Confirm your \n seed phrase","");
                    }
                    break;
                }
            case 3:
                {
                    qWidget = tutorialMnemonicRevealed;
                    loadLeftContainer(":/icons/img-start-password","Encrypt your wallet","");
                    break;
                }

        }
        if(qWidget != nullptr){
            if(ui->QStackTutorialContainer->currentWidget() != qWidget) {
                ui->QStackTutorialContainer->setCurrentWidget(qWidget);
            }
        }
    }
}

std::string TutorialWidget::GetLanguageSelection() const
{
    return strLanguageSelection.toStdString();
}

TutorialWidget::~TutorialWidget()
{
    delete ui;
}
