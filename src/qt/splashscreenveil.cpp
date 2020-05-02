// Copyright (c) 2019-2020 The Veil developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qt/splashscreenveil.h>
#include <qt/forms/ui_splashscreenveil.h>
#include <QPixmap>

#if defined(HAVE_CONFIG_H)
#include <config/veil-config.h>
#endif

#ifdef ENABLE_WALLET
#include <wallet/wallet.h> // For DEFAULT_DISABLE_WALLET
#endif

#include <qt/networkstyle.h>

#include <qt/guiutil.h>

#include <clientversion.h>
#include <interfaces/handler.h>
#include <interfaces/node.h>
#include <interfaces/wallet.h>
#include <util.h>
#include <ui_interface.h>
#include <version.h>

#include <QApplication>
#include <QCloseEvent>
#include <QRadialGradient>
#include <QScreen>


SplashScreenVeil::SplashScreenVeil(interfaces::Node& node, Qt::WindowFlags f, const NetworkStyle *networkStyle) :
    QWidget(0, f),ui(new Ui::SplashScreenVeil), curAlignment(0), m_node(node)
{

    ui->setupUi(this);

    // define text to place
    QString titleText       = tr(PACKAGE_NAME);
    QString versionText     = QString("Version %1").arg(QString::fromStdString(FormatFullVersion()));
    QString copyrightText   = QString::fromUtf8(CopyrightHolders(strprintf("\xc2\xA9 %u-%u ", 2009, COPYRIGHT_YEAR)).c_str());
    QString titleAddText    = networkStyle->getTitleAddText();

    QString font            = QApplication::font().toString();

    this->setStyleSheet(GUIUtil::loadStyleSheet());

    this->resize(800, 514);
    ui->frame->setStyleSheet("#frame{border-image: url(\":/icons/splash_background\") 0 0 0 0 stretch stretch;padding: 0;margin: 0;}");

    // Set window title
    setWindowTitle(titleText + " " + titleAddText);

    // Resize window and move to center of desktop, disallow resizing
    QRect screenGeometry = QGuiApplication::primaryScreen()->geometry();
    int x = (screenGeometry.width() - width()) / 2;
    int y = (screenGeometry.height() - height()) / 2;
    move(x, y);

    subscribeToCoreSignals();
    installEventFilter(this);
}

SplashScreenVeil::~SplashScreenVeil()
{
    unsubscribeFromCoreSignals();
    delete ui;
}

bool SplashScreenVeil::eventFilter(QObject * obj, QEvent * ev) {
    if (ev->type() == QEvent::KeyPress) {
        QKeyEvent *keyEvent = static_cast<QKeyEvent *>(ev);
        if(keyEvent->text()[0] == 'q') {
            m_node.startShutdown();
        }
    }
    return QObject::eventFilter(obj, ev);
}

void SplashScreenVeil::slotFinish(QWidget *mainWin)
{
    Q_UNUSED(mainWin);

    /* If the window is minimized, hide() will be ignored. */
    /* Make sure we de-minimize the splashscreen window before hiding */
    if (isMinimized())
        showNormal();
    hide();
    deleteLater(); // No more need for this
}

static void InitMessage(SplashScreenVeil *splash, const std::string &message)
{
    QMetaObject::invokeMethod(splash, "showMessage",
        Qt::QueuedConnection,
        Q_ARG(QString, QString::fromStdString(message)),
        Q_ARG(int, Qt::AlignBottom|Qt::AlignHCenter),
        Q_ARG(QColor, QColor(55,55,55)));
}

static void ShowProgress(SplashScreenVeil *splash, const std::string &title, int nProgress, bool resume_possible)
{
    InitMessage(splash, title + std::string("\n") +
            (resume_possible ? _("(press q to shutdown and continue later)")
                                : _("press q to shutdown")) +
            strprintf("\n%d", nProgress) + "%");
}
#ifdef ENABLE_WALLET
void SplashScreenVeil::ConnectWallet(std::unique_ptr<interfaces::Wallet> wallet)
{
    if (gArgs.GetBoolArg("-disablewallet", DEFAULT_DISABLE_WALLET))
        return;

    m_connected_wallet_handlers.emplace_back(wallet->handleShowProgress(std::bind(ShowProgress, this, std::placeholders::_1, std::placeholders::_2, false)));
    m_connected_wallets.emplace_back(std::move(wallet));
}
#endif

void SplashScreenVeil::subscribeToCoreSignals()
{
    // Connect signals to client
    m_handler_init_message = m_node.handleInitMessage(std::bind(InitMessage, this, std::placeholders::_1));
    m_handler_show_progress = m_node.handleShowProgress(std::bind(ShowProgress, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));
#ifdef ENABLE_WALLET
    if (!gArgs.GetBoolArg("-disablewallet", DEFAULT_DISABLE_WALLET))
        m_handler_load_wallet = m_node.handleLoadWallet([this](std::unique_ptr<interfaces::Wallet> wallet) { ConnectWallet(std::move(wallet)); });
#endif
}

void SplashScreenVeil::unsubscribeFromCoreSignals()
{
    // Disconnect signals from client
    m_handler_init_message->disconnect();
    m_handler_show_progress->disconnect();
    for (auto& handler : m_connected_wallet_handlers) {
        handler->disconnect();
    }
    m_connected_wallet_handlers.clear();
    m_connected_wallets.clear();
}

void SplashScreenVeil::showMessage(const QString &message, int alignment, const QColor &color)
{
    curMessage = message;
    curAlignment = alignment;
    curColor = color;
    ui->labelLoading->setText("<html><head/><body><p><span style=\" color:#ffffff;\">"+ curMessage + "</span></p></body></html>");
}

void SplashScreenVeil::closeEvent(QCloseEvent *event)
{
    m_node.startShutdown(); // allows an "emergency" shutdown during startup
    event->ignore();
}
