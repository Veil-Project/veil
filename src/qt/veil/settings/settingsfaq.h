#ifndef SETTINGSFAQ_H
#define SETTINGSFAQ_H

#include <QWidget>
#include <QDialog>
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


namespace Ui {
class SettingsFaq;
}

class SettingsFaq : public QDialog
{
    Q_OBJECT

public:
    explicit SettingsFaq(QWidget *parent = nullptr, bool howToObtainVeil = false);
    ~SettingsFaq();
private Q_SLOTS:
    void onEscapeClicked();
    void onRadioButton01Clicked();
    void onRadioButton02Clicked();
    void onRadioButton03Clicked();
    void onRadioButton04Clicked();
    void onRadioButton05Clicked();
    void onRadioButton06Clicked();
    void onRadioButton07Clicked();
    void onRadioButton08Clicked();
    void onRadioButton09Clicked();
    void onRadioButton10Clicked();
    void onRadioButton11Clicked();
    void changeScreen(QWidget *widget);

private:
    Ui::SettingsFaq *ui;
    SettingsFaq01 *faq01;
    SettingsFaq02 *faq02;
    SettingsFaq03 *faq03;
    SettingsFaq04 *faq04;
    SettingsFaq05 *faq05;
    SettingsFaq06 *faq06;
    SettingsFaq07 *faq07;
    SettingsFaq08 *faq08;
    SettingsFaq09 *faq09;
    SettingsFaq10 *faq10;
    SettingsFaq11 *faq11;
};

#endif // SETTINGSFAQ_H
