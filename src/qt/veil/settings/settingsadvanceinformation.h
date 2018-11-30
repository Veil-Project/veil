#ifndef SETTINGSADVANCEINFORMATION_H
#define SETTINGSADVANCEINFORMATION_H

#include <QWidget>

namespace Ui {
class SettingsAdvanceInformation;
}

class SettingsAdvanceInformation : public QWidget
{
    Q_OBJECT

public:
    explicit SettingsAdvanceInformation(QWidget *parent = nullptr);
    ~SettingsAdvanceInformation();

private:
    Ui::SettingsAdvanceInformation *ui;
};

#endif // SETTINGSADVANCEINFORMATION_H
