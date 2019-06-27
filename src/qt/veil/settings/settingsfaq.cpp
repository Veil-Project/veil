// Copyright (c) 2019 The Veil developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qt/veil/settings/settingsfaq.h>
#include <qt/veil/forms/ui_settingsfaq.h>
#include <qt/veil/settings/settingsfaq01.h>
#include <qt/veil/settings/settingsfaq02.h>
#include <qt/veil/settings/settingsfaq03.h>
#include <qt/veil/settings/settingsfaq04.h>
#include <qt/veil/settings/settingsfaq05.h>
#include <qt/veil/settings/settingsfaq06.h>
#include <qt/veil/settings/settingsfaq07.h>
#include <qt/veil/settings/settingsfaq08.h>
#include <qt/veil/settings/settingsfaq09.h>
#include <qt/veil/settings/settingsfaq10.h>
#include <qt/veil/settings/settingsfaq11.h>
#include <qt/veil/settings/settingsfaq12.h>
#include <qt/guiutil.h>


SettingsFaq::SettingsFaq(QWidget *parent, bool howToObtainVeil) :
    QDialog(parent),
    ui(new Ui::SettingsFaq)
{
    ui->setupUi(this);
    connect(ui->btnEsc,SIGNAL(clicked()),this, SLOT(onEscapeClicked()));

    setStyleSheet(GUIUtil::loadStyleSheet());

    //ui->editSearch->setAttribute(Qt::WA_MacShowFocusRect, 0);
    ui->radioButton_01->setProperty("cssClass" , "radio-button-faq");
    //ui->radioButton_02->setProperty("cssClass" , "radio-button-faq");
    ui->radioButton_03->setProperty("cssClass" , "radio-button-faq");
    ui->radioButton_04->setProperty("cssClass" , "radio-button-faq");
    ui->radioButton_05->setProperty("cssClass" , "radio-button-faq");
    ui->radioButton_06->setProperty("cssClass" , "radio-button-faq");
    ui->radioButton_07->setProperty("cssClass" , "radio-button-faq");
    ui->radioButton_08->setProperty("cssClass" , "radio-button-faq");
    ui->radioButton_09->setProperty("cssClass" , "radio-button-faq");
    ui->radioButton_10->setProperty("cssClass" , "radio-button-faq");
    //ui->radioButton_11->setProperty("cssClass" , "radio-button-faq");
    ui->radioButton_12->setProperty("cssClass" , "radio-button-faq");

    connect(ui->radioButton_01,SIGNAL(clicked()),this, SLOT(onRadioButton01Clicked()));
    //connect(ui->radioButton_02,SIGNAL(clicked()),this, SLOT(onRadioButton02Clicked()));
    connect(ui->radioButton_03,SIGNAL(clicked()),this, SLOT(onRadioButton03Clicked()));
    connect(ui->radioButton_04,SIGNAL(clicked()),this, SLOT(onRadioButton04Clicked()));
    connect(ui->radioButton_05,SIGNAL(clicked()),this, SLOT(onRadioButton05Clicked()));
    connect(ui->radioButton_06,SIGNAL(clicked()),this, SLOT(onRadioButton06Clicked()));
    connect(ui->radioButton_07,SIGNAL(clicked()),this, SLOT(onRadioButton07Clicked()));
    connect(ui->radioButton_08,SIGNAL(clicked()),this, SLOT(onRadioButton08Clicked()));
    connect(ui->radioButton_09,SIGNAL(clicked()),this, SLOT(onRadioButton09Clicked()));
    connect(ui->radioButton_10,SIGNAL(clicked()),this, SLOT(onRadioButton10Clicked()));
    //connect(ui->radioButton_11,SIGNAL(clicked()),this, SLOT(onRadioButton11Clicked()));
    connect(ui->radioButton_12,SIGNAL(clicked()),this, SLOT(onRadioButton12Clicked()));



    faq01 = new SettingsFaq01(this);

    ui->stackedWidget->addWidget(faq01);
    ui->stackedWidget->setCurrentWidget(faq01);

    if(howToObtainVeil){
        faq07 = new SettingsFaq07(this);
        ui->stackedWidget->addWidget(faq07);
        ui->stackedWidget->setCurrentWidget(faq07);
        ui->radioButton_07->setChecked(true);
    }
}

void SettingsFaq::changeScreen(QWidget *widget){
    ui->stackedWidget->setCurrentWidget(widget);
}

void SettingsFaq::onEscapeClicked(){
    close();
}


void SettingsFaq::onRadioButton01Clicked(){
    if(!faq01) {
        faq01 = new SettingsFaq01(this);
        ui->stackedWidget->addWidget(faq01);
    }

    changeScreen(faq01);
}

void SettingsFaq::onRadioButton02Clicked(){
    if(!faq02) {
        faq02 = new SettingsFaq02(this);
        ui->stackedWidget->addWidget(faq02);
    }

    changeScreen(faq02);
}



void SettingsFaq::onRadioButton03Clicked(){
    if(!faq03) {
        faq03 = new SettingsFaq03(this);
        ui->stackedWidget->addWidget(faq03);
    }

    changeScreen(faq03);
}

void SettingsFaq::onRadioButton04Clicked(){
    if(!faq04) {
        faq04 = new SettingsFaq04(this);
        ui->stackedWidget->addWidget(faq04);
    }

    changeScreen(faq04);
}

void SettingsFaq::onRadioButton05Clicked(){
    if(!faq05) {
        faq05 = new SettingsFaq05(this);
        ui->stackedWidget->addWidget(faq05);
    }

    changeScreen(faq05);
}

void SettingsFaq::onRadioButton06Clicked(){
    if(!faq06) {
        faq06 = new SettingsFaq06(this);
        ui->stackedWidget->addWidget(faq06);
    }

    changeScreen(faq06);
}

void SettingsFaq::onRadioButton07Clicked(){
    if(!faq07) {
        faq07 = new SettingsFaq07(this);
        ui->stackedWidget->addWidget(faq07);
    }

    changeScreen(faq07);
}

void SettingsFaq::onRadioButton08Clicked(){
    if(!faq08) {
        faq08 = new SettingsFaq08(this);
        ui->stackedWidget->addWidget(faq08);
    }

    changeScreen(faq08);
}

void SettingsFaq::onRadioButton09Clicked(){
    if(!faq09) {
        faq09 = new SettingsFaq09(this);
        ui->stackedWidget->addWidget(faq09);
    }

    changeScreen(faq09);
}

void SettingsFaq::onRadioButton10Clicked(){
    if(!faq10) {
        faq10 = new SettingsFaq10(this);
        ui->stackedWidget->addWidget(faq10);
    }

    changeScreen(faq10);
}

void SettingsFaq::onRadioButton11Clicked(){
    if(!faq11) {
        faq11 = new SettingsFaq11(this);
        ui->stackedWidget->addWidget(faq11);
    }

    changeScreen(faq11);
}

void SettingsFaq::onRadioButton12Clicked(){
    if(!faq12) {
        faq12 = new SettingsFaq12(this);
        ui->stackedWidget->addWidget(faq12);
    }

    changeScreen(faq12);
}


SettingsFaq::~SettingsFaq()
{
    delete ui;
}
