#include <qt/veil/tooltipbalance.h>
#include <qt/veil/forms/ui_tooltipbalance.h>

#include <QGraphicsOpacityEffect>
#include <QPropertyAnimation>
#include <QPropertyAnimation>
#include <QTimer>
#include <qt/bitcoinunits.h>

TooltipBalance::TooltipBalance(QWidget *parent, int unit, int64_t nZerocoinBalance, int64_t nRingBalance, int64_t basecoinBalance) :
    QWidget(parent),
    ui(new Ui::TooltipBalance)
{
    ui->setupUi(this);
    ui->textZero->setText(BitcoinUnits::formatWithUnit(unit, nZerocoinBalance, false, BitcoinUnits::separatorAlways));
    ui->textRing->setText(BitcoinUnits::formatWithUnit(unit, nRingBalance, false, BitcoinUnits::separatorAlways));
    ui->textBasecoin->setText(BitcoinUnits::formatWithUnit(unit, basecoinBalance, false, BitcoinUnits::separatorAlways));

    QTimer::singleShot(3500, this, SLOT(hide()));

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
    //connect(a,SIGNAL(finished()),this,SLOT(hideThisWidget()));
}

TooltipBalance::~TooltipBalance()
{
    delete ui;
}
