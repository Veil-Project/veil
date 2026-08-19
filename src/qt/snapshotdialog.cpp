// Copyright (c) 2026 The Veil developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qt/snapshotdialog.h>

#include <qt/snapshotdownloader.h>

#include <chainparamsbase.h>
#include <util/system.h>

#include <QEventLoop>
#include <QLabel>
#include <QMessageBox>
#include <QProgressBar>
#include <QPushButton>
#include <QThread>
#include <QTimer>
#include <QVBoxLayout>
#include <QCloseEvent>
#include <QKeyEvent>

#include <ctime>

namespace {

//! Offer when the chain tip on disk looks at least this far behind. At one
//! minute blocks that is ~43000 blocks, hours of syncing, and the quarterly
//! snapshot cadence means anything staler gains real time from a download.
constexpr int STALE_DAYS = 30;

QString Gb(qint64 bytes)
{
    return QString::number(double(bytes) / 1e9, 'f', 1);
}

//! A chain still early in its initial sync gains nearly the full benefit of
//! a snapshot. A fully synced mainnet blocks folder is ~26 GB, testnet ~12,
//! so anything under this has days of syncing ahead of it.
constexpr uintmax_t EARLY_SYNC_BYTES = 4ULL * 1000 * 1000 * 1000;

//! Decides with no node running whether offering a snapshot makes sense:
//!  - no chain on disk at all, the fresh install
//!  - a leftover snapshot-staging download, the user accepted once already
//!    and deserves the resume offer until it finishes or they delete it
//!  - barely any chain, someone early in a genesis sync (fresh mtimes, so
//!    the staleness check alone misses this completely)
//!  - a chain whose newest block file is weeks old, the returning user
bool OfferMakesSense(const fs::path& chainDir)
{
    fs::path blocksDir = chainDir / "blocks";
    if (!fs::exists(blocksDir)) return true;

    boost::system::error_code ec;
    fs::path staging = chainDir / "snapshot-staging";
    if (fs::exists(staging, ec) && !ec && !fs::is_empty(staging, ec)) return true;

    std::time_t newest = 0;
    uintmax_t totalBytes = 0;
    for (fs::directory_iterator it(blocksDir, ec); !ec && it != fs::directory_iterator(); ++it) {
        boost::system::error_code fec;
        if (fs::is_regular_file(it->path(), fec) && !fec) {
            uintmax_t sz = fs::file_size(it->path(), fec);
            if (!fec) totalBytes += sz;
        }
        std::time_t t = fs::last_write_time(it->path(), fec);
        if (!fec && t > newest) newest = t;
    }
    if (newest == 0) return true;
    if (totalBytes < EARLY_SYNC_BYTES) return true;
    return std::time(nullptr) - newest > std::time_t(STALE_DAYS) * 24 * 3600;
}

} // namespace

void SnapshotDialog::MaybeOffer(QWidget* parent)
{
    if (!gArgs.GetBoolArg("-snapshotprompt", true)) return;

    const std::string chain = gArgs.GetChainName();
    if (chain != "main" && chain != "test") return;

    const fs::path chainDir = GetDataDir(true);
    if (!OfferMakesSense(chainDir)) return;

    SnapshotDialog dlg(QString::fromStdString(chain), chainDir, parent);
    if (dlg.prepare()) {
        dlg.exec();
    }
}

