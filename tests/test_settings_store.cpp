// Хранилище настроек: правка, сохранение и уведомление за одну операцию.

#include "settings_store.hpp"
#include "support.hpp"

#include <QSignalSpy>

#include <catch2/catch_test_macros.hpp>

using namespace radio;
using namespace Qt::StringLiterals;

namespace {

const QString CATALOG = u"https://example.org/catalog.json"_s;
const QString OTHER = u"https://example.net/catalog.json"_s;

}  // namespace

TEST_CASE("settings defaults are read from disk")
{
    SettingsStore store;
    CHECK(store.closeToTray());
    CHECK(store.extraCatalogUrls().isEmpty());
}

TEST_CASE("a change is saved immediately")
{
    SettingsStore store;
    store.setCloseToTray(false);
    CHECK_FALSE(loadSettings().closeToTray);
}

TEST_CASE("a change notifies")
{
    SettingsStore store;
    QSignalSpy changes(&store, &SettingsStore::changed);
    store.setUpdateIntervalHours(6);
    CHECK(changes.count() == 1);
}

TEST_CASE("writing the same value changes nothing")
{
    // Виджеты шлют сигнал и когда значение не менялось — файл трогать не за что.
    SettingsStore store;
    store.setLanguage(u"ru"_s);
    QSignalSpy changes(&store, &SettingsStore::changed);
    store.setLanguage(u"ru"_s);
    CHECK(changes.isEmpty());
}

TEST_CASE("the interval is normalised like on disk")
{
    SettingsStore store;
    store.setUpdateIntervalHours(0);
    CHECK(store.updateIntervalHours() == 1);
    CHECK(loadSettings().updateIntervalHours == 1);
}

TEST_CASE("an added catalog is saved and announced")
{
    SettingsStore store;
    QSignalSpy urls(&store, &SettingsStore::catalogUrlsChanged);
    CHECK(store.addCatalogUrl(CATALOG));
    CHECK(store.extraCatalogUrls() == QStringList{CATALOG});
    CHECK(loadSettings().extraCatalogUrls == QStringList{CATALOG});
    CHECK(urls.count() == 1);
}

TEST_CASE("a duplicate catalog is rejected")
{
    SettingsStore store;
    store.addCatalogUrl(CATALOG);
    QSignalSpy urls(&store, &SettingsStore::catalogUrlsChanged);
    CHECK_FALSE(store.addCatalogUrl(CATALOG));
    CHECK(urls.isEmpty());
}

TEST_CASE("a removed catalog is saved and announced")
{
    SettingsStore store;
    store.addCatalogUrl(CATALOG);
    store.addCatalogUrl(OTHER);
    QSignalSpy urls(&store, &SettingsStore::catalogUrlsChanged);
    CHECK(store.removeCatalogUrl(CATALOG));
    CHECK(store.extraCatalogUrls() == QStringList{OTHER});
    CHECK(urls.count() == 1);
}

TEST_CASE("removing an unknown catalog changes nothing")
{
    SettingsStore store;
    QSignalSpy urls(&store, &SettingsStore::catalogUrlsChanged);
    CHECK_FALSE(store.removeCatalogUrl(CATALOG));
    CHECK(urls.isEmpty());
}

TEST_CASE("settings survive a restart")
{
    {
        SettingsStore store;
        store.setCloseToTray(false);
        store.setLanguage(u"en"_s);
        store.addCatalogUrl(CATALOG);
    }
    SettingsStore restarted;
    CHECK_FALSE(restarted.closeToTray());
    CHECK(restarted.language() == "en"_L1);
    CHECK(restarted.extraCatalogUrls() == QStringList{CATALOG});
}

TEST_CASE("a plain http catalog is rejected")
{
    SettingsStore store;
    QSignalSpy urls(&store, &SettingsStore::catalogUrlsChanged);
    CHECK_FALSE(store.addCatalogUrl(u"http://example.org/catalog.json"_s));
    CHECK(store.extraCatalogUrls().isEmpty());
    CHECK(urls.isEmpty());
}
