#ifndef SETTINGSADVANCENETWORK_H
#define SETTINGSADVANCENETWORK_H

#include <QWidget>

namespace Ui {
class SettingsAdvanceNetwork;
}

class SettingsAdvanceNetwork : public QWidget
{
    Q_OBJECT

public:
    explicit SettingsAdvanceNetwork(QWidget *parent = nullptr);
    ~SettingsAdvanceNetwork();

private:
    Ui::SettingsAdvanceNetwork *ui;
};

#endif // SETTINGSADVANCENETWORK_H
