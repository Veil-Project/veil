#ifndef SETTINGSADVANCEPEERS_H
#define SETTINGSADVANCEPEERS_H

#include <QWidget>

namespace Ui {
class SettingsAdvancePeers;
}

class SettingsAdvancePeers : public QWidget
{
    Q_OBJECT

public:
    explicit SettingsAdvancePeers(QWidget *parent = nullptr);
    ~SettingsAdvancePeers();

private:
    Ui::SettingsAdvancePeers *ui;
};

#endif // SETTINGSADVANCEPEERS_H
