#ifndef TUTORIALCREATEWALLETWIDGET_H
#define TUTORIALCREATEWALLETWIDGET_H

#include <QWidget>

namespace Ui {
class TutorialCreateWalletWidget;
}

class TutorialCreateWalletWidget : public QWidget
{
    Q_OBJECT

public:
    explicit TutorialCreateWalletWidget(QWidget *parent = nullptr);
    ~TutorialCreateWalletWidget();

    int GetButtonClicked();

private:
    Ui::TutorialCreateWalletWidget *ui;
};

#endif // TUTORIALCREATEWALLETWIDGET_H
