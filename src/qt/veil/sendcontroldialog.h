// Copyright (c) 2019 The Veil developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef SENDCONTROLDIALOG_H
#define SENDCONTROLDIALOG_H

#include <QWidget>
#include <QDialog>

namespace Ui {
class SendControlDialog;
}

class SendControlDialog : public QDialog
{
    Q_OBJECT

public:
    explicit SendControlDialog(QWidget *parent = nullptr);
    ~SendControlDialog();
private slots:
    void onEscapeClicked();
private:
    Ui::SendControlDialog *ui;
};

#endif // SENDCONTROLDIALOG_H
