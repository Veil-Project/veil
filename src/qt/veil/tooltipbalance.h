#ifndef TOOLTIPBALANCE_H
#define TOOLTIPBALANCE_H

#include <QWidget>
#include <QString>

namespace Ui {
class TooltipBalance;
}

class TooltipBalance : public QWidget
{
    Q_OBJECT

public:
    explicit TooltipBalance(QWidget *parent = nullptr, int _unit = 0, int64_t nZerocoinbalance = 0, int64_t nRingBalance = 0,
            int64_t basecoinBalance = 0);
    ~TooltipBalance();

    virtual void showEvent(QShowEvent *event) override;
    virtual void hideEvent(QHideEvent *event) override;

    void update(
            QString firstTitle, int64_t firstBalance,
            QString secondTitle, int64_t secondBalance,
            QString thirdTitle, int64_t thirdBalance
    );

private:
    Ui::TooltipBalance *ui;
    int unit = 0;
};

#endif // TOOLTIPBALANCE_H
