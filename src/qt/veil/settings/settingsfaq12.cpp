// Copyright (c) 2019 The Veil developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qt/veil/settings/settingsfaq12.h>
#include <qt/veil/forms/ui_settingsfaq12.h>

SettingsFaq12::SettingsFaq12(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::SettingsFaq12)
{
    ui->setupUi(this);

    ui->label_2->setTextFormat( Qt::RichText );
    ui->label_2->setWordWrap(true);
}

SettingsFaq12::~SettingsFaq12()
{
    delete ui;
}
