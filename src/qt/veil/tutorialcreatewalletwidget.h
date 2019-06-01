// Copyright (c) 2019 The Veil developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef TUTORIALCREATEWALLETWIDGET_H
#define TUTORIALCREATEWALLETWIDGET_H

#include <QWidget>

namespace Ui {
class TutorialCreateWalletWidget;
}

class TutorialCreateWalletWidget : public QWidget
{
    Q_OBJECT

public:
    explicit TutorialCreateWalletWidget(QWidget *parent = nullptr);
    ~TutorialCreateWalletWidget();

    int GetButtonClicked();

private:
    Ui::TutorialCreateWalletWidget *ui;
};

#endif // TUTORIALCREATEWALLETWIDGET_H
