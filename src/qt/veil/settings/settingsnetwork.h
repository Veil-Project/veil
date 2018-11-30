#ifndef SETTINGSNETWORK_H
#define SETTINGSNETWORK_H

#include <QWidget>
#include <QDialog>

namespace Ui {
class SettingsNetwork;
}

class SettingsNetwork : public QDialog
{
    Q_OBJECT

public:
    explicit SettingsNetwork(QWidget *parent = nullptr);
    ~SettingsNetwork();
private Q_SLOTS:
    void onEscapeClicked();
private:
    Ui::SettingsNetwork *ui;
};

#endif // SETTINGSNETWORK_H
