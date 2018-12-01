#ifndef TOOLTIPBALANCE_H
#define TOOLTIPBALANCE_H

#include <QWidget>

namespace Ui {
class TooltipBalance;
}

class TooltipBalance : public QWidget
{
    Q_OBJECT

public:
    explicit TooltipBalance(QWidget *parent = nullptr, int nZerocoinbalance = 0, int nRingBalance = 0);
    ~TooltipBalance();

    virtual void showEvent(QShowEvent *event) override;
    virtual void hideEvent(QHideEvent *event) override;

private:
    Ui::TooltipBalance *ui;
};

#endif // TOOLTIPBALANCE_H
