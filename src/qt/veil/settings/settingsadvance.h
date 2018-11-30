#ifndef SETTINGSADVANCE_H
#define SETTINGSADVANCE_H

#include <qt/veil/settings/settingsadvanceinformation.h>
#include <qt/veil/settings/settingsadvanceconsole.h>
#include <qt/veil/settings/settingsadvancenetwork.h>
#include <qt/veil/settings/settingsadvancepeers.h>

#include <QWidget>
#include <QDialog>

namespace Ui {
class SettingsAdvance;
}

class SettingsAdvance : public QDialog
{
    Q_OBJECT

public:
    explicit SettingsAdvance(QWidget *parent = nullptr);
    ~SettingsAdvance();
private Q_SLOTS:
    void onEscapeClicked();
    void onInformationClicked();
    void onConsoleClicked();
    void onPeersClicked();
    void onNetworkClicked();
    void changeScreen(QWidget *widget);
private:
    Ui::SettingsAdvance *ui;
    SettingsAdvanceInformation *informationView;
    SettingsAdvanceConsole *consoleView;
    SettingsAdvanceNetwork *networkView;
    SettingsAdvancePeers *peersView;
};

#endif // SETTINGSADVANCE_H
