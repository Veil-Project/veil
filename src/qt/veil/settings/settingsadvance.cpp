#include <qt/veil/settings/settingsadvance.h>
#include <qt/veil/forms/ui_settingsadvance.h>

SettingsAdvance::SettingsAdvance(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::SettingsAdvance)
{
    ui->setupUi(this);
    connect(ui->btnEsc,SIGNAL(clicked()),this, SLOT(onEscapeClicked()));
    connect(ui->btnInformation,SIGNAL(clicked()),this, SLOT(onInformationClicked()));
    connect(ui->btnConsole,SIGNAL(clicked()),this, SLOT(onConsoleClicked()));
    connect(ui->btnNetwork,SIGNAL(clicked()),this, SLOT(onNetworkClicked()));
    connect(ui->btnPeers,SIGNAL(clicked()),this, SLOT(onPeersClicked()));

    ui->labelTitle->setProperty("cssClass" , "title-dialog");
    ui->btnInformation->setStyleSheet("color: #105aef;font-size: 20px;font-weight: light;background-color: transparent;border: 0;background-image: url(':/icons/ic-title');background-repeat: no-repeat;background-position: center bottom;padding: 10 0 10 0;");
    ui->btnConsole->setStyleSheet("color: #bababa;font-size:20px;font-weight:light;background-color:transparent;border:0;padding:10 0 10 0;background-image:none;");
    ui->btnNetwork->setStyleSheet("color: #bababa;font-size:20px;font-weight:light;background-color:transparent;border:0;padding:10 0 10 0;background-image:none;");
    ui->btnPeers->setStyleSheet("color: #bababa;font-size:20px;font-weight:light;background-color:transparent;border:0;padding:10 0 10 0;background-image:none;");

    informationView = new SettingsAdvanceInformation(this);

    ui->stackedWidget->addWidget(informationView);
    ui->stackedWidget->setCurrentWidget(informationView);
}

void SettingsAdvance::changeScreen(QWidget *widget){
    ui->stackedWidget->setCurrentWidget(widget);
}


void SettingsAdvance::onInformationClicked(){
    informationView = new SettingsAdvanceInformation(this);
    ui->stackedWidget->addWidget(informationView);

    ui->btnInformation->setStyleSheet("color: #105aef;font-size: 20px;font-weight: light;background-color: transparent;border: 0;background-image: url(':/icons/ic-title');background-repeat: no-repeat;background-position: center bottom;padding: 10 0 10 0;");
    ui->btnConsole->setStyleSheet("color: #bababa;font-size:20px;font-weight:light;background-color:transparent;border:0;padding:10 0 10 0;background-image:none;");
    ui->btnNetwork->setStyleSheet("color: #bababa;font-size:20px;font-weight:light;background-color:transparent;border:0;padding:10 0 10 0;background-image:none;");
    ui->btnPeers->setStyleSheet("color: #bababa;font-size:20px;font-weight:light;background-color:transparent;border:0;padding:10 0 10 0;background-image:none;");

    changeScreen(informationView);
}

void SettingsAdvance::onConsoleClicked(){
    consoleView = new SettingsAdvanceConsole(this);
    ui->stackedWidget->addWidget(consoleView);

    ui->btnInformation->setStyleSheet("color: #bababa;font-size:20px;font-weight:light;background-color:transparent;border:0;padding:10 0 10 0;background-image:none;");
    ui->btnConsole->setStyleSheet("color: #105aef;font-size: 20px;font-weight: light;background-color: transparent;border: 0;background-image: url(':/icons/ic-title');background-repeat: no-repeat;background-position: center bottom;padding: 10 0 10 0;");
    ui->btnNetwork->setStyleSheet("color: #bababa;font-size:20px;font-weight:light;background-color:transparent;border:0;padding:10 0 10 0;background-image:none;");
    ui->btnPeers->setStyleSheet("color: #bababa;font-size:20px;font-weight:light;background-color:transparent;border:0;padding:10 0 10 0;background-image:none;");

    changeScreen(consoleView);

}

void SettingsAdvance::onNetworkClicked(){
    networkView = new SettingsAdvanceNetwork(this);
    ui->stackedWidget->addWidget(networkView);

    ui->btnInformation->setStyleSheet("color: #bababa;font-size:20px;font-weight:light;background-color:transparent;border:0;padding:10 0 10 0;background-image:none;");
    ui->btnConsole->setStyleSheet("color: #bababa;font-size:20px;font-weight:light;background-color:transparent;border:0;padding:10 0 10 0;background-image:none;");
    ui->btnNetwork->setStyleSheet("color: #105aef;font-size: 20px;font-weight: light;background-color: transparent;border: 0;background-image: url(':/icons/ic-title');background-repeat: no-repeat;background-position: center bottom;padding: 10 0 10 0;");
    ui->btnPeers->setStyleSheet("color: #bababa;font-size:20px;font-weight:light;background-color:transparent;border:0;padding:10 0 10 0;background-image:none;");

    changeScreen(networkView);
}

void SettingsAdvance::onPeersClicked(){
    ui->btnInformation->setStyleSheet("color: #bababa;font-size:20px;font-weight:light;background-color:transparent;border:0;padding:10 0 10 0;background-image:none;");
    ui->btnConsole->setStyleSheet("color: #bababa;font-size:20px;font-weight:light;background-color:transparent;border:0;padding:10 0 10 0;background-image:none;");
    ui->btnNetwork->setStyleSheet("color: #bababa;font-size:20px;font-weight:light;background-color:transparent;border:0;padding:10 0 10 0;background-image:none;");
    ui->btnPeers->setStyleSheet("color: #105aef;font-size: 20px;font-weight: light;background-color: transparent;border: 0;background-image: url(':/icons/ic-title');background-repeat: no-repeat;background-position: center bottom;padding: 10 0 10 0;");

}

void SettingsAdvance::onEscapeClicked(){
    close();
}


SettingsAdvance::~SettingsAdvance()
{
    delete ui;
}
