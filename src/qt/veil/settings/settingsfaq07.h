#ifndef SETTINGSFAQ07_H
#define SETTINGSFAQ07_H

#include <QWidget>

namespace Ui {
class SettingsFaq07;
}

class SettingsFaq07 : public QWidget
{
    Q_OBJECT

public:
    explicit SettingsFaq07(QWidget *parent = nullptr);
    ~SettingsFaq07();

private:
    Ui::SettingsFaq07 *ui;
};

#endif // SETTINGSFAQ07_H
