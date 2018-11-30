#ifndef TUTORIALLANGUAGESWIDGET_H
#define TUTORIALLANGUAGESWIDGET_H

#include <QWidget>

namespace Ui {
class TutorialLanguagesWidget;
}

class TutorialLanguagesWidget : public QWidget
{
    Q_OBJECT

public:
    explicit TutorialLanguagesWidget(QWidget *parent = nullptr);
    ~TutorialLanguagesWidget();

private:
    Ui::TutorialLanguagesWidget *ui;
};

#endif // TUTORIALLANGUAGESWIDGET_H
