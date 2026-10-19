#include "artwork.hpp"

#include "paths.hpp"

#include <QCryptographicHash>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>
#include <QUrl>

#include <algorithm>
#include <cstdio>
#include <utility>

using namespace Qt::StringLiterals;

namespace radio {

namespace {

// Обложки крупнее не нужны: 300×300 — максимум, что просит интерфейс.
constexpr qint64 MAX_BYTES = 2 * 1024 * 1024;
// Уборка после запуска, а не во время: старт важнее.
constexpr int PRUNE_DELAY_MS = 5000;
constexpr int TRANSFER_TIMEOUT_MS = 15'000;

}  // namespace

int pruneArtworkCache(const QString &directory, qint64 maxBytes)
{
    QFileInfoList entries = QDir(directory).entryInfoList(QDir::Files);
    qint64 total = 0;
    for (const QFileInfo &entry : entries)
        total += entry.size();
    if (total <= maxBytes)
        return 0;

    // Сначала самые старые: недавние обложки ещё нужны — их показывает окно
    // и отдаёт MPRIS.
    std::sort(entries.begin(), entries.end(), [](const QFileInfo &left, const QFileInfo &right) {
        return left.lastModified() < right.lastModified();
    });
    int removed = 0;
    for (const QFileInfo &entry : std::as_const(entries)) {
        if (total <= maxBytes)
            break;
        if (QFile::remove(entry.filePath())) {
            total -= entry.size();
            ++removed;
        }
    }
    return removed;
}

ArtworkLoader::ArtworkLoader(QObject *parent) : QObject(parent), network_(new QNetworkAccessManager(this))
{
    QTimer::singleShot(PRUNE_DELAY_MS, this, [] {
        if (const int removed = pruneArtworkCache(paths::artworkCacheDir(), MAX_ARTWORK_CACHE_BYTES))
            qDebug().noquote() << u"Кэш обложек: удалено файлов —"_s << removed;
    });
}

QString ArtworkLoader::cachePath(const QString &url)
{
    // Хеш здесь — способ превратить адрес в имя файла, а не защита:
    // подделывать его некому, коллизия стоит одной перерисованной обложки.
    const QByteArray digest = QCryptographicHash::hash(url.toUtf8(), QCryptographicHash::Sha1).toHex();
    const QString suffix = url.section(u'?', 0, 0).endsWith(".webp"_L1) ? u".webp"_s : u".jpg"_s;
    return paths::artworkCacheDir() + u'/' + QString::fromLatin1(digest) + suffix;
}

void ArtworkLoader::load(const QString &url)
{
    if (url.isEmpty() || pending_.contains(url))
        return;

    const QString cached = cachePath(url);
    if (QFile::exists(cached)) {
        const QImage image(cached);
        if (!image.isNull()) {
            emit artworkReady(url, cached, image);
            return;
        }
        QFile::remove(cached);
    }

    QNetworkRequest request{QUrl(url)};
    request.setTransferTimeout(TRANSFER_TIMEOUT_MS);
    QNetworkReply *reply = network_->get(request);
    pending_.insert(url, reply);
    connect(reply, &QNetworkReply::finished, this, [this, url, reply] { handleReply(url, reply); });
}

void ArtworkLoader::cancelAll()
{
    for (QNetworkReply *reply : std::exchange(pending_, {})) {
        reply->disconnect(this);
        reply->abort();
        reply->deleteLater();
    }
}

void ArtworkLoader::handleReply(const QString &url, QNetworkReply *reply)
{
    pending_.remove(url);
    reply->deleteLater();
    if (reply->error() != QNetworkReply::NoError)
        return;

    const QByteArray data = reply->readAll();
    if (data.isEmpty() || data.size() > MAX_BYTES)
        return;
    QImage image;
    if (!image.loadFromData(data))
        return;

    // Пишем через временный файл: недокачанная обложка не должна попасть в кэш.
    const QString path = cachePath(url);
    const QString part = path + u".part"_s;
    QFile file(part);
    const bool written = QDir().mkpath(QFileInfo(path).absolutePath()) && file.open(QIODevice::WriteOnly) &&
                         file.write(data) == data.size();
    file.close();
    if (!written ||
        std::rename(QFile::encodeName(part).constData(), QFile::encodeName(path).constData()) != 0) {
        // Кэш не критичен — отдаём картинку из памяти.
        QFile::remove(part);
        emit artworkReady(url, QString(), image);
        return;
    }
    emit artworkReady(url, path, image);
}

}  // namespace radio
