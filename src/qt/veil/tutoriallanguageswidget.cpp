#include <qt/veil/tutoriallanguageswidget.h>
#include <qt/veil/forms/ui_tutoriallanguageswidget.h>

TutorialLanguagesWidget::TutorialLanguagesWidget(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::TutorialLanguagesWidget)
{
    ui->setupUi(this);


    ui->groupBox->setFocusPolicy(Qt::NoFocus);
    ui->pushButton->setFocusPolicy(Qt::NoFocus);
    ui->pushButton1->setFocusPolicy(Qt::NoFocus);
    ui->pushButton2->setFocusPolicy(Qt::NoFocus);
    ui->pushButton3->setFocusPolicy(Qt::NoFocus);
    ui->pushButton4->setFocusPolicy(Qt::NoFocus);

}

TutorialLanguagesWidget::~TutorialLanguagesWidget()
{
    delete ui;
}
