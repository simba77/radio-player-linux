// Мерж каталогов с оверлеем — правила в `docs/architecture.md`.

#include "station_store.hpp"
#include "support.hpp"

#include <catch2/catch_test_macros.hpp>

#include <initializer_list>
#include <utility>

using namespace radio;
using namespace Qt::StringLiterals;

namespace {

Catalog makeCatalog(std::initializer_list<std::pair<const char *, const char *>> pairs,
                    const QString &catalogId = u"test"_s)
{
    Catalog catalog{catalogId, catalogId, 1, {}};
    for (const auto &[id, name] : pairs) {
        const QString stationId = QString::fromUtf8(id);
        catalog.stations.append(
            {QString::fromUtf8(name), u"https://example.org/%1.m3u8"_s.arg(stationId), stationId});
    }
    return catalog;
}

const Catalog &base()
{
    static const Catalog catalog = makeCatalog({{"a", "Первая"}, {"b", "Вторая"}, {"c", "Третья"}});
    return catalog;
}

QStringList names(const StationStore &store)
{
    QStringList out;
    for (const Station &station : store.stations())
        out.append(station.name);
    return out;
}

QStringList ids(const StationStore &store)
{
    QStringList out;
    for (const Station &station : store.stations())
        out.append(station.id);
    return out;
}

const QStringList ABC{u"a"_s, u"b"_s, u"c"_s};

}  // namespace

// MARK: - Базовый мерж

TEST_CASE("stations come from the catalog in order")
{
    StationStore store({base()});
    CHECK(ids(store) == ABC);
}

TEST_CASE("the earlier catalog wins on an id collision")
{
    StationStore store({base(), makeCatalog({{"b", "Дубль"}}, u"other"_s)});
    CHECK(store.find(u"b"_s)->name == u"Вторая"_s);
    CHECK(ids(store) == ABC);
}

TEST_CASE("custom stations go after the catalog")
{
    StationStore store({base()});
    store.addCustom(u"Моё радио"_s, u"https://example.org/mine.mp3"_s);
    CHECK(names(store).last() == u"Моё радио"_s);
}

TEST_CASE("a custom station needs a name and a playable url")
{
    StationStore store({base()});
    CHECK_FALSE(store.addCustom(u"  "_s, u"https://example.org/mine.mp3"_s));
    CHECK_FALSE(store.addCustom(u"Файл"_s, u"file:///etc/passwd"_s));
    CHECK(ids(store) == ABC);
}

// MARK: - Правки пользователя

TEST_CASE("a hidden station disappears")
{
    StationStore store({base()});
    store.hide(u"b"_s);
    CHECK(ids(store) == QStringList{u"a"_s, u"c"_s});
    REQUIRE(store.hiddenStations().size() == 1);
    CHECK(store.hiddenStations()[0].id == "b"_L1);
}

TEST_CASE("unhide restores the station")
{
    StationStore store({base()});
    store.hide(u"b"_s);
    store.unhide(u"b"_s);
    CHECK(ids(store) == ABC);
}

TEST_CASE("renaming a catalog station keeps its url")
{
    StationStore store({base()});
    store.rename(u"a"_s, u"Переименованная"_s);
    const auto station = store.find(u"a"_s);
    REQUIRE(station);
    CHECK(station->name == u"Переименованная"_s);
    CHECK(station->url == "https://example.org/a.m3u8"_L1);
}

TEST_CASE("renaming back to the original drops the override")
{
    StationStore store({base()});
    store.rename(u"a"_s, u"Переименованная"_s);
    store.rename(u"a"_s, u"Первая"_s);
    CHECK(store.overlay().renamed.isEmpty());
}

TEST_CASE("renaming a custom station is permanent")
{
    StationStore store({base()});
    const auto created = store.addCustom(u"Моё"_s, u"https://example.org/mine.mp3"_s);
    REQUIRE(created);
    store.rename(created->id, u"Моё радио"_s);
    CHECK(store.find(created->id)->name == u"Моё радио"_s);
    CHECK(store.overlay().renamed.isEmpty());
}

TEST_CASE("a custom station can be removed")
{
    StationStore store({base()});
    const auto created = store.addCustom(u"Моё"_s, u"https://example.org/mine.mp3"_s);
    REQUIRE(created);
    store.removeCustom(created->id);
    CHECK_FALSE(ids(store).contains(created->id));
    CHECK_FALSE(store.overlay().order.contains(created->id));
}

TEST_CASE("duplicate replaces the catalog station in place")
{
    StationStore store({base()});
    const auto copy = store.duplicate(u"b"_s);
    REQUIRE(copy);
    CHECK(copy->url == "https://example.org/b.m3u8"_L1);
    // Копия встаёт на место оригинала, оригинал скрывается.
    CHECK(ids(store) == QStringList{u"a"_s, copy->id, u"c"_s});
    CHECK(store.overlay().hidden.contains(u"b"_s));
}

