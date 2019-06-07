// Copyright (c) 2019 The Veil developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef VEIL_MNEMONICDISPLAY_H
#define VEIL_MNEMONICDISPLAY_H

#include <QDialog>

namespace Ui {
    class MnemonicDisplay;
}

class MnemonicDisplay : public QDialog
{
    Q_OBJECT

public:
    explicit MnemonicDisplay(bool fRetry = false, QWidget *parent = 0);
    explicit MnemonicDisplay(QString seed, QWidget *parent = 0);
    ~MnemonicDisplay();
    bool ShutdownRequested() const { return shutdown; }
    QString GetImportSeed() const { return importSeed; }

private Q_SLOTS:
    void buttonClicked();

private:
    Ui::MnemonicDisplay *ui;
    bool shutdown;
    QString importSeed;
};


#endif //VEIL_MNEMONICDISPLAY_H
