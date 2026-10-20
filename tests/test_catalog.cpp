// Каталоги: встроенная копия, кэш и обновление по сети.
//
// Сеть настоящая — локальный HTTP-сервер: ETag, 304 и битый ответ проходят
// тот же путь, что и в работе.

#include "catalog.hpp"
#include "http_server.hpp"
#include "paths.hpp"
#include "storage.hpp"
#include "support.hpp"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSignalSpy>

#include <catch2/catch_test_macros.hpp>

using namespace radio;
using namespace Qt::StringLiterals;

namespace {

const QString BUNDLED_URL = catalog::defaultCatalogUrls().first();
const QString EXTRA_URL = u"https://example.org/catalog.json"_s;
const QString THIRD_URL = u"https://example.net/catalog.json"_s;

QJsonObject catalogJson(const QString &id, const QString &name, int revision = 3)
{
    return {
        {"schema"_L1, 1},
        {"id"_L1, id},
        {"name"_L1, name},
        {"revision"_L1, revision},
        {"stations"_L1, QJsonArray{QJsonObject{{"id"_L1, id + u"-main"_s},
                                               {"name"_L1, "Main"_L1},
                                               {"url"_L1, "https://example.org/main.m3u8"_L1}}}},
    };
}

void putInCache(const QString &url, const QJsonObject &data)
{
    test::writeFile(catalog::cachePath(url), QJsonDocument(data).toJson());
}

QStringList catalogIds(const QList<Catalog> &catalogs)
{
    QStringList out;
    for (const Catalog &catalog : catalogs)
        out.append(catalog.id);
    return out;
}

QStringList catalogNames(const QList<Catalog> &catalogs)
{
    QStringList out;
    for (const Catalog &catalog : catalogs)
        out.append(catalog.name);
    return out;
}

}  // namespace

// MARK: - Встроенная копия и кэш

TEST_CASE("the bundled catalog is valid")
{
    // Встроенный каталог обязан парситься — иначе приложение стартует пустым.
    const QList<Catalog> catalogs = catalog::loadBundled();
    REQUIRE(catalogs.size() == 1);
    CHECK(catalogs[0].id == "record"_L1);

    // Ни одна станция не отсеяна проверками: опечатка в каталоге молча
    // отняла бы её у всех пользователей.
    QFile file(u":/catalogs/record.json"_s);
    REQUIRE(file.open(QIODevice::ReadOnly));
    const QJsonArray declared = QJsonDocument::fromJson(file.readAll())["stations"_L1].toArray();
    CHECK_FALSE(declared.isEmpty());
    CHECK(catalogs[0].stations.size() == declared.size());
}

TEST_CASE("load initial falls back to bundled without a cache")
{
    CHECK(catalogIds(catalog::loadInitial()) == QStringList{u"record"_s});
}

TEST_CASE("load initial prefers the cache over bundled")
{
    putInCache(BUNDLED_URL, catalogJson(u"record"_s, u"Из кэша"_s, 99));
    CHECK(catalogNames(catalog::loadInitial()) == QStringList{u"Из кэша"_s});
}

TEST_CASE("bundled wins when it is newer than the cache")
{
    // После обновления приложения встроенная копия может обогнать кэш.
    putInCache(BUNDLED_URL, catalogJson(u"record"_s, u"Устаревший кэш"_s, 0));
    CHECK(catalogNames(catalog::loadInitial()) == QStringList{u"Radio Record"_s});
}

TEST_CASE("the cache of a removed catalog is ignored")
{
    // Каталог, убранный из настроек, больше не поставляет станции.
    putInCache(EXTRA_URL, catalogJson(u"extra"_s, u"Лишний"_s));
    CHECK(catalogIds(catalog::loadInitial({BUNDLED_URL, EXTRA_URL})).contains(u"extra"_s));
    CHECK_FALSE(catalogIds(catalog::loadInitial({BUNDLED_URL})).contains(u"extra"_s));
}

