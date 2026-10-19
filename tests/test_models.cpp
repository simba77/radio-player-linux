// Валидация станций, каталогов и оверлея.
//
// Каталог приходит из сети, оверлей — с диска: обе стороны могут оказаться битыми,
// и ни одна не должна ронять приложение или обнулять список станций.

#include "models.hpp"
#include "support.hpp"

#include <QJsonArray>
#include <QJsonDocument>

#include <catch2/catch_test_macros.hpp>

using namespace radio;
using namespace Qt::StringLiterals;

namespace {

QJsonObject parse(const char *json)
{
    return QJsonDocument::fromJson(json).object();
}

QJsonObject valid()
{
    return parse(R"({
        "schema": 1, "id": "record", "name": "Radio Record", "revision": 3,
        "stations": [{"id": "record-main", "name": "Main", "url": "https://example.org/main.m3u8"}]
    })");
}

QJsonObject with(QJsonObject object, const QString &key, const QJsonValue &value)
{
    object.insert(key, value);
    return object;
}

QStringList ids(const QList<Station> &stations)
{
    QStringList out;
    for (const Station &station : stations)
        out.append(station.id);
    return out;
}

}  // namespace

// MARK: - Каталог

TEST_CASE("a valid catalog parses")
{
    const auto catalog = Catalog::fromJson(valid());
    REQUIRE(catalog);
    CHECK(catalog->id == "record"_L1);
    CHECK(catalog->revision == 3);
    CHECK(catalog->stations[0].id == "record-main"_L1);
}

TEST_CASE("a catalog with a newer schema is rejected")
{
    CHECK_FALSE(Catalog::fromJson(with(valid(), u"schema"_s, 99)));
}

TEST_CASE("a catalog without a schema is rejected")
{
    QJsonObject payload = valid();
    payload.remove("schema"_L1);
    CHECK_FALSE(Catalog::fromJson(payload));
}

TEST_CASE("a catalog with a fractional schema is rejected")
{
    CHECK_FALSE(Catalog::fromJson(with(valid(), u"schema"_s, 0.5)));
}

TEST_CASE("a catalog without stations is rejected")
{
    CHECK_FALSE(Catalog::fromJson(with(valid(), u"stations"_s, QJsonArray())));
}

TEST_CASE("a catalog skips broken stations but keeps good ones")
{
    const auto catalog = Catalog::fromJson(with(valid(), u"stations"_s,
                                                QJsonDocument::fromJson(R"([
        {"id": "ok", "name": "Хорошая", "url": "https://example.org/ok.m3u8"},
        {"id": "no-url", "name": "Без адреса"},
        {"name": "Без id", "url": "https://example.org/x.m3u8"},
        "мусор"
    ])")
                                                    .array()));
    REQUIRE(catalog);
    CHECK(ids(catalog->stations) == QStringList{u"ok"_s});
}

TEST_CASE("a catalog ignores duplicate station ids")
{
    const auto catalog = Catalog::fromJson(with(valid(), u"stations"_s,
                                                QJsonDocument::fromJson(R"([
        {"id": "dup", "name": "Первая", "url": "https://example.org/1.m3u8"},
        {"id": "dup", "name": "Вторая", "url": "https://example.org/2.m3u8"}
    ])")
                                                    .array()));
    REQUIRE(catalog);
    REQUIRE(catalog->stations.size() == 1);
    CHECK(catalog->stations[0].name == u"Первая"_s);
}

TEST_CASE("a catalog ignores unknown fields")
{
    QJsonObject payload = with(valid(), u"publisher"_s, u"кто-то"_s);
    payload = with(payload, u"stations"_s,
                   QJsonDocument::fromJson(R"([
        {"id": "x", "name": "X", "url": "https://example.org/x.m3u8", "tags": ["electronic"]}
    ])")
                       .array());
    CHECK(Catalog::fromJson(payload));
}

TEST_CASE("a catalog drops stations with unplayable urls")
{
    const auto catalog = Catalog::fromJson(with(valid(), u"stations"_s,
                                                QJsonDocument::fromJson(R"([
        {"id": "web", "name": "Сеть", "url": "https://example.org/ok.m3u8"},
        {"id": "file", "name": "Файл", "url": "file:///etc/passwd"},
        {"id": "device", "name": "Устройство", "url": "avdevice://alsa:default"}
    ])")
                                                    .array()));
    REQUIRE(catalog);
    CHECK(ids(catalog->stations) == QStringList{u"web"_s});
}

TEST_CASE("a catalog without a name is named by its id")
{
    QJsonObject payload = valid();
    payload.remove("name"_L1);
    const auto catalog = Catalog::fromJson(payload);
    REQUIRE(catalog);
    CHECK(catalog->name == "record"_L1);
}