SnapshotDialog::SnapshotDialog(const QString& chain, const fs::path& chainDir, QWidget* parent)
    : QDialog(parent), m_chain(chain), m_chainDir(chainDir)
{
    m_stagingDir = QString::fromStdString((chainDir / "snapshot-staging").string());
    m_extractDir = m_stagingDir + "/extracted";

    setWindowTitle(tr("Blockchain snapshot"));
    setMinimumWidth(480);

    auto* layout = new QVBoxLayout(this);

    m_headline = new QLabel(this);
    m_headline->setWordWrap(true);
    layout->addWidget(m_headline);

    m_detail = new QLabel(this);
    m_detail->setWordWrap(true);
    layout->addWidget(m_detail);

    m_status = new QLabel(this);
    m_status->setWordWrap(true);
    m_status->hide();
    layout->addWidget(m_status);

    m_bar = new QProgressBar(this);
    m_bar->setTextVisible(true);
    // the application stylesheet does not cover QProgressBar, which leaves
    // the chunk invisible on some platforms, so this widget styles itself
    // dark track and explicit text color, so the centered percent label is
    // readable at any fill level instead of white on white until halfway
    m_bar->setStyleSheet(QStringLiteral(
        "QProgressBar { border: 1px solid #3a4d63; border-radius: 3px; text-align: center;"
        "               background-color: #16222f; color: #e8eef5; }"
        "QProgressBar::chunk { background-color: #105aef; }"));
    m_bar->hide();
    layout->addWidget(m_bar);

    auto* buttons = new QHBoxLayout();
    m_decline = new QPushButton(tr("Sync from network"), this);
    m_download = new QPushButton(tr("Download snapshot"), this);
    m_download->setDefault(true);
    m_cancel = new QPushButton(tr("Cancel download"), this);
    m_cancel->hide();
    buttons->addStretch();
    buttons->addWidget(m_decline);
    buttons->addWidget(m_download);
    buttons->addWidget(m_cancel);
    layout->addLayout(buttons);

    connect(m_download, &QPushButton::clicked, this, &SnapshotDialog::startDownload);
    connect(m_decline, &QPushButton::clicked, this, [this]() {
        askAboutLeftovers();
        reject();
    });
    connect(m_cancel, &QPushButton::clicked, this, &SnapshotDialog::cancelPressed);

    // worker on its own thread, everything reaches this dialog as queued signals
    m_thread = new QThread(this);
    m_worker = new SnapshotDownloader(m_stagingDir, m_chain);
    m_worker->moveToThread(m_thread);
    connect(m_thread, &QThread::finished, m_worker, &QObject::deleteLater);
    connect(m_worker, &SnapshotDownloader::manifestReady, this, &SnapshotDialog::onManifestReady);
    connect(m_worker, &SnapshotDownloader::progress, this, &SnapshotDialog::onProgress);
    connect(m_worker, &SnapshotDownloader::downloadFinished, this, &SnapshotDialog::onDownloadFinished);
    connect(m_worker, &SnapshotDownloader::extractProgress, this, &SnapshotDialog::onExtractProgress);
    connect(m_worker, &SnapshotDownloader::extractFinished, this, &SnapshotDialog::onExtractFinished);
    connect(m_worker, &SnapshotDownloader::failed, this, &SnapshotDialog::onFailed);
    m_thread->start();
}

SnapshotDialog::~SnapshotDialog()
{
    m_thread->quit();
    m_thread->wait();
}

bool SnapshotDialog::prepare()
{
    // fetch the manifest quietly with a bounded wait: no reachable manifest,
    // no offer, and the user never learns this ran
    QEventLoop loop;
    bool done = false;
    connect(m_worker, &SnapshotDownloader::manifestReady, &loop, [&]() { done = true; loop.quit(); });
    connect(m_worker, &SnapshotDownloader::failed, &loop, [&]() { loop.quit(); });
    QTimer::singleShot(15000, &loop, [&]() { loop.quit(); });
    QMetaObject::invokeMethod(m_worker, "fetchManifest", Qt::QueuedConnection);
    loop.exec();
    return done && m_manifestOk;
}

