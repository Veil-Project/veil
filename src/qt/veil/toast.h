#ifndef TOAST_H
#define TOAST_H

#include <QWidget>

namespace Ui {
class Toast;
}

class Toast : public QWidget
{
    Q_OBJECT

public:
    explicit Toast(QWidget *parent = nullptr, QString text = "Unknown error");
    ~Toast();

    virtual void showEvent(QShowEvent *event) override;
    virtual void hideEvent(QHideEvent *event) override;

private:
    Ui::Toast *ui;
};

#endif // TOAST_H
