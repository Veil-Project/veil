// Copyright (c) 2019 The Veil developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef SENDCONFIRMATION_H
#define SENDCONFIRMATION_H

#include <QWidget>
#include <QDialog>

namespace Ui {
class SendConfirmation;
}

class SendConfirmation : public QDialog
{
    Q_OBJECT

public:
    explicit SendConfirmation(QWidget *parent = nullptr, const QString &addresses = "", const QString &amount = "", const QString &fee = "");
    ~SendConfirmation();

    bool getRes(){
        return res;
    }

private Q_SLOTS:
    void onSendClicked();
private:
    Ui::SendConfirmation *ui;
    bool res = false;
};

#endif // SENDCONFIRMATION_H