void SnapshotDialog::onManifestReady(int height, qint64 compressedBytes, qint64 neededBytes, qint64 availableBytes)
{
    m_compressedBytes = compressedBytes;
    m_manifestOk = true;

    m_headline->setText(QStringLiteral("<b>%1</b>").arg(
        tr("A verified blockchain snapshot is available.")));

    QString rows = tr("Downloading it gets this wallet running in well under an hour on a decent "
                      "connection. Syncing everything from the network instead can take days.");
    rows += QStringLiteral("<br><br>");
    rows += tr("Snapshot height: %1").arg(height) + QStringLiteral("<br>");
    rows += tr("Download size: %1 GB").arg(Gb(compressedBytes)) + QStringLiteral("<br>");
    rows += tr("Free disk needed while setting up: %1 GB").arg(Gb(neededBytes)) + QStringLiteral("<br>");
    rows += tr("Free disk now: %1 GB").arg(Gb(availableBytes));
    rows += QStringLiteral("<br><br><small>");
    rows += tr("Every file is checked against the published checksums, and the node fully "
               "validates all new blocks from the snapshot onward. Wallets and keys are never touched.");
    rows += QStringLiteral("</small>");
    m_detail->setText(rows);

    if (availableBytes < neededBytes) {
        m_download->setEnabled(false);
        m_status->setText(tr("Not enough free disk space for the snapshot. Free up %1 GB and "
                             "restart, or sync from the network.").arg(Gb(neededBytes - availableBytes)));
        m_status->show();
    }

    const qint64 leftovers = SnapshotDownloader::StagingBytes(m_stagingDir);
    if (leftovers > 0 && m_download->isEnabled()) {
        m_status->setText(tr("An earlier download left %1 GB here, it will be resumed rather "
                             "than started over.").arg(Gb(leftovers)));
        m_status->show();
    }
}

void SnapshotDialog::startDownload()
{
    m_downloading = true;
    m_download->hide();
    m_decline->hide();
    m_cancel->show();
    m_cancel->setAutoDefault(false);
    m_cancel->setDefault(false);
    setFocus(); // keep stray space bar presses away from the cancel button
    m_bar->setRange(0, 1000);
    m_bar->show();
    m_status->setText(tr("Downloading..."));
    m_status->show();
    adjustSize(); // grow for the new widgets instead of clipping the fine print
    QMetaObject::invokeMethod(m_worker, "startDownload", Qt::QueuedConnection);
}

void SnapshotDialog::onProgress(qint64 done, qint64 total, int partsDone, int partCount)
{
    if (total > 0) m_bar->setValue(int(done * 1000 / total));

    // rate and eta, refreshed at most four times a second
    if (!m_rateTick.isValid()) {
        m_rateTick.start();
        m_rateLastDone = done;
        m_status->setText(tr("Downloading (%1 of %2 GB)").arg(Gb(done), Gb(total)));
        return;
    }
    const qint64 ms = m_rateTick.elapsed();
    if (ms < 250) return;
    m_rateTick.restart();

    const double instant = double(done - m_rateLastDone) * 1000.0 / double(ms);
    m_rateLastDone = done;
    // ignore the skip-verified bursts that are disk reads, not network
    if (instant >= 0) {
        m_bytesPerSec = m_bytesPerSec <= 0 ? instant : m_bytesPerSec * 0.8 + instant * 0.2;
    }

    QString line = tr("Downloading: %1 of %2 GB (%3 of %4 parts done, %5 MB/s)")
                       .arg(Gb(done), Gb(total))
                       .arg(partsDone).arg(partCount)
                       .arg(m_bytesPerSec / 1e6, 0, 'f', 1);
    if (m_bytesPerSec > 1e5) {
        const qint64 secsLeft = qint64(double(total - done) / m_bytesPerSec);
        line += QStringLiteral(", ");
        if (secsLeft >= 3600) {
            line += tr("about %1 h %2 min left").arg(secsLeft / 3600).arg((secsLeft % 3600) / 60);
        } else if (secsLeft >= 60) {
            line += tr("about %1 min left").arg(secsLeft / 60);
        } else {
            line += tr("under a minute left");
        }
    }
    m_status->setText(line);
}

void SnapshotDialog::onDownloadFinished()
{
    // everything verified on disk, unpack into the staging area
    m_cancel->setEnabled(true);
    m_bar->setRange(0, 0); // busy: decompressed size is not known up front
    m_status->setText(tr("All parts verified. Unpacking..."));
    QMetaObject::invokeMethod(m_worker, "extract", Qt::QueuedConnection,
                              Q_ARG(QString, m_extractDir));
}

void SnapshotDialog::onExtractProgress(qint64 bytesOut)
{
    m_status->setText(tr("Unpacking... %1 GB written").arg(Gb(bytesOut)));
}

