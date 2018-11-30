#ifndef SETTINGSBACKUP_H
#define SETTINGSBACKUP_H

#include <QWidget>
#include <QDialog>

namespace Ui {
class SettingsBackup;
}

class SettingsBackup : public QDialog
{
    Q_OBJECT

public:
    explicit SettingsBackup(QWidget *parent = nullptr);
    ~SettingsBackup();
private Q_SLOTS:
    void onEscapeClicked();
private:
    Ui::SettingsBackup *ui;
};

#endif // SETTINGSBACKUP_H
