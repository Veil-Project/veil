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
    explicit TooltipBalance(QWidget *parent = nullptr);
    ~TooltipBalance();

    virtual void showEvent(QShowEvent *event) override;
    virtual void hideEvent(QHideEvent *event) override;

private:
    Ui::TooltipBalance *ui;
};

#endif // TOOLTIPBALANCE_H
