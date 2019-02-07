#ifndef QTUTILS_H
#define QTUTILS_H

#include <QWidget>
#include <QDialog>
#include <QPropertyAnimation>
#include <QAbstractAnimation>
#include <QPoint>
#include <QString>
#include <QSettings>
#include <QStandardPaths>
#include <qt/bitcoingui.h>

class BitcoinGUI;


void openDialog(QWidget * dialog);
void openDialogFullScreen(QWidget *parent, QWidget * dialog);
bool openDialogWithOpaqueBackgroundY(QDialog *widget, BitcoinGUI *gui, double posX = 3, int posY = 5);
bool openDialogWithOpaqueBackground(QDialog *widget, BitcoinGUI *gui, double posX = 3);
void openDialogWithOpaqueBackgroundFullScreen(QDialog *widget, BitcoinGUI *gui);
QSettings* getSettings();


void openToastDialog(QString text, QWidget *gui);

#endif // QTUTILS_H
