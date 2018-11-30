#ifndef SETTINGSFAQ03_H
#define SETTINGSFAQ03_H

#include <QWidget>

namespace Ui {
class SettingsFaq03;
}

class SettingsFaq03 : public QWidget
{
    Q_OBJECT

public:
    explicit SettingsFaq03(QWidget *parent = nullptr);
    ~SettingsFaq03();

private:
    Ui::SettingsFaq03 *ui;
};

#endif // SETTINGSFAQ03_H
