// Copyright (c) 2026 The Veil developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qt/snapshotdownloader.h>

#include <veil/snapshot/snapshot_extract.h>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QStorageInfo>

//! Hardcoded on purpose: this is the project's snapshot repo, and the
//! whole point is that the wallet only ever offers snapshots from it.
static const char* SNAPSHOT_REPO = "Veil-Project/veil-snapshots";
static const char* USER_AGENT = "VeilCore-SnapshotDownloader/1.0";

//! How many parts download at once. This is where download managers get
//! their speed: hosts commonly throttle per connection, so a few in
//! parallel fill the pipe. Kept under Qt's six-per-host connection cap.
static const int MAX_ACTIVE_PARTS = 6;

namespace {

QString ToHex(const unsigned char* hash)
{
    static const char* hexmap = "0123456789abcdef";
    QString out;
    out.reserve(CSHA256::OUTPUT_SIZE * 2);
    for (size_t i = 0; i < CSHA256::OUTPUT_SIZE; i++) {
        out += hexmap[hash[i] >> 4];
        out += hexmap[hash[i] & 15];
    }
    return out;
}

} // namespace

SnapshotDownloader::Active::~Active()
{
    if (file) {
        file->flush();
        delete file;
    }
}

SnapshotDownloader::SnapshotDownloader(const QString& stagingDir, const QString& chain, QObject* parent)
    : QObject(parent), m_stagingDir(stagingDir), m_chain(chain)
{
}

SnapshotDownloader::~SnapshotDownloader()
{
    for (auto it = m_active.begin(); it != m_active.end(); ++it) {
        it.key()->abort();
        delete it.value();
    }
    m_active.clear();
    if (m_reply) m_reply->abort();
}

QNetworkReply* SnapshotDownloader::get(const QUrl& url, qint64 rangeFrom)
{
    if (!m_net) m_net = new QNetworkAccessManager(this);
    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::UserAgentHeader, USER_AGENT);
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    if (rangeFrom > 0) {
        req.setRawHeader("Range", QStringLiteral("bytes=%1-").arg(rangeFrom).toLatin1());
    }
    return m_net->get(req);
}

void SnapshotDownloader::cancel()
{
    m_cancelled = true;
    if (!m_active.isEmpty()) {
        failAll(tr("cancelled"));
    }
}

qint64 SnapshotDownloader::StagingBytes(const QString& stagingDir)
{
    qint64 total = 0;
    QDir dir(stagingDir);
    for (const QFileInfo& fi : dir.entryInfoList(QDir::Files)) {
        total += fi.size();
    }
    return total;
}

void SnapshotDownloader::PurgeStaging(const QString& stagingDir)
{
    QDir(stagingDir).removeRecursively();
}

// ---- manifest -----------------------------------------------------------

void SnapshotDownloader::fetchManifest()
{
    // test hook: point the whole pipeline at any release base URL, used by
    // the harness to prove the flow against a tiny fixture release instead
    // of tens of gigabytes. Not a user knob, it is read once at startup
    // from the environment and never persisted.
    const QByteArray urlOverride = qgetenv("VEIL_SNAPSHOT_BASE_URL");
    if (!urlOverride.isEmpty()) {
        m_baseUrl = QString::fromUtf8(urlOverride);
        requestManifestFromBase();
        return;
    }

    if (m_chain == "main") {
        m_baseUrl = QStringLiteral("https://github.com/%1/releases/latest/download").arg(SNAPSHOT_REPO);
        requestManifestFromBase();
        return;
    }
    // testnet releases are never "latest" on GitHub, look the tag up by name
    QUrl url(QStringLiteral("https://api.github.com/repos/%1/releases?per_page=100").arg(SNAPSHOT_REPO));
    m_reply = get(url);
    connect(m_reply, &QNetworkReply::finished, this, &SnapshotDownloader::onTagListReply);
}

void SnapshotDownloader::onTagListReply()
{
    QNetworkReply* reply = m_reply;
    m_reply = nullptr;
    reply->deleteLater();
    if (reply->error() != QNetworkReply::NoError) {
        Q_EMIT failed(tr("could not reach GitHub to look up the snapshot: %1").arg(reply->errorString()));
        return;
    }
    QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
    QString tag;
    for (const QJsonValue& v : doc.array()) {
        QString t = v.toObject().value("tag_name").toString();
        if (t.startsWith("testnet-")) {
            tag = t;
            break;
        }
    }
    if (tag.isEmpty()) {
        Q_EMIT failed(tr("no testnet snapshot has been published yet"));
        return;
    }
    m_baseUrl = QStringLiteral("https://github.com/%1/releases/download/%2").arg(SNAPSHOT_REPO, tag);
    requestManifestFromBase();
}

void SnapshotDownloader::requestManifestFromBase()
{
    m_reply = get(QUrl(m_baseUrl + "/manifest.json"));
    connect(m_reply, &QNetworkReply::finished, this, &SnapshotDownloader::onManifestReply);
}

