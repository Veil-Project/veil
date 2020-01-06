// Copyright (c) 2019 The Veil developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qt/veil/tooltipbalance.h>
#include <qt/veil/forms/ui_tooltipbalance.h>

#include <QGraphicsOpacityEffect>
#include <QPropertyAnimation>
#include <QPropertyAnimation>
#include <QTimer>
#include <qt/bitcoinunits.h>

TooltipBalance::TooltipBalance(QWidget *parent, int _unit, int64_t nZerocoinBalance, int64_t nRingBalance, int64_t basecoinBalance) :
    QWidget(parent),
    ui(new Ui::TooltipBalance),
    unit(_unit)
{
    ui->setupUi(this);
    ui->textZero->setText(BitcoinUnits::formatWithUnit(unit, nZerocoinBalance, false, BitcoinUnits::separatorAlways));
    ui->textRing->setText(BitcoinUnits::formatWithUnit(unit, nRingBalance, false, BitcoinUnits::separatorAlways));
    ui->textBasecoin->setText(BitcoinUnits::formatWithUnit(unit, basecoinBalance, false, BitcoinUnits::separatorAlways));
}

void TooltipBalance::update(
        QString firstTitle, int64_t firstBalance,
        QString secondTitle, int64_t secondBalance,
        QString thirdTitle, int64_t thirdBalance){

    if(!firstTitle.isEmpty()){
        ui->lblSecond->setVisible(true);
        ui->textRing->setVisible(true);
        ui->lblFirst->setText(firstTitle);
        ui->textZero->setText(BitcoinUnits::formatWithUnit(unit, firstBalance, false, BitcoinUnits::separatorAlways));
    }

    if(!secondTitle.isEmpty()){
        ui->lblSecond->setVisible(true);
        ui->textRing->setVisible(true);
        ui->lblSecond->setText(secondTitle);
        ui->textRing->setText(BitcoinUnits::formatWithUnit(unit, secondBalance, false, BitcoinUnits::separatorAlways));
    }else{
        ui->lblSecond->setVisible(false);
        ui->textRing->setVisible(false);
    }

    if(!thirdTitle.isEmpty()){
        ui->lblSecond->setVisible(true);
        ui->textRing->setVisible(true);
        ui->lblThird->setText(thirdTitle);
        ui->textBasecoin->setText(BitcoinUnits::formatWithUnit(unit, thirdBalance, false, BitcoinUnits::separatorAlways));
    }else{
        ui->lblSecond->setVisible(false);
        ui->textRing->setVisible(false);
    }

}

void TooltipBalance::showEvent(QShowEvent *event){
    QGraphicsOpacityEffect *eff = new QGraphicsOpacityEffect(this);
    this->setGraphicsEffect(eff);
    QPropertyAnimation *a = new QPropertyAnimation(eff,"opacity");
    a->setDuration(400);
    a->setStartValue(0.1);
    a->setEndValue(1);
    a->setEasingCurve(QEasingCurve::InBack);
    a->start(QPropertyAnimation::DeleteWhenStopped);
}

void TooltipBalance::hideEvent(QHideEvent *event){
    QGraphicsOpacityEffect *eff = new QGraphicsOpacityEffect(this);
    this->setGraphicsEffect(eff);
    QPropertyAnimation *a = new QPropertyAnimation(eff,"opacity");
    a->setDuration(800);
    a->setStartValue(1);
    a->setEndValue(0);
    a->setEasingCurve(QEasingCurve::OutBack);
    a->start(QPropertyAnimation::DeleteWhenStopped);
}

TooltipBalance::~TooltipBalance()
{
    delete ui;
}
