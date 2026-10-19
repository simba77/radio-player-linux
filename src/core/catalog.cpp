#include "catalog.hpp"

#include "paths.hpp"
#include "storage.hpp"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>

#include <algorithm>
#include <utility>

using namespace Qt::StringLiterals;

namespace radio {

namespace catalog {

QStringList defaultCatalogUrls()
{
    return {
        u"https://raw.githubusercontent.com/simba77/radio-player-linux/main/data/catalogs/record.json"_s,
    };
}

namespace {

// Каталоги, встроенные в программу (ресурсы Qt). Порядок важен: при коллизии
// id побеждает первый.
const QStringList BUNDLED_CATALOGS{u":/catalogs/record.json"_s};

// Служебный файл в каталожном кэше — не каталог.
const QString ETAGS_FILE = u"etags.json"_s;

double now()
{
    return double(QDateTime::currentMSecsSinceEpoch()) / 1000.0;
}

QString cacheFileName(const QString &url)
{
    const QByteArray digest = QCryptographicHash::hash(url.toUtf8(), QCryptographicHash::Blake2s_128);
    return QString::fromLatin1(digest.toHex()) + u".json"_s;
}

void saveEtags(const QHash<QString, QString> &known)
{
    QJsonObject data;
    for (auto it = known.cbegin(); it != known.cend(); ++it)
        data.insert(it.key(), it.value());
    storage::writeJson(etagsPath(), data);
}

}  // namespace

QList<Catalog> loadBundled()
{
    QList<Catalog> catalogs;
    for (const QString &name : BUNDLED_CATALOGS) {
        QFile file(name);
        if (!file.open(QIODevice::ReadOnly))
            continue;
        const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
        if (const auto catalog = Catalog::fromJson(document.object()))
            catalogs.append(*catalog);
    }
    return catalogs;
}

QList<Catalog> loadCached(const QStringList &urls)
{
    QList<Catalog> catalogs;
    for (const QString &url : urls) {
        if (const auto catalog = Catalog::fromJson(storage::readJson(cachePath(url))))
            catalogs.append(*catalog);
    }
    return catalogs;
}

QList<Catalog> loadInitial(const QStringList &urls)
{
    pruneCache(urls);
    const QList<Catalog> cached = loadCached(urls);
    QList<Catalog> bundled = loadBundled();
    if (cached.isEmpty())
        return bundled;

    QList<Catalog> merged;
    for (const Catalog &catalog : cached) {
        const auto fresher = std::find_if(bundled.begin(), bundled.end(), [&](const Catalog &candidate) {
            return candidate.id == catalog.id;
        });
        if (fresher == bundled.end()) {
            merged.append(catalog);
            continue;
        }
        merged.append(fresher->revision > catalog.revision ? *fresher : catalog);
        bundled.erase(fresher);
    }
    merged.append(bundled);
    return merged;
}

std::optional<double> secondsSinceUpdate()
{
    const QJsonValue updatedAt = storage::loadState().value("catalogs_updated_at"_L1);
    if (!updatedAt.isDouble())
        return std::nullopt;
    return std::max(0.0, now() - updatedAt.toDouble());
}

void pruneCache(const QStringList &urls)
{
    QSet<QString> keep{ETAGS_FILE};
    for (const QString &url : urls)
        keep.insert(cacheFileName(url));

    const QDir dir(paths::catalogCacheDir());
    for (const QString &name : dir.entryList({u"*.json"_s}, QDir::Files)) {
        if (!keep.contains(name))
            QFile::remove(dir.filePath(name));
    }

    QHash<QString, QString> known = etags();
    bool stale = false;
    for (auto it = known.begin(); it != known.end();) {
        if (urls.contains(it.key())) {
            ++it;
        } else {
            it = known.erase(it);
            stale = true;
        }
    }
    if (stale)
        saveEtags(known);
}

QString cachePath(const QString &url)
{
    return paths::catalogCacheDir() + u'/' + cacheFileName(url);
}

QString etagsPath()
{
    return paths::catalogCacheDir() + u'/' + ETAGS_FILE;
}

QHash<QString, QString> etags()
{
    QHash<QString, QString> result;
    const QJsonObject data = storage::readJson(etagsPath()).toObject();
    for (auto it = data.constBegin(); it != data.constEnd(); ++it) {
        if (it.value().isString())
            result.insert(it.key(), it.value().toString());
    }
    return result;
}

}  // namespace catalog

// MARK: - CatalogUpdater

namespace {

// Таймаут загрузки одного каталога.
constexpr int TRANSFER_TIMEOUT_MS = 20'000;
constexpr int HTTP_NOT_MODIFIED = 304;

}  // namespace

CatalogUpdater::CatalogUpdater(const QStringList &extraUrls, const QStringList &builtinUrls, QObject *parent)
    : QObject(parent), builtinUrls_(builtinUrls), extraUrls_(extraUrls),
      network_(new QNetworkAccessManager(this))
{
}

void CatalogUpdater::setExtraUrls(const QStringList &urls)
{
    extraUrls_ = urls;
}

void CatalogUpdater::updateNow()
{
    const QStringList sources = urls();
    if (isRunning() || sources.isEmpty())
        return;
    pending_ = int(sources.size());
    changed_ = false;
    for (const QString &url : sources)
        fetch(url);
}

void CatalogUpdater::cancel()
{
    // Отсоединяемся до abort(): он сообщает о завершении синхронно, и обработчик
    // иначе засчитал бы прерванный цикл как обновление — следующий запуск
    // решил бы, что каталоги свежие, и не стал бы их проверять.
    const QSet<QNetworkReply *> replies = std::exchange(replies_, {});
    for (QNetworkReply *reply : replies) {
        reply->disconnect(this);
        reply->abort();
        reply->deleteLater();
    }
    pending_ = 0;
}

void CatalogUpdater::fetch(const QString &url)
{
    QNetworkRequest request{QUrl(url)};
    request.setTransferTimeout(TRANSFER_TIMEOUT_MS);
    // ETag отправляется, только пока жив файл кэша: на 304 сервер не пришлёт тела,
    // и удалённый кэш иначе не восстановился бы, пока каталог не изменится.
    const QString etag = catalog::etags().value(url);
    if (!etag.isEmpty() && QFile::exists(catalog::cachePath(url)))
        request.setRawHeader("If-None-Match", etag.toUtf8());
    QNetworkReply *reply = network_->get(request);
    replies_.insert(reply);
    connect(reply, &QNetworkReply::finished, this, [this, url, reply] { handleReply(url, reply); });
}

void CatalogUpdater::handleReply(const QString &url, QNetworkReply *reply)
{
    replies_.remove(reply);
    reply->deleteLater();
    if (reply->error() == QNetworkReply::NoError)
        store(url, reply);
    if (--pending_ <= 0) {
        pending_ = 0;
        finish();
    }
}

void CatalogUpdater::store(const QString &url, QNetworkReply *reply)
{
    if (reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() == HTTP_NOT_MODIFIED)
        return;  // Каталог не менялся.

    QJsonParseError error{};
    const QJsonDocument document = QJsonDocument::fromJson(reply->readAll(), &error);
    // Битый или слишком новый каталог: остаёмся на прежней копии.
    if (error.error != QJsonParseError::NoError || !Catalog::fromJson(document.object()))
        return;
    if (!storage::writeJson(catalog::cachePath(url), document.object()))
        return;
    changed_ = true;

    QHash<QString, QString> known = catalog::etags();
    const QString etag = QString::fromUtf8(reply->rawHeader("ETag"));
    if (etag.isEmpty())
        known.remove(url);
    else
        known.insert(url, etag);
    catalog::saveEtags(known);
}

void CatalogUpdater::finish()
{
    storage::updateState({{"catalogs_updated_at"_L1, catalog::now()}});
    emit finished(catalog::loadInitial(urls()), changed_);
}

}  // namespace radio
