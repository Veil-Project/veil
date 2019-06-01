// Copyright (c) 2019 The Veil developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef CREATEPASSWORD_H
#define CREATEPASSWORD_H

#include <QWidget>
#include <QString>

namespace Ui {
class CreatePassword;
}

class CreatePassword : public QWidget
{
    Q_OBJECT

public:
    explicit CreatePassword(QWidget *parent = nullptr);
    ~CreatePassword();

    QString getPassword();
    QString getPasswordRepeat();
    bool isValid();

private:
    Ui::CreatePassword *ui;
};

#endif // CREATEPASSWORD_H
