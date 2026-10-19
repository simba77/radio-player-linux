// Настройки приложения. Автозапуск — в `test_autostart.cpp`.

#include "paths.hpp"
#include "settings.hpp"
#include "support.hpp"

#include <QFile>
#include <QJsonArray>

#include <catch2/catch_test_macros.hpp>

using namespace radio;
using namespace Qt::StringLiterals;

TEST_CASE("defaults when the file is missing")
{
    const Settings loaded = loadSettings();
    CHECK(loaded.closeToTray);
    CHECK(loaded.updateIntervalHours == 24);
    CHECK(loaded.extraCatalogUrls.isEmpty());
    CHECK(loaded.language == "auto"_L1);
}

TEST_CASE("settings survive a roundtrip")
{
    Settings settings;
    settings.closeToTray = false;
    settings.updateIntervalHours = 6;
    settings.extraCatalogUrls = {u"https://example.org/c.json"_s};
    settings.language = u"ru"_s;
    saveSettings(settings);

    CHECK(loadSettings() == settings);
}

TEST_CASE("interval is clamped")
{
    CHECK(Settings::fromJson(QJsonObject{{"update_interval_hours"_L1, 0}}).updateIntervalHours == 1);
    const Settings huge = Settings::fromJson(QJsonObject{{"update_interval_hours"_L1, 1'000'000}});
    CHECK(huge.updateIntervalHours == MAX_UPDATE_INTERVAL_HOURS);
}

TEST_CASE("broken values fall back to defaults")
{
    const Settings loaded = Settings::fromJson(QJsonObject{
        {"update_interval_hours"_L1, "часто"_L1},
        {"extra_catalog_urls"_L1, "нет"_L1},
        {"language"_L1, "klingon"_L1},
    });
    CHECK(loaded.updateIntervalHours == 24);
    CHECK(loaded.extraCatalogUrls.isEmpty());
    CHECK(loaded.language == "auto"_L1);
}

TEST_CASE("non-http catalog urls are dropped")
{
    const Settings loaded = Settings::fromJson(QJsonObject{
        {"extra_catalog_urls"_L1,
         QJsonArray{"ftp://example.org/c.json"_L1, "/etc/passwd"_L1, "https://example.org/ok.json"_L1}},
    });
    CHECK(loaded.extraCatalogUrls == QStringList{u"https://example.org/ok.json"_s});
}

TEST_CASE("update interval in seconds")
{
    Settings settings;
    settings.updateIntervalHours = 2;
    CHECK(settings.updateIntervalSeconds() == 7200);
}

TEST_CASE("plain http catalog urls are dropped")
{
    // Каталог задаёт, что и откуда играть: подменённый по дороге — это подменённый
    // список станций, поэтому только HTTPS.
    const Settings loaded = Settings::fromJson(QJsonObject{
        {"extra_catalog_urls"_L1,
         QJsonArray{"http://example.org/c.json"_L1, "https://example.org/ok.json"_L1}},
    });
    CHECK(loaded.extraCatalogUrls == QStringList{u"https://example.org/ok.json"_s});
}

TEST_CASE("settings from a future version fall back to defaults")
{
    // Файл новее поддерживаемого не разбирается наугад: берём умолчания,
    // а сам файл откладывается в сторону — см. `storage::readDocument`.
    test::writeFile(paths::settingsFile(), R"({"schema": 99, "close_to_tray": false})");

    CHECK(loadSettings().closeToTray);
    CHECK(QFile::exists(paths::configDir() + u"/settings.bak-v99"_s));
}

TEST_CASE("files written by the Python version are read the same way")
{
    // Python-версия писала отступ в два пробела и ключи в своём порядке;
    // для разбора это не должно иметь значения.
    test::writeFile(paths::settingsFile(), R"({
  "schema": 1,
  "close_to_tray": false,
  "update_interval_hours": 12,
  "extra_catalog_urls": ["https://example.org/c.json"],
  "language": "ru"
})");
    const Settings loaded = loadSettings();
    CHECK_FALSE(loaded.closeToTray);
    CHECK(loaded.updateIntervalHours == 12);
    CHECK(loaded.extraCatalogUrls == QStringList{u"https://example.org/c.json"_s});
    CHECK(loaded.language == "ru"_L1);
}
