#include <qt/veil/tutoriallanguageswidget.h>
#include <qt/veil/forms/ui_tutoriallanguageswidget.h>
#include <string>
#include "tutorialwidget.h"

TutorialLanguagesWidget::TutorialLanguagesWidget(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::TutorialLanguagesWidget)
{
    ui->setupUi(this);

    this->parent = parent;
    connect(ui->pushButton_en, SIGNAL(clicked(QAbstractButton*)), this, SLOT(buttonClicked(QString("english"))));
    connect(ui->pushButton_fr, SIGNAL(clicked(QAbstractButton*)), this, SLOT(buttonClicked(QString("french"))));
    connect(ui->pushButton_port, SIGNAL(clicked(QAbstractButton*)), this, SLOT(buttonClicked(QString("english"))));
    connect(ui->pushButton_es, SIGNAL(clicked(QAbstractButton*)), this, SLOT(buttonClicked(QString("spanish"))));
    connect(ui->pushButton_fr, SIGNAL(clicked(QAbstractButton*)), this, SLOT(buttonClicked(QString("french"))));

    ui->groupBox->setFocusPolicy(Qt::NoFocus);
    ui->pushButton_en->setFocusPolicy(Qt::NoFocus);
    ui->pushButton_fr->setFocusPolicy(Qt::NoFocus);
    ui->pushButton_port->setFocusPolicy(Qt::NoFocus);
    ui->pushButton_es->setFocusPolicy(Qt::NoFocus);
    ui->pushButton_fr->setFocusPolicy(Qt::NoFocus);

}

std::string TutorialLanguagesWidget::GetLanguageSelection()
{
    return strLanguageSelection.toStdString();
}

void TutorialLanguagesWidget::buttonClicked(QString str)
{
    auto tut = (TutorialWidget*)this->parent;
    tut->SetLanguageSelection(str);
}

TutorialLanguagesWidget::~TutorialLanguagesWidget()
{
    delete ui;
}