void SnapshotDownloader::onManifestReply()
{
    QNetworkReply* reply = m_reply;
    m_reply = nullptr;
    reply->deleteLater();
    if (reply->error() != QNetworkReply::NoError) {
        Q_EMIT failed(tr("could not fetch the snapshot manifest: %1").arg(reply->errorString()));
        return;
    }
    QJsonObject manifest = QJsonDocument::fromJson(reply->readAll()).object();

    m_height = manifest.value("height").toInt();
    m_totalBytes = qint64(manifest.value("compressed_bytes").toDouble());
    // added to manifests later, so older releases simply do not have it
    m_uncompressedBytes = qint64(manifest.value("uncompressed_bytes").toDouble());
    m_parts.clear();
    for (const QJsonValue& v : manifest.value("parts").toArray()) {
        QJsonObject o = v.toObject();
        Part p;
        p.file = o.value("file").toString();
        p.bytes = qint64(o.value("bytes").toDouble());
        p.sha256 = o.value("sha256").toString().toLower();
        // a manifest entry with anything missing means a broken or hostile
        // manifest, and nothing downloaded from it could ever verify
        if (p.file.isEmpty() || p.file.contains('/') || p.bytes <= 0 || p.sha256.size() != 64) {
            Q_EMIT failed(tr("the snapshot manifest is malformed"));
            return;
        }
        m_parts.push_back(p);
    }
    if (m_parts.isEmpty() || m_totalBytes <= 0) {
        Q_EMIT failed(tr("the snapshot manifest is malformed"));
        return;
    }

    QDir().mkpath(m_stagingDir);
    QStorageInfo storage(m_stagingDir);
    Q_EMIT manifestReady(m_height, m_totalBytes,
                         NeededBytes(m_totalBytes, StagingBytes(m_stagingDir)),
                         storage.bytesAvailable());
}

// ---- parts --------------------------------------------------------------

void SnapshotDownloader::startDownload()
{
    if (m_parts.isEmpty()) {
        Q_EMIT failed(tr("no manifest loaded"));
        return;
    }
    if (!m_active.isEmpty()) {
        // a second start while downloads run would reset the counters under
        // live replies and double count everything they finish
        return;
    }
    QStorageInfo storage(m_stagingDir);
    const qint64 needed = NeededBytes(m_totalBytes, StagingBytes(m_stagingDir));
    if (storage.bytesAvailable() < needed) {
        Q_EMIT failed(tr("not enough free disk space: this needs about %1 GB free")
                          .arg(needed / 1000000000));
        return;
    }
    m_doneBytes = 0;
    m_nextPart = 0;
    m_partsDone = 0;
    m_failing = false;
    m_progressTick.start();
    fillPool();
}

void SnapshotDownloader::fillPool()
{
    while (!m_cancelled && !m_failing
           && m_active.size() < MAX_ACTIVE_PARTS && m_nextPart < m_parts.size()) {
        startPart(m_nextPart++);
        if (m_failing) return; // startPart can fail the whole run
    }
    if (m_active.isEmpty() && !m_failing && m_partsDone >= m_parts.size()) {
        emitProgress(true);
        Q_EMIT downloadFinished();
    }
}

void SnapshotDownloader::startPart(int index)
{
    const Part& part = m_parts[index];
    const QString path = m_stagingDir + "/" + part.file;
    qint64 have = QFile::exists(path) ? QFileInfo(path).size() : 0;

    if (have == part.bytes) {
        // possibly complete from an earlier run, trust it only if it hashes
        if (hashFileHex(path) == part.sha256) {
            m_doneBytes += part.bytes;
            m_partsDone++;
            emitProgress(true);
            return;
        }
        QFile::remove(path);
        have = 0;
    } else if (have > part.bytes) {
        // bigger than the manifest says it should be, only garbage does that
        QFile::remove(path);
        have = 0;
    }

    std::unique_ptr<Active> ctx(new Active());
    ctx->index = index;
    ctx->hasher.reset(new CSHA256());
    if (have > 0) {
        if (!hashExistingPrefix(path, have, *ctx->hasher)) {
            // unreadable partial, start the part over
            QFile::remove(path);
            have = 0;
            ctx->hasher.reset(new CSHA256());
        }
    }

    ctx->file = new QFile(path);
    if (!ctx->file->open(have > 0 ? QIODevice::Append : QIODevice::WriteOnly)) {
        failAll(tr("cannot write to %1").arg(path));
        return;
    }
    m_doneBytes += have;

    QNetworkReply* reply = get(QUrl(m_baseUrl + "/" + part.file), have);
    m_active[reply] = ctx.release();
    connect(reply, &QNetworkReply::readyRead, this, [this, reply]() { onPartReadyRead(reply); });
    connect(reply, &QNetworkReply::finished, this, [this, reply]() { onPartFinished(reply); });
}

