#ifndef TUTORIALMNEMONICREVEALED_H
#define TUTORIALMNEMONICREVEALED_H

#include <QWidget>
#include <list>
#include <QString>
#include <QLineEdit>

namespace Ui {
class TutorialMnemonicRevealed;
}

class TutorialMnemonicRevealed : public QWidget
{
    Q_OBJECT

public:
    explicit TutorialMnemonicRevealed(QStringList wordList, QWidget *parent = nullptr);
    ~TutorialMnemonicRevealed();

    std::list<QString> getOrderedStrings();

private Q_SLOTS:
    void textChanged(const QString &text);

private:
    Ui::TutorialMnemonicRevealed *ui;
    std::list<QLineEdit*> editList;

    QStringList wordList;
};

#endif // TUTORIALMNEMONICREVEALED_H
