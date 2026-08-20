// Copyright (c) 2026 The Veil developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef VEIL_QT_SNAPSHOTDOWNLOADER_H
#define VEIL_QT_SNAPSHOTDOWNLOADER_H

#include <QHash>
#include <QObject>
#include <QString>
#include <QUrl>
#include <QVector>
#include <QElapsedTimer>

#include <crypto/sha256.h>

#include <atomic>
#include <memory>

class QNetworkAccessManager;
class QNetworkReply;
class QFile;

//! Downloads a published Veil snapshot into a staging directory and leaves it
//! fully verified on disk, ready for extraction.
//!
//! Runs on its own thread (moveToThread before calling anything). The
//! pipeline is: fetchManifest() -> manifestReady, then on acceptance
//! startDownload() -> progress -> downloadFinished, then
//! extract(dest) -> extractProgress -> extractFinished.
//!
//! Parts download several at a time, which is where download managers get
//! their speed: single HTTP streams are frequently throttled per connection,
//! so a handful in parallel fills the pipe. Every part is verified against
//! the manifest's sha256 before it counts, with every byte fed through the
//! hasher as it arrives. Partial files resume with a Range request, the
//! already present prefix re-hashed from disk first. Any failure keeps the
//! partials, a later run picks up where this one stopped.
class SnapshotDownloader : public QObject
{
    Q_OBJECT

public:
    //! stagingDir: where parts land, created if missing.
    //! chain: "main" or "test", decides which release is fetched.
    explicit SnapshotDownloader(const QString& stagingDir, const QString& chain, QObject* parent = nullptr);
    ~SnapshotDownloader();

    //! Bytes of free disk needed to finish downloading and extract. What is
    //! already verified or partial in staging does not need downloading
    //! again, so a resumed setup near the end needs far less than a fresh
    //! one, and the check must not turn away a download that is basically
    //! done. Extraction itself wants roughly 1.2x the compressed size.
    static qint64 NeededBytes(qint64 compressedBytes, qint64 alreadyStagedBytes)
    {
        const qint64 toFetch = qMax<qint64>(0, compressedBytes - alreadyStagedBytes);
        return toFetch + compressedBytes * 12 / 10;
    }

    //! Bytes currently sitting in a staging directory, for telling the user
    //! how much a declined download is still occupying.
    static qint64 StagingBytes(const QString& stagingDir);

    //! Removes a staging directory entirely. Resume is impossible afterwards.
    static void PurgeStaging(const QString& stagingDir);

    //! Safe from any thread: makes a blocking extract stop at its next
    //! progress callback. The cancel() slot handles the network side.
    void requestCancel() { m_cancelled = true; }

public Q_SLOTS:
    void fetchManifest();
    void startDownload();
    void extract(const QString& destDir);
    void cancel();

Q_SIGNALS:
    //! Manifest fetched and parsed. availableBytes is free disk on the
    //! staging volume, neededBytes what the whole operation wants.
    void manifestReady(int height, qint64 compressedBytes, qint64 neededBytes, qint64 availableBytes);
    void progress(qint64 doneBytes, qint64 totalBytes, int partsDone, int partCount);
    void downloadFinished();
    //! totalBytes is 0 when the manifest predates uncompressed_bytes.
    void extractProgress(qint64 bytesOut, qint64 totalBytes);
    void extractFinished();
    void failed(const QString& reason);

private Q_SLOTS:
    void onManifestReply();
    void onTagListReply();

private:
    //! Per in-flight part download.
    struct Active
    {
        int index = -1;
        QFile* file = nullptr;
        std::unique_ptr<CSHA256> hasher;
        bool sawFirstData = false;
        ~Active();
    };

    void requestManifestFromBase();
    void fillPool();
    void startPart(int index);
    void onPartReadyRead(QNetworkReply* reply);
    void onPartFinished(QNetworkReply* reply);
    void failAll(const QString& reason);
    void emitProgress(bool force);
    bool hashExistingPrefix(const QString& path, qint64 bytes, CSHA256& hasher);
    QString hashFileHex(const QString& path);
    QNetworkReply* get(const QUrl& url, qint64 rangeFrom = -1);

    struct Part
    {
        QString file;
        qint64 bytes = 0;
        QString sha256; // lower case hex
    };

    QString m_stagingDir;
    QString m_chain;
    QString m_baseUrl;      // release download base, set once the tag is known
    QVector<Part> m_parts;
    int m_height = 0;
    qint64 m_totalBytes = 0;
    qint64 m_uncompressedBytes = 0; // 0 if the manifest does not say
    qint64 m_doneBytes = 0; // bytes of verified or in-flight progress

    QHash<QNetworkReply*, Active*> m_active;
    int m_nextPart = 0;
    int m_partsDone = 0;
    bool m_failing = false;
    QElapsedTimer m_progressTick;

    QNetworkReply* m_reply = nullptr; // manifest and tag lookup only
    QNetworkAccessManager* m_net = nullptr;
    std::atomic<bool> m_cancelled{false};
};

#endif // VEIL_QT_SNAPSHOTDOWNLOADER_H
