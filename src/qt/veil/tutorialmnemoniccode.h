// Copyright (c) 2019 The Veil developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

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