void SnapshotDownloader::onPartReadyRead(QNetworkReply* reply)
{
    Active* ctx = m_active.value(reply);
    if (!ctx || m_failing) return;

    // if we asked for a Range and the server answered 200 instead of 206 it
    // sent the whole file, so the partial prefix has to be thrown away
    if (!ctx->sawFirstData) {
        ctx->sawFirstData = true;
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (status == 200 && ctx->file->size() > 0) {
            m_doneBytes -= ctx->file->size();
            ctx->file->close();
            ctx->file->remove();
            if (!ctx->file->open(QIODevice::WriteOnly)) {
                failAll(tr("cannot rewrite %1").arg(ctx->file->fileName()));
                return;
            }
            ctx->hasher.reset(new CSHA256());
        }
    }

    const QByteArray data = reply->readAll();
    if (data.isEmpty()) return;
    ctx->hasher->Write(reinterpret_cast<const unsigned char*>(data.constData()), size_t(data.size()));
    if (ctx->file->write(data) != data.size()) {
        failAll(tr("disk write failed on %1").arg(ctx->file->fileName()));
        return;
    }
    m_doneBytes += data.size();
    emitProgress(false);
}

void SnapshotDownloader::onPartFinished(QNetworkReply* reply)
{
    Active* ctx = m_active.take(reply);
    reply->deleteLater();
    if (!ctx) return;
    std::unique_ptr<Active> cleanup(ctx);

    if (m_failing) return; // an abort during shutdown of the pool

    ctx->file->flush();
    ctx->file->close();

    if (reply->error() != QNetworkReply::NoError) {
        // partial stays on disk for resume, a rerun picks it up
        failAll(m_cancelled ? tr("cancelled")
                            : tr("download interrupted on %1: %2, run this again to resume")
                                  .arg(m_parts[ctx->index].file, reply->errorString()));
        return;
    }

    const Part& part = m_parts[ctx->index];
    unsigned char digest[CSHA256::OUTPUT_SIZE];
    ctx->hasher->Finalize(digest);
    if (ToHex(digest) != part.sha256) {
        // damaged in transit beyond resuming, only a fresh copy can fix it
        QFile::remove(m_stagingDir + "/" + part.file);
        failAll(tr("%1 failed checksum verification, deleted it, run this again").arg(part.file));
        return;
    }

    m_partsDone++;
    emitProgress(true);
    fillPool();
}

void SnapshotDownloader::failAll(const QString& reason)
{
    if (m_failing) return;
    m_failing = true;
    const QList<QNetworkReply*> replies = m_active.keys();
    for (QNetworkReply* r : replies) {
        r->abort(); // handlers see m_failing and only clean up
    }
    // anything abort left behind
    for (auto it = m_active.begin(); it != m_active.end(); ++it) {
        it.key()->deleteLater();
        delete it.value();
    }
    m_active.clear();
    Q_EMIT failed(reason);
}

void SnapshotDownloader::emitProgress(bool force)
{
    if (!force && m_progressTick.isValid() && m_progressTick.elapsed() < 100) return;
    m_progressTick.restart();
    Q_EMIT progress(m_doneBytes, m_totalBytes, m_partsDone, m_parts.size());
}

// ---- helpers ------------------------------------------------------------

bool SnapshotDownloader::hashExistingPrefix(const QString& path, qint64 bytes, CSHA256& hasher)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return false;
    qint64 left = bytes;
    QByteArray buf;
    while (left > 0) {
        buf = f.read(qMin<qint64>(left, 1 << 20));
        if (buf.isEmpty()) return false;
        hasher.Write(reinterpret_cast<const unsigned char*>(buf.constData()), size_t(buf.size()));
        left -= buf.size();
    }
    return true;
}

QString SnapshotDownloader::hashFileHex(const QString& path)
{
    CSHA256 hasher;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return QString();
    while (!f.atEnd()) {
        QByteArray buf = f.read(1 << 20);
        hasher.Write(reinterpret_cast<const unsigned char*>(buf.constData()), size_t(buf.size()));
    }
    unsigned char digest[CSHA256::OUTPUT_SIZE];
    hasher.Finalize(digest);
    return ToHex(digest);
}

// ---- extraction ---------------------------------------------------------

void SnapshotDownloader::extract(const QString& destDir)
{
    // the chain data about to replace the user's is only as good as what is
    // on disk right now, so re-verify every part here, independently of any
    // bookkeeping the download phase did. ~30 seconds for a mainnet
    // snapshot, and it makes counter bugs cosmetic instead of dangerous.
    for (const Part& p : m_parts) {
        const QString path = m_stagingDir + "/" + p.file;
        if (hashFileHex(path) != p.sha256) {
            QFile::remove(path);
            Q_EMIT failed(tr("%1 is damaged on disk, deleted it, run this again to refetch it").arg(p.file));
            return;
        }
    }

    std::vector<fs::path> parts;
    for (const Part& p : m_parts) {
        parts.push_back(fs::path((m_stagingDir + "/" + p.file).toStdString()));
    }
    std::string error;
    bool ok = snapshot::ExtractTarZst(
        parts, fs::path(destDir.toStdString()),
        {"blocks", "chainstate", "indexes", "zerocoin"}, error,
        [this](uint64_t bytesOut) {
            Q_EMIT extractProgress(qint64(bytesOut), m_uncompressedBytes);
            return !m_cancelled.load();
        });
    if (!ok) {
        Q_EMIT failed(tr("extraction failed: %1").arg(QString::fromStdString(error)));
        return;
    }
    Q_EMIT extractFinished();
}
