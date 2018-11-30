#include <qt/veil/settings/settingspreferences.h>
#include <qt/veil/forms/ui_settingspreferences.h>

SettingsPreferences::SettingsPreferences(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::SettingsPreferences)
{
    ui->setupUi(this);
    ui->labelTitle->setProperty("cssClass" , "title-dialog");
    ui->btnSave->setProperty("cssClass" , "btn-text-primary");
    ui->btnEsc->setProperty("cssClass" , "btn-text-primary-inactive");
    connect(ui->btnEsc,SIGNAL(clicked()),this, SLOT(onEscapeClicked()));
    //Combobox
    ui->comboLanguague->addItem(tr("English"));
    ui->comboLanguague->addItem(tr("Spanish"));

    //ui->checkBoxLaunch
    //ui->checkBoxMinTray
    //ui->checkBoxMinExit
    //ui->checkBoxPrivacy
}

void SettingsPreferences::onEscapeClicked(){
    close();
}

SettingsPreferences::~SettingsPreferences()
{
    delete ui;
}
