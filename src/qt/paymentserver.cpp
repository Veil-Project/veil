// Copyright (c) 2011-2019 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qt/paymentserver.h>

#include <qt/guiutil.h>
#include <qt/optionsmodel.h>

#include <chainparams.h>
#include <interfaces/node.h>
#include <key_io.h>
#include <ui_interface.h>
#include <util/system.h>

#include <cstdlib>
#include <memory>

#include <QApplication>
#include <QByteArray>
#include <QDataStream>
#include <QDebug>
#include <QFile>
#include <QFileOpenEvent>
#include <QHash>
#include <QList>
#include <QLocalServer>
#include <QLocalSocket>
#include <QMessageBox>
#include <QStringList>
#include <QUrlQuery>

const int BITCOIN_IPC_CONNECT_TIMEOUT = 1000; // milliseconds
const QString BITCOIN_IPC_PREFIX("veil:");

//
// Create a name that is unique for:
//  testnet / non-testnet
//  data directory
//
static QString ipcServerName()
{
    QString name("VeilQt");

    // Append a simple hash of the datadir
    // Note that GetDataDir(true) returns a different path
    // for -testnet versus main net
    QString ddir(GUIUtil::boostPathToQString(GetDataDir(true)));
    name.append(QString::number(qHash(ddir)));

    return name;
}

//
// We store payment URIs and requests received before
// the main GUI window is up and ready to ask the user
// to send payment.

static QList<QString> savedPaymentRequests;

//
// Sending to the server is done synchronously, at startup.
// If the server isn't already running, startup continues,
// and the items in savedPaymentRequest will be handled
// when uiReady() is called.
//
// Warning: ipcSendCommandLine() is called early in init,
// so don't use "Q_EMIT message()", but "QMessageBox::"!
//
void PaymentServer::ipcParseCommandLine(interfaces::Node& node, int argc, char* argv[])
{
    for (int i = 1; i < argc; i++)
    {
        QString arg(argv[i]);
        if (arg.startsWith("-"))
            continue;

        if (arg.startsWith(BITCOIN_IPC_PREFIX, Qt::CaseInsensitive)) // veil: URI
        {
            savedPaymentRequests.append(arg);

            SendCoinsRecipient r;
            if (GUIUtil::parseBitcoinURI(arg, &r) && !r.address.isEmpty())
            {
                auto tempChainParams = CreateChainParams(CBaseChainParams::MAIN);

                if (IsValidDestinationString(r.address.toStdString(), *tempChainParams)) {
                    node.selectParams(CBaseChainParams::MAIN);
                } else {
                    tempChainParams = CreateChainParams(CBaseChainParams::TESTNET);
                    if (IsValidDestinationString(r.address.toStdString(), *tempChainParams)) {
                        node.selectParams(CBaseChainParams::TESTNET);
                    } else {
                        tempChainParams = CreateChainParams(CBaseChainParams::DEVNET);
                        if (IsValidDestinationString(r.address.toStdString(), *tempChainParams)) {
                            node.selectParams(CBaseChainParams::DEVNET);
                        }
                    }
                }
            }
        }
        else
        {
            // Printing to debug.log is about the best we can do here, the
            // GUI hasn't started yet so we can't pop up a message box.
            qWarning() << "PaymentServer::ipcSendCommandLine: Ignoring non-URI argument (BIP70 payment request files are no longer supported): " << arg;
        }
    }
}

//
// Sending to the server is done synchronously, at startup.
// If the server isn't already running, startup continues,
// and the items in savedPaymentRequest will be handled
// when uiReady() is called.
//
bool PaymentServer::ipcSendCommandLine()
{
    bool fResult = false;
    for (const QString& r : savedPaymentRequests)
    {
        QLocalSocket* socket = new QLocalSocket();
        socket->connectToServer(ipcServerName(), QIODevice::WriteOnly);
        if (!socket->waitForConnected(BITCOIN_IPC_CONNECT_TIMEOUT))
        {
            delete socket;
            socket = nullptr;
            return false;
        }

        QByteArray block;
        QDataStream out(&block, QIODevice::WriteOnly);
        out.setVersion(QDataStream::Qt_4_0);
        out << r;
        out.device()->seek(0);

        socket->write(block);
        socket->flush();
        socket->waitForBytesWritten(BITCOIN_IPC_CONNECT_TIMEOUT);
        socket->disconnectFromServer();

        delete socket;
        socket = nullptr;
        fResult = true;
    }

    return fResult;
}

