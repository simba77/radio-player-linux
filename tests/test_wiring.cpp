// Связки подсистем из точки входа.
//
// Раньше ошибка в одной из них стоила бага, который никакой модульный тест
// увидеть не мог: список каталогов правился в настройках, а обновление ходило
// по списку, снятому при запуске.

#include "catalog.hpp"
#include "settings_store.hpp"
#include "station_store.hpp"
#include "storage.hpp"
#include "support.hpp"
#include "wiring.hpp"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>

#include <catch2/catch_test_macros.hpp>

using namespace radio;
using namespace Qt::StringLiterals;

namespace {

const QString EXTRA_URL = u"https://example.org/extra.json"_s;

// Каталог, уже скачанный обновлением: сеть в тестах не трогаем.
void putExtraInCache()
{
    const QJsonObject data{
        {"schema"_L1, 1},
        {"id"_L1, "extra"_L1},
        {"name"_L1, u"Дополнительный"_s},
        {"revision"_L1, 1},
        {"stations"_L1, QJsonArray{QJsonObject{{"id"_L1, "extra-1"_L1},
                                               {"name"_L1, u"Из добавленного каталога"_s},
                                               {"url"_L1, "https://example.org/s"_L1}}}},
    };
    test::writeFile(catalog::cachePath(EXTRA_URL), QJsonDocument(data).toJson());
}

struct WiredFixture {
    SettingsStore settings;
    CatalogUpdater updater{settings.extraCatalogUrls()};
    StationStore store{catalog::loadInitial(updater.urls())};

    WiredFixture() { wireCatalogSources(&settings, &updater, &store); }

    bool hasExtraStation() const { return store.find(u"extra-1"_s).has_value(); }
};

}  // namespace

TEST_CASE_METHOD(WiredFixture, "an added catalog reaches the updater")
{
    settings.addCatalogUrl(EXTRA_URL);
    CHECK(updater.urls().contains(EXTRA_URL));
}

TEST_CASE_METHOD(WiredFixture, "an added catalog reaches the station list")
{
    putExtraInCache();
    settings.addCatalogUrl(EXTRA_URL);
    CHECK(hasExtraStation());
}

TEST_CASE_METHOD(WiredFixture, "a removed catalog leaves the station list")
{
    putExtraInCache();
    settings.addCatalogUrl(EXTRA_URL);
    settings.removeCatalogUrl(EXTRA_URL);
    CHECK_FALSE(hasExtraStation());
}

TEST_CASE("catalogs that were never updated are due")
{
    CHECK(catalogUpdateDue(SettingsStore()));
}

TEST_CASE("fresh catalogs are not updated again")
{
    storage::updateState({{"catalogs_updated_at"_L1, double(QDateTime::currentSecsSinceEpoch())}});
    CHECK_FALSE(catalogUpdateDue(SettingsStore()));
}

TEST_CASE("catalogs older than the interval are due")
{
    SettingsStore settings;
    settings.setUpdateIntervalHours(1);
    storage::updateState({{"catalogs_updated_at"_L1, double(QDateTime::currentSecsSinceEpoch() - 2 * 3600)}});
    CHECK(catalogUpdateDue(settings));
}
