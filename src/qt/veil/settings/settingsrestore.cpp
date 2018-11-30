#include <qt/veil/settings/settingsrestore.h>
#include <qt/veil/forms/ui_settingsrestore.h>
#include <qt/veil/settings/settingsrestoreseed.h>
#include <qt/veil/settings/settingsrestorefile.h>

SettingsRestore::SettingsRestore(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::SettingsRestore)
{
    ui->setupUi(this);

    ui->btnSave->setProperty("cssClass" , "btn-text-primary");
    ui->btnEsc->setProperty("cssClass" , "btn-text-primary-inactive");
    connect(ui->btnEsc,SIGNAL(clicked()),this, SLOT(onEscapeClicked()));
    connect(ui->btnFile,SIGNAL(clicked()),this, SLOT(onFileClicked()));
    connect(ui->btnSeed,SIGNAL(clicked()),this, SLOT(onSeedClicked()));
    ui->stackedWidget->setContentsMargins(0,0,0,0);

    restoreSeed = new SettingsRestoreSeed(this);

    ui->stackedWidget->addWidget(restoreSeed);
    ui->stackedWidget->setCurrentWidget(restoreSeed);
    ui->btnSeed->setStyleSheet("color: #105aef;font-size: 20px;font-weight: light;background-color: transparent;border: 0;background-image: url(':/icons/ic-title');background-repeat: no-repeat;background-position: center bottom;padding: 10 0 10 0;");
    ui->btnFile->setStyleSheet("color: #bababa;font-size:20px;font-weight:light;background-color:transparent;border:0;padding:10 0 10 0;background-image:none;");
}

void SettingsRestore::changeScreen(QWidget *widget){
    ui->stackedWidget->setCurrentWidget(widget);
}

void SettingsRestore::onFileClicked(){
    restoreFile = new SettingsRestoreFile(this);
    ui->stackedWidget->addWidget(restoreFile);
    ui->btnFile->setStyleSheet("color: #105aef;font-size: 20px;font-weight: light;background-color: transparent;border: 0;background-image: url(':/icons/ic-title');background-repeat: no-repeat;background-position: center bottom;padding: 10 0 10 0;");
    ui->btnSeed->setStyleSheet("color: #bababa;font-size:20px;font-weight:light;background-color:transparent;border:0;padding:10 0 10 0;background-image:none;");
    changeScreen(restoreFile);
}

void SettingsRestore::onSeedClicked(){
    restoreSeed = new SettingsRestoreSeed(this);
    ui->stackedWidget->addWidget(restoreSeed);
    ui->btnSeed->setStyleSheet("color: #105aef;font-size: 20px;font-weight: light;background-color: transparent;border: 0;background-image: url(':/icons/ic-title');background-repeat: no-repeat;background-position: center bottom;padding: 10 0 10 0;");
    ui->btnFile->setStyleSheet("color: #bababa;font-size:20px;font-weight:light;background-color:transparent;border:0;padding:10 0 10 0;");

    changeScreen(restoreSeed);
}

void SettingsRestore::onEscapeClicked(){
    close();
}

SettingsRestore::~SettingsRestore()
{
    delete ui;
}
