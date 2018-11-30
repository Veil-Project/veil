#ifndef SETTINGSRESTOREFILE_H
#define SETTINGSRESTOREFILE_H

#include <QWidget>

namespace Ui {
class SettingsRestoreFile;
}

class SettingsRestoreFile : public QWidget
{
    Q_OBJECT

public:
    explicit SettingsRestoreFile(QWidget *parent = nullptr);
    ~SettingsRestoreFile();

private:
    Ui::SettingsRestoreFile *ui;
};

#endif // SETTINGSRESTOREFILE_H
