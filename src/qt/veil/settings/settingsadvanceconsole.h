#ifndef SETTINGSADVANCECONSOLE_H
#define SETTINGSADVANCECONSOLE_H

#include <QWidget>

namespace Ui {
class SettingsAdvanceConsole;
}

class SettingsAdvanceConsole : public QWidget
{
    Q_OBJECT

public:
    explicit SettingsAdvanceConsole(QWidget *parent = nullptr);
    ~SettingsAdvanceConsole();

private:
    Ui::SettingsAdvanceConsole *ui;
};

#endif // SETTINGSADVANCECONSOLE_H
