#ifndef SETTINGSPREFERENCES_H
#define SETTINGSPREFERENCES_H

#include <QWidget>
#include <QDialog>

namespace Ui {
class SettingsPreferences;
}

class SettingsPreferences : public QDialog
{
    Q_OBJECT

public:
    explicit SettingsPreferences(QWidget *parent = nullptr);
    ~SettingsPreferences();
private Q_SLOTS:
    void onEscapeClicked();
private:
    Ui::SettingsPreferences *ui;
};

#endif // SETTINGSPREFERENCES_H
