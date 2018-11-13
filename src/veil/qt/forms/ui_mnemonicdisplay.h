/********************************************************************************
** Form generated from reading UI file 'mnemonicdisplay.ui'
**
** Created by: Qt User Interface Compiler version 5.9.5
**
** WARNING! All changes made in this file will be lost when recompiling UI file!
********************************************************************************/

#ifndef UI_MNEMONICDISPLAY_H
#define UI_MNEMONICDISPLAY_H

#include <QtCore/QVariant>
#include <QtWidgets/QAction>
#include <QtWidgets/QApplication>
#include <QtWidgets/QButtonGroup>
#include <QtWidgets/QDialog>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QHeaderView>
#include <QtWidgets/QLabel>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QSpacerItem>
#include <QtWidgets/QTextEdit>
#include <QtWidgets/QVBoxLayout>

QT_BEGIN_NAMESPACE

class Ui_MnemonicDisplay
{
public:
    QVBoxLayout *verticalLayout;
    QLabel *titlelabel;
    QSpacerItem *verticalSpacer_4;
    QLabel *lblExplanation;
    QSpacerItem *verticalSpacer_2;
    QTextEdit *seedEdit;
    QHBoxLayout *horizontalLayout;
    QSpacerItem *horizontalSpacer;
    QPushButton *pushButton;
    QSpacerItem *verticalSpacer;

    void setupUi(QDialog *MnemonicDisplay)
    {
        if (MnemonicDisplay->objectName().isEmpty())
            MnemonicDisplay->setObjectName(QStringLiteral("MnemonicDisplay"));
        MnemonicDisplay->resize(674, 280);
        verticalLayout = new QVBoxLayout(MnemonicDisplay);
        verticalLayout->setObjectName(QStringLiteral("verticalLayout"));
        titlelabel = new QLabel(MnemonicDisplay);
        titlelabel->setObjectName(QStringLiteral("titlelabel"));
        QFont font;
        font.setPointSize(20);
        font.setBold(true);
        font.setWeight(75);
        titlelabel->setFont(font);

        verticalLayout->addWidget(titlelabel);

        verticalSpacer_4 = new QSpacerItem(20, 15, QSizePolicy::Minimum, QSizePolicy::Minimum);

        verticalLayout->addItem(verticalSpacer_4);

        lblExplanation = new QLabel(MnemonicDisplay);
        lblExplanation->setObjectName(QStringLiteral("lblExplanation"));
        lblExplanation->setWordWrap(true);

        verticalLayout->addWidget(lblExplanation);

        verticalSpacer_2 = new QSpacerItem(20, 15, QSizePolicy::Minimum, QSizePolicy::Fixed);

        verticalLayout->addItem(verticalSpacer_2);

        seedEdit = new QTextEdit(MnemonicDisplay);
        seedEdit->setObjectName(QStringLiteral("seedEdit"));

        verticalLayout->addWidget(seedEdit);

        horizontalLayout = new QHBoxLayout();
        horizontalLayout->setObjectName(QStringLiteral("horizontalLayout"));
        horizontalSpacer = new QSpacerItem(40, 20, QSizePolicy::Expanding, QSizePolicy::Minimum);

        horizontalLayout->addItem(horizontalSpacer);

        pushButton = new QPushButton(MnemonicDisplay);
        pushButton->setObjectName(QStringLiteral("pushButton"));

        horizontalLayout->addWidget(pushButton);


        verticalLayout->addLayout(horizontalLayout);

        verticalSpacer = new QSpacerItem(20, 40, QSizePolicy::Minimum, QSizePolicy::Expanding);

        verticalLayout->addItem(verticalSpacer);


        retranslateUi(MnemonicDisplay);

        QMetaObject::connectSlotsByName(MnemonicDisplay);
    } // setupUi

    void retranslateUi(QDialog *MnemonicDisplay)
    {
        MnemonicDisplay->setWindowTitle(QApplication::translate("MnemonicDisplay", "Welcome", Q_NULLPTR));
        titlelabel->setText(QApplication::translate("MnemonicDisplay", "Wallet Seed Generation", Q_NULLPTR));
        lblExplanation->setText(QApplication::translate("MnemonicDisplay", "Please store your seed somewhere safe. You will be unable to recover your wallet without it.", Q_NULLPTR));
        pushButton->setText(QApplication::translate("MnemonicDisplay", "Continue", Q_NULLPTR));
    } // retranslateUi

};

namespace Ui {
    class MnemonicDisplay: public Ui_MnemonicDisplay {};
} // namespace Ui

QT_END_NAMESPACE

#endif // UI_MNEMONICDISPLAY_H