TEST_CASE("a duplicate allows editing the url")
{
    StationStore store({base()});
    const auto copy = store.duplicate(u"b"_s);
    REQUIRE(copy);
    store.setCustomUrl(copy->id, u"https://example.org/custom.mp3"_s);
    CHECK(store.find(copy->id)->url == "https://example.org/custom.mp3"_L1);
}

TEST_CASE("a custom station cannot be duplicated")
{
    StationStore store({base()});
    const auto created = store.addCustom(u"Моё"_s, u"https://example.org/mine.mp3"_s);
    REQUIRE(created);
    CHECK_FALSE(store.duplicate(created->id));
}

TEST_CASE("a catalog station url cannot be edited")
{
    StationStore store({base()});
    store.setCustomUrl(u"a"_s, u"https://example.org/hack.mp3"_s);
    CHECK(store.find(u"a"_s)->url == "https://example.org/a.m3u8"_L1);
}

// MARK: - Порядок

TEST_CASE("move changes the order")
{
    StationStore store({base()});
    store.move(2, 0);
    CHECK(ids(store) == QStringList{u"c"_s, u"a"_s, u"b"_s});
}

TEST_CASE("move ignores indices out of range")
{
    StationStore store({base()});
    store.move(5, 0);
    store.move(0, -1);
    CHECK(ids(store) == ABC);
}

TEST_CASE("new catalog stations append to the end")
{
    StationStore store({base()});
    store.move(2, 0);  // зафиксировали свой порядок
    store.setCatalogs({makeCatalog({{"a", "Первая"}, {"b", "Вторая"}, {"c", "Третья"}, {"d", "Новая"}})});
    CHECK(ids(store) == QStringList{u"c"_s, u"a"_s, u"b"_s, u"d"_s});
}

TEST_CASE("set order applies a new sequence")
{
    StationStore store({base()});
    store.setOrder({u"c"_s, u"b"_s, u"a"_s});
    CHECK(ids(store) == QStringList{u"c"_s, u"b"_s, u"a"_s});
}

TEST_CASE("set order ignores an incomplete list")
{
    // Неполный список — признак рассинхрона UI, порядок трогать нельзя.
    StationStore store({base()});
    store.setOrder({u"c"_s});
    CHECK(ids(store) == ABC);
}

TEST_CASE("set order keeps hidden stations")
{
    StationStore store({base()});
    store.hide(u"b"_s);
    store.setOrder({u"c"_s, u"a"_s});
    store.unhide(u"b"_s);
    CHECK(ids(store) == QStringList{u"c"_s, u"a"_s, u"b"_s});
}

TEST_CASE("a hidden station returns to its place")
{
    // Скрытая станция не должна уезжать в конец после возврата.
    StationStore store({base()});
    store.hide(u"b"_s);
    store.move(1, 0);  // было [a, c] → стало [c, a]
    store.unhide(u"b"_s);
    CHECK(ids(store) == QStringList{u"c"_s, u"a"_s, u"b"_s});
}

// MARK: - Обновление каталогов

TEST_CASE("a catalog update changes the url but keeps the rename")
{
    StationStore store({base()});
    store.rename(u"a"_s, u"Моё название"_s);
    store.setCatalogs(
        {Catalog{u"test"_s, u"test"_s, 2, {{u"Первая"_s, u"https://example.org/new-a.m3u8"_s, u"a"_s}}}});
    const auto station = store.find(u"a"_s);
    REQUIRE(station);
    CHECK(station->name == u"Моё название"_s);
    CHECK(station->url == "https://example.org/new-a.m3u8"_L1);
}

TEST_CASE("a station removed from the catalog disappears")
{
    StationStore store({base()});
    store.setCatalogs({makeCatalog({{"a", "Первая"}})});
    CHECK(ids(store) == QStringList{u"a"_s});
}

TEST_CASE("custom stations survive the removal of a catalog")
{
    StationStore store({base()});
    const auto created = store.addCustom(u"Моё"_s, u"https://example.org/mine.mp3"_s);
    REQUIRE(created);
    store.setCatalogs({});
    CHECK(ids(store) == QStringList{created->id});
}

// MARK: - Сохранение и сброс

TEST_CASE("the overlay survives a restart")
{
    {
        StationStore store({base()});
        store.rename(u"a"_s, u"Моё название"_s);
        store.hide(u"c"_s);
        store.addCustom(u"Моё радио"_s, u"https://example.org/mine.mp3"_s);
    }
    StationStore restarted({base()});
    CHECK(names(restarted) == QStringList{u"Моё название"_s, u"Вторая"_s, u"Моё радио"_s});
}

TEST_CASE("reset discards user changes")
{
    StationStore store({base()});
    store.rename(u"a"_s, u"Моё название"_s);
    store.hide(u"b"_s);
    store.addCustom(u"Моё радио"_s, u"https://example.org/mine.mp3"_s);
    store.reset();
    CHECK(names(store) == QStringList{u"Первая"_s, u"Вторая"_s, u"Третья"_s});
}

TEST_CASE("an empty catalog list gives an empty store")
{
    CHECK(StationStore({}).stations().isEmpty());
}
