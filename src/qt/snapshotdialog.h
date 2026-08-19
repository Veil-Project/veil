// Copyright (c) 2026 The Veil developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef VEIL_QT_SNAPSHOTDIALOG_H
#define VEIL_QT_SNAPSHOTDIALOG_H

#include <fs.h>

#include <QDialog>
#include <QElapsedTimer>

class SnapshotDownloader;
class QLabel;
class QProgressBar;
class QPushButton;
class QThread;

//! Offers a verified blockchain snapshot instead of a full sync, when the
//! chain data is missing or weeks stale. Runs before the node opens any
//! database, so applying it is plain folder work and startup just continues
//! afterwards.
//!
//! Every path out of here leaves the wallet able to sync normally: decline,
//! cancel, network failure, disk failure. The snapshot is an accelerator,
//! never a dependency.
class SnapshotDialog : public QDialog
{
    Q_OBJECT

public:
    //! Called from startup. Decides quietly whether an offer makes sense
    //! (chain missing or stale, manifest reachable) and shows the dialog
    //! only then. Never blocks startup for long without showing UI.
    static void MaybeOffer(QWidget* parent = nullptr);

    ~SnapshotDialog();

protected:
    //! Escape and the window close button must not silently kill a
    //! running download, they route through the same confirmation.
    void keyPressEvent(QKeyEvent* event) override;
    void closeEvent(QCloseEvent* event) override;

private:
    SnapshotDialog(const QString& chain, const fs::path& chainDir, QWidget* parent);

    //! Fetches the manifest with a bounded wait. True means show the offer.
    bool prepare();
    bool applyExtracted(QString& error);
    void askAboutLeftovers();

private Q_SLOTS:
    void onManifestReady(int height, qint64 compressedBytes, qint64 neededBytes, qint64 availableBytes);
    void onProgress(qint64 done, qint64 total, int partsDone, int partCount);
    void onDownloadFinished();
    void onExtractProgress(qint64 bytesOut);
    void onExtractFinished();
    void onFailed(const QString& reason);
    void startDownload();
    void cancelPressed();

private:
    QString m_chain;
    fs::path m_chainDir;
    QString m_stagingDir;
    QString m_extractDir;

    QThread* m_thread = nullptr;
    SnapshotDownloader* m_worker = nullptr;

    QLabel* m_headline = nullptr;
    QLabel* m_detail = nullptr;
    QLabel* m_status = nullptr;
    QProgressBar* m_bar = nullptr;
    QPushButton* m_download = nullptr;
    QPushButton* m_decline = nullptr;
    QPushButton* m_cancel = nullptr;

    bool m_manifestOk = false;
    bool m_downloading = false;
    qint64 m_compressedBytes = 0;

    // download rate bookkeeping, smoothed so the label does not flicker
    QElapsedTimer m_rateTick;
    qint64 m_rateLastDone = 0;
    double m_bytesPerSec = 0;
};

#endif // VEIL_QT_SNAPSHOTDIALOG_H
