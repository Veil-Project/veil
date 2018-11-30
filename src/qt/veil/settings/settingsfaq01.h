#ifndef SETTINGSFAQ01_H
#define SETTINGSFAQ01_H

#include <QWidget>

namespace Ui {
class SettingsFaq01;
}

class SettingsFaq01 : public QWidget
{
    Q_OBJECT

public:
    explicit SettingsFaq01(QWidget *parent = nullptr);
    ~SettingsFaq01();

private:
    Ui::SettingsFaq01 *ui;
};

#endif // SETTINGSFAQ01_H
