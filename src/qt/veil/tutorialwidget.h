#ifndef TUTORIALWIDGET_H
#define TUTORIALWIDGET_H

#include <qt/veil/createpassword.h>
#include <qt/veil/tutorialcreatewalletwidget.h>
#include <qt/veil/tutoriallanguageswidget.h>
#include <qt/veil/tutorialmnemoniccode.h>
#include <qt/veil/tutorialmnemonicrevealed.h>
#include <veil/mnemonic/walletinitflags.h>

#include <QWidget>
#include <QDialog>
#include <uint256.h>

class WalletModel;

namespace Ui {
class TutorialWidget;
}

class TutorialWidget : public QDialog
{
    Q_OBJECT

public:
    explicit TutorialWidget(QWidget *parent = nullptr);
    ~TutorialWidget();

    bool ShutdownRequested() const { return shutdown; }
    MnemonicWalletInitFlags GetSelection() const { return selection; }
    void SetLanguageSelection(QString strLanguage) { this->strLanguageSelection = strLanguage; }
    std::string GetLanguageSelection() const;
    uint512 GetSeed() const { return seed; }
    std::string GetMnemonic() const {return mnemonic; }

private Q_SLOTS:
    void on_next_triggered();

    void on_back_triggered();
private:
    Ui::TutorialWidget *ui;
    QWidget* parent;
    int position = 0;

    QStringList wordList;

    bool shutdown = true;
    MnemonicWalletInitFlags selection;

    TutorialMnemonicCode *tutorialMnemonicCode;
    TutorialMnemonicRevealed *tutorialMnemonicRevealed;
    CreatePassword *createPassword;
    TutorialLanguagesWidget *tutorialLanguageWidget;
    TutorialCreateWalletWidget *tutorialCreateWallet;

    QString strLanguageSelection;
    uint512 seed;
    std::string mnemonic;

    void loadLeftContainer(QString imgPath, QString topMessage,  QString bottomMessage);
};

#endif // TUTORIALWIDGET_H
