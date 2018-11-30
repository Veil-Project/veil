#ifndef SETTINGSFAQ06_H
#define SETTINGSFAQ06_H

#include <QWidget>

namespace Ui {
class SettingsFaq06;
}

class SettingsFaq06 : public QWidget
{
    Q_OBJECT

public:
    explicit SettingsFaq06(QWidget *parent = nullptr);
    ~SettingsFaq06();

private:
    Ui::SettingsFaq06 *ui;
};

#endif // SETTINGSFAQ06_H