PaymentServer::PaymentServer(QObject* parent, bool startLocalServer) :
    QObject(parent),
    saveURIs(true),
    uriServer(0),
    optionsModel(0)
{
    // Install global event filter to catch QFileOpenEvents
    // on Mac: sent when you click veil: links
    // other OSes: helpful when dealing with payment request files
    if (parent)
        parent->installEventFilter(this);

    QString name = ipcServerName();

    // Clean up old socket leftover from a crash:
    QLocalServer::removeServer(name);

    if (startLocalServer)
    {
        uriServer = new QLocalServer(this);

        if (!uriServer->listen(name)) {
            // constructor is called early in init, so don't use "Q_EMIT message()" here
            QMessageBox::critical(0, tr("Payment request error"),
                tr("Cannot start veil: click-to-pay handler"));
        }
        else {
            connect(uriServer, SIGNAL(newConnection()), this, SLOT(handleURIConnection()));
        }
    }
}

PaymentServer::~PaymentServer()
{
}

//
// OSX-specific way of handling veil: URIs.
// Also used when opening a URI via the "Open URI..." menu entry.
//
bool PaymentServer::eventFilter(QObject *object, QEvent *event)
{
    if (event->type() == QEvent::FileOpen) {
        QFileOpenEvent *fileEvent = static_cast<QFileOpenEvent*>(event);
        if (!fileEvent->file().isEmpty())
            handleURIOrFile(fileEvent->file());
        else if (!fileEvent->url().isEmpty())
            handleURIOrFile(fileEvent->url().toString());

        return true;
    }

    return QObject::eventFilter(object, event);
}

void PaymentServer::uiReady()
{
    saveURIs = false;
    for (const QString& s : savedPaymentRequests)
    {
        handleURIOrFile(s);
    }
    savedPaymentRequests.clear();
}

void PaymentServer::handleURIOrFile(const QString& s)
{
    if (saveURIs)
    {
        savedPaymentRequests.append(s);
        return;
    }

    if (s.startsWith("veil://", Qt::CaseInsensitive))
    {
        Q_EMIT message(tr("URI handling"), tr("'veil://' is not a valid URI. Use 'veil:' instead."),
            CClientUIInterface::MSG_ERROR);
    }
    else if (s.startsWith(BITCOIN_IPC_PREFIX, Qt::CaseInsensitive)) // veil: URI
    {
        QUrlQuery uri((QUrl(s)));
        if (uri.hasQueryItem("r")) // payment request URI
        {
            // The URI points to a BIP70 payment request; that protocol is no
            // longer supported (removed together with its OpenSSL dependency,
            // following upstream). Ask the sender for a plain address instead.
            Q_EMIT message(tr("URI handling"),
                tr("Cannot process payment request because BIP70 is not supported. Please ask the merchant for a BIP21 compatible URI."),
                CClientUIInterface::ICON_WARNING);
            return;
        }
        else // normal URI
        {
            SendCoinsRecipient recipient;
            if (GUIUtil::parseBitcoinURI(s, &recipient))
            {
                if (!IsValidDestinationString(recipient.address.toStdString())) {
                    Q_EMIT message(tr("URI handling"), tr("Invalid payment address %1").arg(recipient.address),
                        CClientUIInterface::MSG_ERROR);
                }
                else
                    Q_EMIT receivedPaymentRequest(recipient);
            }
            else
                Q_EMIT message(tr("URI handling"),
                    tr("URI cannot be parsed! This can be caused by an invalid Veil address or malformed URI parameters."),
                    CClientUIInterface::ICON_WARNING);

            return;
        }
    }

    if (QFile::exists(s)) // payment request file
    {
        Q_EMIT message(tr("Payment request file handling"),
            tr("Payment request files (BIP70) are no longer supported. Please ask the merchant for a BIP21 compatible URI."),
            CClientUIInterface::ICON_WARNING);
        return;
    }
}

void PaymentServer::handleURIConnection()
{
    QLocalSocket *clientConnection = uriServer->nextPendingConnection();

    while (clientConnection->bytesAvailable() < (int)sizeof(quint32))
        clientConnection->waitForReadyRead();

    connect(clientConnection, SIGNAL(disconnected()),
            clientConnection, SLOT(deleteLater()));

    QDataStream in(clientConnection);
    in.setVersion(QDataStream::Qt_4_0);
    if (clientConnection->bytesAvailable() < (int)sizeof(quint16)) {
        return;
    }
    QString msg;
    in >> msg;

    handleURIOrFile(msg);
}

void PaymentServer::setOptionsModel(OptionsModel *_optionsModel)
{
    this->optionsModel = _optionsModel;
}
