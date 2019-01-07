#include <qt/veil/tutorialcreatewalletwidget.h>
#include <qt/veil/forms/ui_tutorialcreatewalletwidget.h>

TutorialCreateWalletWidget::TutorialCreateWalletWidget(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::TutorialCreateWalletWidget)
{
    ui->setupUi(this);

    ui->groupBox->setFocusPolicy(Qt::NoFocus);
    ui->btnCreate->setFocusPolicy(Qt::NoFocus);
    ui->btnRestore->setFocusPolicy(Qt::NoFocus);
}

TutorialCreateWalletWidget::~TutorialCreateWalletWidget()
{
    delete ui;
}

int TutorialCreateWalletWidget::GetButtonClicked()
{
    if (ui->btnCreate->isChecked())
        return 1;
    else if (ui->btnRestore->isChecked())
        return 2;
    else
        return 0;
}
