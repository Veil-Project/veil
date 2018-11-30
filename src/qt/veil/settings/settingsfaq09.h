#ifndef SETTINGSFAQ09_H
#define SETTINGSFAQ09_H

#include <QWidget>

namespace Ui {
class SettingsFaq09;
}

class SettingsFaq09 : public QWidget
{
    Q_OBJECT

public:
    explicit SettingsFaq09(QWidget *parent = nullptr);
    ~SettingsFaq09();

private:
    Ui::SettingsFaq09 *ui;
};

#endif // SETTINGSFAQ09_H
