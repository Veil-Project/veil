#ifndef SETTINGSFAQ04_H
#define SETTINGSFAQ04_H

#include <QWidget>

namespace Ui {
class SettingsFaq04;
}

class SettingsFaq04 : public QWidget
{
    Q_OBJECT

public:
    explicit SettingsFaq04(QWidget *parent = nullptr);
    ~SettingsFaq04();

private:
    Ui::SettingsFaq04 *ui;
};

#endif // SETTINGSFAQ04_H