void SnapshotDialog::onExtractFinished()
{
    m_downloading = false;
    m_cancel->setEnabled(false);
    m_status->setText(tr("Moving the blockchain into place..."));

    QString error;
    if (!applyExtracted(error)) {
        QMessageBox::critical(this, tr("Blockchain snapshot"),
            tr("Applying the snapshot failed: %1\n\nThe wallet will sync from the "
               "network instead.").arg(error));
        reject();
        return;
    }
    QMessageBox::information(this, tr("Blockchain snapshot"),
        tr("Snapshot applied. The wallet will now start and sync the remaining "
           "blocks from the network.")
        + QStringLiteral("\n\n")
        + tr("If the wallet then spends a while on \"Rescanning...\", let it finish. "
             "That is the wallet going through the chain to find your coins, it only "
             "happens once, and your funds are exactly where they were."));
    accept();
}

bool SnapshotDialog::applyExtracted(QString& error)
{
    const fs::path extracted = fs::path(m_extractDir.toStdString());

    // the builder always ships these three; indexes travels when present
    for (const char* required : {"blocks", "chainstate", "zerocoin"}) {
        if (!fs::exists(extracted / required)) {
            error = tr("the unpacked snapshot is missing its %1 folder").arg(required);
            return false;
        }
    }

    try {
        for (const char* name : {"blocks", "chainstate", "indexes", "zerocoin"}) {
            const fs::path from = extracted / name;
            const fs::path to = m_chainDir / name;
            if (!fs::exists(from)) continue;
            if (fs::exists(to)) fs::remove_all(to);
            // staging lives inside the chain dir, so this is a cheap rename
            fs::rename(from, to);
        }
    } catch (const fs::filesystem_error& e) {
        error = QString::fromStdString(e.what());
        return false;
    }

    SnapshotDownloader::PurgeStaging(m_stagingDir);
    return true;
}

void SnapshotDialog::cancelPressed()
{
    // a 25 GB download deserves a second question, especially since a stray
    // space bar or escape press lands here too
    const QMessageBox::StandardButton stop = QMessageBox::question(
        this, tr("Blockchain snapshot"),
        tr("Stop the download? Everything fetched so far stays on disk and the "
           "next launch offers to resume it."),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (stop != QMessageBox::Yes) return;

    m_cancel->setEnabled(false);
    m_status->setText(tr("Stopping..."));
    m_worker->requestCancel();
    QMetaObject::invokeMethod(m_worker, "cancel", Qt::QueuedConnection);
}

void SnapshotDialog::keyPressEvent(QKeyEvent* event)
{
    if (event->key() == Qt::Key_Escape && m_downloading) {
        cancelPressed();
        return;
    }
    QDialog::keyPressEvent(event);
}

void SnapshotDialog::closeEvent(QCloseEvent* event)
{
    if (m_downloading) {
        cancelPressed();
        event->ignore();
        return;
    }
    QDialog::closeEvent(event);
}

void SnapshotDialog::onFailed(const QString& reason)
{
    const bool wasDownloading = m_downloading;
    m_downloading = false;
    if (!wasDownloading) {
        // manifest stage, prepare() handles it silently
        return;
    }
    const qint64 leftovers = SnapshotDownloader::StagingBytes(m_stagingDir);
    QString text = tr("The snapshot download stopped: %1").arg(reason);
    if (leftovers > 0) {
        text += QStringLiteral("\n\n");
        text += tr("The %1 GB downloaded so far is kept, and the next launch offers to "
                   "resume it. The wallet will sync from the network in the meantime.")
                    .arg(Gb(leftovers));
    }
    QMessageBox::warning(this, tr("Blockchain snapshot"), text);
    reject();
}

void SnapshotDialog::askAboutLeftovers()
{
    const qint64 leftovers = SnapshotDownloader::StagingBytes(m_stagingDir);
    if (leftovers <= 0) return;
    const QMessageBox::StandardButton keep = QMessageBox::question(
        this, tr("Blockchain snapshot"),
        tr("An earlier snapshot download is using %1 GB of disk. Keep it so it can be "
           "resumed later?").arg(Gb(leftovers)),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes);
    if (keep == QMessageBox::No) {
        SnapshotDownloader::PurgeStaging(m_stagingDir);
    }
}
