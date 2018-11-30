#ifndef SETTINGSMINTING_H
#define SETTINGSMINTING_H
#include <QWidget>
#include <QDialog>


namespace Ui {
class SettingsMinting;
}

class SettingsMinting : public QDialog
{
    Q_OBJECT

public:
    explicit SettingsMinting(QWidget *parent = nullptr);
    ~SettingsMinting();
private Q_SLOTS:
    void onEscapeClicked();
private:
    Ui::SettingsMinting *ui;
};

#endif // SETTINGSMINTING_H
