#ifndef SETTINGSCHANGEPASSWORD_H
#define SETTINGSCHANGEPASSWORD_H

#include <QWidget>
#include <QDialog>

namespace Ui {
class SettingsChangePassword;
}

class SettingsChangePassword : public QDialog
{
    Q_OBJECT

public:
    explicit SettingsChangePassword(QWidget *parent = nullptr);
    ~SettingsChangePassword();
private Q_SLOTS:
    void onEscapeClicked();
private:
    Ui::SettingsChangePassword *ui;
};

#endif // SETTINGSCHANGEPASSWORD_H
