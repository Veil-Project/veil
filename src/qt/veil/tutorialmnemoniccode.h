#ifndef TUTORIALMNEMONICCODE_H
#define TUTORIALMNEMONICCODE_H

#include <QLabel>
#include <QWidget>
#include <veil/mnemonic/mnemonic.h>

namespace Ui {
class TutorialMnemonicCode;
}

class TutorialMnemonicCode : public QWidget
{
    Q_OBJECT

public:
    explicit TutorialMnemonicCode(std::vector<std::string> vWords, QWidget *parent = nullptr);
    ~TutorialMnemonicCode();

private Q_SLOTS:
    void onRevealClicked();
private:
    Ui::TutorialMnemonicCode *ui;

    std::list<QLabel*> labelsList;
    std::vector<std::string> vWords;
};

#endif // TUTORIALMNEMONICCODE_H