// MARK: - Станция

TEST_CASE("a station accepts the macOS field name")
{
    // macOS-версия писала поле `urlString` — нужно для импорта.
    const auto station =
        Station::fromJson(parse(R"({"id": "x", "name": "X", "urlString": "https://example.org/x.m3u8"})"));
    REQUIRE(station);
    CHECK(station->url == "https://example.org/x.m3u8"_L1);
}

TEST_CASE("a station with a non-web url is rejected")
{
    for (const char *url : {"file:///etc/passwd", "avdevice://alsa:default", "/local/file.mp3", ""}) {
        INFO(url);
        CHECK_FALSE(Station::fromJson(QJsonObject{{"name"_L1, "X"_L1}, {"url"_L1, QString::fromUtf8(url)}}));
    }
}

TEST_CASE("a station with a web url is accepted")
{
    for (const char *url :
         {"http://example.org/a.mp3", "https://example.org/a.m3u8", "HTTPS://EXAMPLE.ORG/A"}) {
        INFO(url);
        CHECK(Station::fromJson(QJsonObject{{"name"_L1, "X"_L1}, {"url"_L1, QString::fromUtf8(url)}}));
    }
}

TEST_CASE("a station without an id becomes local")
{
    const auto station = Station::fromJson(parse(R"({"name": "X", "url": "https://example.org/x.mp3"})"));
    REQUIRE(station);
    CHECK(station->isCustom());
    CHECK(station->id.size() > QString::fromLatin1(LOCAL_ID_PREFIX).size());
}

TEST_CASE("hls is recognised by the path, not the query")
{
    CHECK(Station{u"X"_s, u"https://example.org/live.m3u8?token=1"_s, u"x"_s}.isHls());
    CHECK_FALSE(Station{u"X"_s, u"https://example.org/live.mp3?f=.m3u8"_s, u"x"_s}.isHls());
}

// MARK: - Оверлей

TEST_CASE("an overlay survives a roundtrip")
{
    Overlay overlay;
    overlay.order = {u"a"_s, u"b"_s};
    overlay.hidden = {u"c"_s};
    overlay.renamed = {{u"a"_s, u"Моё"_s}};
    overlay.custom = {Station{u"Своя"_s, u"https://example.org/s.mp3"_s, u"local:1"_s}};

    const Overlay restored = Overlay::fromJson(overlay.toJson());
    CHECK(restored.order == overlay.order);
    CHECK(restored.hidden == overlay.hidden);
    CHECK(restored.renamed == overlay.renamed);
    REQUIRE(restored.custom.size() == 1);
    CHECK(restored.custom[0].id == "local:1"_L1);
}

TEST_CASE("an overlay keeps the file format of version 1.x")
{
    // Оверлей переживает переход между версиями: переименование хранится
    // объектом с полем `name`, а не строкой.
    Overlay overlay;
    overlay.renamed = {{u"a"_s, u"Моё"_s}};
    CHECK(overlay.toJson().value("overrides"_L1).toObject() ==
          QJsonObject{{"a"_L1, QJsonObject{{"name"_L1, u"Моё"_s}}}});
    CHECK(overlay.toJson().value("schema"_L1).toInt() == OVERLAY_SCHEMA_VERSION);
}

TEST_CASE("a broken overlay degrades to empty")
{
    CHECK(Overlay::fromJson(u"не словарь"_s).custom.isEmpty());
    const Overlay overlay =
        Overlay::fromJson(parse(R"({"custom": ["мусор"], "overrides": {"a": "строка"}})"));
    CHECK(overlay.custom.isEmpty());
    CHECK(overlay.renamed.isEmpty());
}

// MARK: - ICY

TEST_CASE("icy title separates the artist")
{
    const auto track = splitIcyTitle(u"TIESTO/BIA/21 SAVAGE - Both (Record Mix)"_s);
    REQUIRE(track);
    CHECK(track->artist == "TIESTO/BIA/21 SAVAGE"_L1);
    CHECK(track->title == "Both (Record Mix)"_L1);
    CHECK(track->display() == u"TIESTO/BIA/21 SAVAGE — Both (Record Mix)"_s);
}

TEST_CASE("icy title without a separator is all title")
{
    const auto track = splitIcyTitle(u"Просто название"_s);
    REQUIRE(track);
    CHECK(track->artist.isEmpty());
    CHECK(track->title == u"Просто название"_s);
}

TEST_CASE("an empty icy title is ignored")
{
    CHECK_FALSE(splitIcyTitle(u"   "_s));
}
