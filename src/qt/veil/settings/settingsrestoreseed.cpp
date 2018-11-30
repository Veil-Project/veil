#include <qt/veil/settings/settingsrestoreseed.h>
#include <qt/veil/forms/ui_settingsrestoreseed.h>
#include <QLineEdit>
#include <QString>

SettingsRestoreSeed::SettingsRestoreSeed(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::SettingsRestoreSeed)
{
    ui->setupUi(this);

    for(int i=0; i<6; i++){
        for(int j=0; j<4; j++){
            QLineEdit* editWord = new QLineEdit(this);
            editWord->setAttribute(Qt::WA_MacShowFocusRect, 0);
            editWord->setAlignment(Qt::AlignHCenter);
            editWord->setProperty("cssClass" , "edit-seed");
            //editWord->setStyleSheet("QLineEdit{border:0px;border-bottom:1px solid #707070;background-color:#fff;padding-left:8px;padding-right:8px;padding-top:7px;padding-bottom:7px;margin:8px;}");
            ui->gridLayout->addWidget(editWord, i, j);
        }
    }

}

SettingsRestoreSeed::~SettingsRestoreSeed()
{
    delete ui;
}
