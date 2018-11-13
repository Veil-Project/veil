/********************************************************************************
** Form generated from reading UI file 'mnemonicdialog.ui'
**
** Created by: Qt User Interface Compiler version 5.9.5
**
** WARNING! All changes made in this file will be lost when recompiling UI file!
********************************************************************************/

#ifndef UI_MNEMONICDIALOG_H
#define UI_MNEMONICDIALOG_H

#include <QtCore/QVariant>
#include <QtWidgets/QAction>
#include <QtWidgets/QApplication>
#include <QtWidgets/QButtonGroup>
#include <QtWidgets/QDialog>
#include <QtWidgets/QDialogButtonBox>
#include <QtWidgets/QHeaderView>
#include <QtWidgets/QLabel>
#include <QtWidgets/QRadioButton>
#include <QtWidgets/QSpacerItem>
#include <QtWidgets/QVBoxLayout>

QT_BEGIN_NAMESPACE

class Ui_MnemonicDialog
{
public:
    QVBoxLayout *verticalLayout;
    QLabel *titlelabel;
    QSpacerItem *verticalSpacer_4;
    QLabel *lblExplanation;
    QSpacerItem *verticalSpacer_2;
    QRadioButton *seedNew;
    QRadioButton *seedImport;
    QSpacerItem *verticalSpacer;
    QDialogButtonBox *buttonBox;

    void setupUi(QDialog *MnemonicDialog)
    {
        if (MnemonicDialog->objectName().isEmpty())
            MnemonicDialog->setObjectName(QStringLiteral("MnemonicDialog"));
        MnemonicDialog->resize(674, 280);
        verticalLayout = new QVBoxLayout(MnemonicDialog);
        verticalLayout->setObjectName(QStringLiteral("verticalLayout"));
        titlelabel = new QLabel(MnemonicDialog);
        titlelabel->setObjectName(QStringLiteral("titlelabel"));
        QFont font;
        font.setPointSize(20);
        font.setBold(true);
        font.setWeight(75);
        titlelabel->setFont(font);

        verticalLayout->addWidget(titlelabel);

        verticalSpacer_4 = new QSpacerItem(20, 15, QSizePolicy::Minimum, QSizePolicy::Minimum);

        verticalLayout->addItem(verticalSpacer_4);

        lblExplanation = new QLabel(MnemonicDialog);
        lblExplanation->setObjectName(QStringLiteral("lblExplanation"));
        lblExplanation->setWordWrap(true);

        verticalLayout->addWidget(lblExplanation);

        verticalSpacer_2 = new QSpacerItem(20, 15, QSizePolicy::Minimum, QSizePolicy::Fixed);

        verticalLayout->addItem(verticalSpacer_2);

        seedNew = new QRadioButton(MnemonicDialog);
        seedNew->setObjectName(QStringLiteral("seedNew"));

        verticalLayout->addWidget(seedNew);

        seedImport = new QRadioButton(MnemonicDialog);
        seedImport->setObjectName(QStringLiteral("seedImport"));

        verticalLayout->addWidget(seedImport);

        verticalSpacer = new QSpacerItem(20, 40, QSizePolicy::Minimum, QSizePolicy::Expanding);

        verticalLayout->addItem(verticalSpacer);

        buttonBox = new QDialogButtonBox(MnemonicDialog);
        buttonBox->setObjectName(QStringLiteral("buttonBox"));
        buttonBox->setEnabled(false);
        buttonBox->setOrientation(Qt::Horizontal);
        buttonBox->setStandardButtons(QDialogButtonBox::Ok);

        verticalLayout->addWidget(buttonBox);


        retranslateUi(MnemonicDialog);
        QObject::connect(buttonBox, SIGNAL(accepted()), MnemonicDialog, SLOT(accept()));
        QObject::connect(buttonBox, SIGNAL(rejected()), MnemonicDialog, SLOT(reject()));

        QMetaObject::connectSlotsByName(MnemonicDialog);
    } // setupUi

    void retranslateUi(QDialog *MnemonicDialog)
    {
        MnemonicDialog->setWindowTitle(QApplication::translate("MnemonicDialog", "Welcome", Q_NULLPTR));
        titlelabel->setText(QApplication::translate("MnemonicDialog", "Wallet Seed Generation", Q_NULLPTR));
        lblExplanation->setText(QApplication::translate("MnemonicDialog", "Your Veil wallet will be generated with a mnemonic seed. In the event that you lose your wallet, this seed will allow you to regenerate it and regain access to your funds. It is important that you keep your seed somewhere safe and private.", Q_NULLPTR));
        seedNew->setText(QApplication::translate("MnemonicDialog", "Generate new seed", Q_NULLPTR));
        seedImport->setText(QApplication::translate("MnemonicDialog", "Regenerate from exisiting seed", Q_NULLPTR));
    } // retranslateUi

};

namespace Ui {
    class MnemonicDialog: public Ui_MnemonicDialog {};
} // namespace Ui

QT_END_NAMESPACE

#endif // UI_MNEMONICDIALOG_H
