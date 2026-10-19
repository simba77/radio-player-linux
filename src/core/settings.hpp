#pragma once

// Пользовательские настройки приложения (`settings.json`).
//
// Автозапуск сюда не входит: его состояние определяется наличием `.desktop`-файла,
// и дублировать его в настройках значило бы завести второй источник правды.

#include <QJsonObject>
#include <QJsonValue>
#include <QString>
#include <QStringList>

namespace radio {

// Версия схемы файла настроек — своя, не связанная с версией каталогов.
inline constexpr int SETTINGS_SCHEMA_VERSION = 1;

// Языки интерфейса. `en` — исходные строки в коде, но перевод нужен и ему:
// формы множественного числа записаны в коде как «%n day(s)».
inline const QStringList LANGUAGES{QStringLiteral("en"), QStringLiteral("ru")};
// Значение настройки «как в системе».
inline constexpr char LANGUAGE_AUTO[] = "auto";

// Границы интервала обновления каталогов, часы.
inline constexpr int MIN_UPDATE_INTERVAL_HOURS = 1;
inline constexpr int MAX_UPDATE_INTERVAL_HOURS = 24 * 30;

struct Settings {
    // Закрытие окна прячет его в трей вместо выхода из приложения.
    bool closeToTray = true;
    // Как часто обновлять каталоги станций.
    int updateIntervalHours = 24;
    // Дополнительные каталоги, добавленные пользователем поверх встроенных.
    QStringList extraCatalogUrls;
    // Язык интерфейса: `auto` (как в системе) или код из `LANGUAGES`.
    QString language = QLatin1String(LANGUAGE_AUTO);

    qint64 updateIntervalSeconds() const { return qint64(updateIntervalHours) * 3600; }

    QJsonObject toJson() const;
    static Settings fromJson(const QJsonValue &data);

    bool operator==(const Settings &) const = default;
};

// Только HTTPS: каталог задаёт, что и откуда играть, и подменить его
// по дороге — значит подменить весь список станций пользователя.
bool isAcceptableCatalogUrl(const QString &url);

Settings loadSettings();
bool saveSettings(const Settings &settings);

}  // namespace radio
