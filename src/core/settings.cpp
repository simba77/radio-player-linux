#include "settings.hpp"

#include "json_values.hpp"
#include "paths.hpp"
#include "storage.hpp"

#include <QJsonArray>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace radio {

bool isAcceptableCatalogUrl(const QString &url)
{
    return url.startsWith("https://"_L1, Qt::CaseInsensitive);
}

QJsonObject Settings::toJson() const
{
    return {
        {"schema"_L1, SETTINGS_SCHEMA_VERSION},
        {"close_to_tray"_L1, closeToTray},
        {"update_interval_hours"_L1, updateIntervalHours},
        {"extra_catalog_urls"_L1, QJsonArray::fromStringList(extraCatalogUrls)},
        {"language"_L1, language},
    };
}

Settings Settings::fromJson(const QJsonValue &data)
{
    Settings settings;
    if (!data.isObject())
        return settings;
    const QJsonObject object = data.toObject();

    if (object.contains("close_to_tray"_L1))
        settings.closeToTray = json::truthy(object.value("close_to_tray"_L1));

    if (const auto interval = json::integer(object.value("update_interval_hours"_L1))) {
        settings.updateIntervalHours =
            int(std::clamp<qint64>(*interval, MIN_UPDATE_INTERVAL_HOURS, MAX_UPDATE_INTERVAL_HOURS));
    }

    for (const QJsonValue &raw : object.value("extra_catalog_urls"_L1).toArray()) {
        const QString url = json::text(raw).trimmed();
        if (isAcceptableCatalogUrl(url))
            settings.extraCatalogUrls.append(url);
    }

    const QString language = object.value("language"_L1).toString();
    if (language == QLatin1String(LANGUAGE_AUTO) || LANGUAGES.contains(language))
        settings.language = language;

    return settings;
}

Settings loadSettings()
{
    return Settings::fromJson(storage::readDocument(paths::settingsFile(), SETTINGS_SCHEMA_VERSION));
}

bool saveSettings(const Settings &settings)
{
    return storage::writeJson(paths::settingsFile(), settings.toJson());
}

}  // namespace radio