TEST_CASE("the cache of a removed catalog is deleted")
{
    putInCache(EXTRA_URL, catalogJson(u"extra"_s, u"Лишний"_s));
    catalog::loadInitial({BUNDLED_URL});
    CHECK_FALSE(QFile::exists(catalog::cachePath(EXTRA_URL)));
}

TEST_CASE("pruning keeps the etags file")
{
    storage::writeJson(catalog::etagsPath(), QJsonObject{{BUNDLED_URL, u"\"tag\""_s}});
    catalog::pruneCache({BUNDLED_URL});
    CHECK(catalog::etags() == QHash<QString, QString>{{BUNDLED_URL, u"\"tag\""_s}});
}

TEST_CASE("pruning forgets the etags of removed catalogs")
{
    storage::writeJson(catalog::etagsPath(), QJsonObject{{BUNDLED_URL, u"\"a\""_s}, {EXTRA_URL, u"\"b\""_s}});
    catalog::pruneCache({BUNDLED_URL});
    CHECK(catalog::etags() == QHash<QString, QString>{{BUNDLED_URL, u"\"a\""_s}});
}

TEST_CASE("cached catalogs follow the order of sources")
{
    // Порядок источников решает, кто победит при совпадении id станций.
    putInCache(BUNDLED_URL, catalogJson(u"record"_s, u"Первый"_s));
    putInCache(EXTRA_URL, catalogJson(u"extra"_s, u"Второй"_s));
    CHECK(catalogNames(catalog::loadCached({BUNDLED_URL, EXTRA_URL})) ==
          QStringList{u"Первый"_s, u"Второй"_s});
    CHECK(catalogNames(catalog::loadCached({EXTRA_URL, BUNDLED_URL})) ==
          QStringList{u"Второй"_s, u"Первый"_s});
}

TEST_CASE("time since the last update is unknown until the first one")
{
    CHECK_FALSE(catalog::secondsSinceUpdate());
    storage::updateState({{"catalogs_updated_at"_L1, 1.0}});
    CHECK(catalog::secondsSinceUpdate().value_or(0) > 0);
}

// MARK: - Список источников

TEST_CASE("bundled urls are used without extras")
{
    CHECK(CatalogUpdater().urls() == catalog::defaultCatalogUrls());
}

TEST_CASE("extra urls go after the bundled ones")
{
    CHECK(CatalogUpdater({EXTRA_URL}).urls() == catalog::defaultCatalogUrls() + QStringList{EXTRA_URL});
}

TEST_CASE("an added catalog is picked up without a restart")
{
    CatalogUpdater updater;
    updater.setExtraUrls({EXTRA_URL});
    CHECK(updater.urls().contains(EXTRA_URL));
}

TEST_CASE("a removed catalog is no longer fetched")
{
    CatalogUpdater updater({EXTRA_URL, THIRD_URL});
    updater.setExtraUrls({THIRD_URL});
    CHECK_FALSE(updater.urls().contains(EXTRA_URL));
    CHECK(updater.urls().contains(THIRD_URL));
}

// MARK: - Загрузка по сети

TEST_CASE("a downloaded catalog is cached with its etag")
{
    test::HttpServer server;
    const QString url = server.url(u"/catalog.json"_s);
    server.respond(u"/catalog.json"_s,
                   {200, QJsonDocument(catalogJson(u"net"_s, u"Из сети"_s)).toJson(), {{"ETag", "\"v1\""}}});

    CatalogUpdater updater({}, {url});
    QSignalSpy finished(&updater, &CatalogUpdater::finished);
    updater.updateNow();
    CHECK(updater.isRunning());
    REQUIRE(finished.wait(5000));

    CHECK(finished[0][1].toBool());
    CHECK(catalogNames(finished[0][0].value<QList<Catalog>>()) ==
          QStringList{u"Из сети"_s, u"Radio Record"_s});
    CHECK(QFile::exists(catalog::cachePath(url)));
    CHECK(catalog::etags().value(url) == "\"v1\""_L1);
    CHECK(catalog::secondsSinceUpdate());
    CHECK_FALSE(updater.isRunning());
}

