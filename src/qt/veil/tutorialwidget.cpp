#include <qt/veil/tutorialwidget.h>
#include <qt/veil/forms/ui_tutorialwidget.h>
#include <qt/guiutil.h>
#include <QDebug>
#include <iostream>
#include <wallet/wallet.h>

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

    tutorialLanguageWidget = new TutorialLanguagesWidget(this);

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
                word_list listWords;
                mnemonic = "";
                std::string strWalletFile = "wallet.dat";
                CWallet::CreateNewHDWallet(strWalletFile, GetDataDir(), mnemonic, this->strLanguageSelection.toStdString(), &pkSeed);

                std::stringstream ss(mnemonic);
                std::istream_iterator<std::string> begin(ss);
                std::istream_iterator<std::string> end;
                mnemonicWordList.clear();
                mnemonicWordList = std::vector<std::string>(begin, end);
                std::vector<unsigned char> keyData = key_from_mnemonic(mnemonicWordList);

                tutorialMnemonicCode = new TutorialMnemonicCode(mnemonicWordList, this);
                ui->QStackTutorialContainer->addWidget(tutorialMnemonicCode);
                qWidget = tutorialMnemonicCode;
                loadLeftContainer(":/icons/img-start-backup","Backup your \n recovery seed phrase","Your 24-word seed phrase \n can  be used to restore your wallet.");
                break;
            }
        case 2:
            {
                tutorialMnemonicRevealed = new TutorialMnemonicRevealed(this);
                ui->QStackTutorialContainer->addWidget(tutorialMnemonicRevealed);
                qWidget = tutorialMnemonicRevealed;
                loadLeftContainer(":/icons/img-start-confirm","Confirm your \n seed phrase","");
                break;
            }
        case 3:
            {
                createPassword = new CreatePassword(this);
                ui->QStackTutorialContainer->addWidget(createPassword);
                qWidget = createPassword;
                loadLeftContainer(":/icons/img-start-password","Encrypt your wallet","");
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
                    qWidget = tutorialMnemonicCode;               
                    loadLeftContainer(":/icons/img-start-confirm","Confirm your \n seed phrase","");
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
