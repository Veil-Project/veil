#include <qt/veil/tutorialwidget.h>
#include <qt/veil/forms/ui_tutorialwidget.h>
#include <qt/guiutil.h>
#include <QDebug>
#include <QFileDialog>
#include <iostream>
#include <wallet/wallet.h>
#include <wallet/walletutil.h>
#include <veil/mnemonic/mnemonic.h>
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
                            mnemonic = "";
                            std::string strWalletFile = "wallet.dat";
                            CWallet::CreateNewHDWallet(strWalletFile, GetWalletDir(), mnemonic, this->strLanguageSelection.toStdString(), &seed);

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

                    bool fBadSeed = false;
                    uint512 seed_local;
                    CWallet::CreateHDWalletFromMnemonic(strWalletFile, GetWalletDir(), mnemonic, fBadSeed, seed_local);
                    if (fBadSeed) {
                        //tutorialMnemonicRevealed = new TutorialMnemonicRevealed(wordList,this);
                        //ui->QStackTutorialContainer->addWidget(tutorialMnemonicRevealed);
                        qWidget = tutorialMnemonicRevealed;
                        loadLeftContainer(":/icons/img-start-confirm","Bad seed phrase \n Try again","");
                        ui->QStackTutorialContainer->setCurrentWidget(qWidget);
                        return;
                    }

                    createPassword = new CreatePassword(this);
                    ui->QStackTutorialContainer->addWidget(createPassword);
                    qWidget = createPassword;
                    loadLeftContainer(":/icons/img-start-password","Encrypt your wallet","");
                } else {
                    //tutorialMnemonicRevealed = new TutorialMnemonicRevealed(wordList, this);
                    //ui->QStackTutorialContainer->addWidget(tutorialMnemonicRevealed);
                    qWidget = tutorialMnemonicRevealed;
                    loadLeftContainer(":/icons/img-start-confirm","Confirm your \n seed phrase","");
                }
                break;
            }
        case 3:
            {
                if (createPassword && tutorialCreateWallet->GetButtonClicked() == 2) {
                    shutdown = false;
                    accept();
                    return;
                } else {
                    createPassword = new CreatePassword(this);
                    ui->QStackTutorialContainer->addWidget(createPassword);
                    qWidget = createPassword;
                    loadLeftContainer(":/icons/img-start-password","Encrypt your wallet","");
                }
                break;
            }
        case 4:
            // Check createPassword if exists to encrypt the wallet
            if(createPassword){
                if(createPassword->isValid()){
                    // TODO: Encrypt wallet here.. Check if it's better to remove this for now or not..
//                    if(walletModel->setWalletEncrypted(true, newpass1)) {
//                        QMessageBox::warning(this, tr("Wallet encrypted"),
//                                             "<qt>" +
//                                             tr("%1 will close now to finish the encryption process. "
//                                                "Remember that encrypting your wallet cannot fully protect "
//                                                "your veil from being stolen by malware infecting your computer.").arg(tr(PACKAGE_NAME)) +
//                                             "<br><br><b>" +
//                                             tr("IMPORTANT: Any previous backups you have made of your wallet file "
//                                                "should be replaced with the newly generated, encrypted wallet file. "
//                                                "For security reasons, previous backups of the unencrypted wallet file "
//                                                "will become useless as soon as you start using the new, encrypted wallet.") +
//                                             "</b></qt>");
//                        QApplication::quit();
//                    }
//                    else {
//                        QMessageBox::critical(this, tr("Wallet encryption failed"),
//                                              tr("Wallet encryption failed due to an internal error. Your wallet was not encrypted."));
//                    }
                }else {
                    openToastDialog(tr("Invalid password"), this);
                    break;
                }
            }

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