TEST_CASE("an unchanged catalog is answered with 304")
{
    test::HttpServer server;
    const QString url = server.url(u"/catalog.json"_s);
    putInCache(url, catalogJson(u"net"_s, u"Кэш"_s));
    storage::writeJson(catalog::etagsPath(), QJsonObject{{url, u"\"v1\""_s}});
    server.respond(u"/catalog.json"_s, {304, {}, {}});

    CatalogUpdater updater({}, {url});
    QSignalSpy finished(&updater, &CatalogUpdater::finished);
    updater.updateNow();
    REQUIRE(finished.wait(5000));

    CHECK(server.requests(u"/catalog.json"_s).value(0).value("if-none-match") == "\"v1\"");
    CHECK_FALSE(finished[0][1].toBool());
    CHECK(catalogNames(finished[0][0].value<QList<Catalog>>()).contains(u"Кэш"_s));
}

TEST_CASE("a missing cache is fetched in full despite a stored etag")
{
    // На 304 сервер не присылает тела: с ETag от удалённого файла кэш
    // не восстановился бы, пока каталог не изменится.
    test::HttpServer server;
    const QString url = server.url(u"/catalog.json"_s);
    storage::writeJson(catalog::etagsPath(), QJsonObject{{url, u"\"v1\""_s}});
    server.respond(u"/catalog.json"_s,
                   {200, QJsonDocument(catalogJson(u"net"_s, u"Из сети"_s)).toJson(), {}});

    CatalogUpdater updater({}, {url});
    QSignalSpy finished(&updater, &CatalogUpdater::finished);
    updater.updateNow();
    REQUIRE(finished.wait(5000));

    CHECK_FALSE(server.requests(u"/catalog.json"_s).value(0).contains("if-none-match"));
    CHECK(QFile::exists(catalog::cachePath(url)));
}

TEST_CASE("a broken catalog keeps the previous copy")
{
    test::HttpServer server;
    const QString url = server.url(u"/catalog.json"_s);
    putInCache(url, catalogJson(u"net"_s, u"Кэш"_s));
    server.respond(u"/catalog.json"_s, {200, "{ это не json", {}});

    CatalogUpdater updater({}, {url});
    QSignalSpy finished(&updater, &CatalogUpdater::finished);
    updater.updateNow();
    REQUIRE(finished.wait(5000));

    CHECK_FALSE(finished[0][1].toBool());
    CHECK(catalogNames(catalog::loadCached({url})) == QStringList{u"Кэш"_s});
}

TEST_CASE("a server error keeps the previous copy")
{
    test::HttpServer server;
    const QString url = server.url(u"/catalog.json"_s);
    putInCache(url, catalogJson(u"net"_s, u"Кэш"_s));
    server.respond(u"/catalog.json"_s, {500, "oops", {}});

    CatalogUpdater updater({}, {url});
    QSignalSpy finished(&updater, &CatalogUpdater::finished);
    updater.updateNow();
    REQUIRE(finished.wait(5000));

    CHECK_FALSE(finished[0][1].toBool());
    CHECK(catalogNames(catalog::loadCached({url})) == QStringList{u"Кэш"_s});
}

TEST_CASE("a cancelled update is not recorded as done")
{
    // Отмена при выходе не должна выдавать прерванный цикл за обновление:
    // иначе следующий запуск счёл бы каталоги свежими и не стал бы их проверять.
    test::HttpServer server;
    CatalogUpdater updater({}, {server.url(u"/catalog.json"_s)});
    QSignalSpy finished(&updater, &CatalogUpdater::finished);
    updater.updateNow();
    updater.cancel();

    CHECK_FALSE(updater.isRunning());
    CHECK_FALSE(finished.wait(300));
    CHECK_FALSE(catalog::secondsSinceUpdate());
}
