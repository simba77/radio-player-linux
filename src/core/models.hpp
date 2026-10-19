#pragma once

// Модели данных: станция, трек, каталог станций и локальный оверлей.
// Схема каталогов и правила мержа — в `docs/architecture.md`.

#include <QJsonObject>
#include <QJsonValue>
#include <QList>
#include <QMap>
#include <QMetaType>
#include <QSet>
#include <QString>
#include <QStringList>

#include <optional>

namespace radio {

// Версия схемы каталога. Каталог с большей версией игнорируется целиком.
inline constexpr int CATALOG_SCHEMA_VERSION = 1;
// Версия схемы оверлея — она своя: форматы меняются независимо друг от друга.
inline constexpr int OVERLAY_SCHEMA_VERSION = 1;
// Префикс идентификаторов станций, созданных пользователем.
inline constexpr char LOCAL_ID_PREFIX[] = "local:";

// Готово ли приложение играть этот адрес. Список станций приходит из сети
// (каталог) и с диска (оверлей, импорт), а mpv откроет всё, что ему дадут:
// `file:///…` или `avdevice://…` в списке радиостанций — это уже не радио.
bool isPlayableUrl(const QString &url);

// Радиостанция. `id` — стабильный ключ: slug из каталога или `local:<uuid>`.
struct Station {
    QString name;
    QString url;
    QString id;

    // Своя станция пользователя со свежим идентификатором.
    static Station local(const QString &name, const QString &url);
    // Разбирает станцию, отбрасывая мусор. Неизвестные поля игнорируются;
    // без `id` станция становится своей.
    static std::optional<Station> fromJson(const QJsonValue &data);

    bool isHls() const;
    // Создана пользователем, а не пришла из каталога.
    bool isCustom() const;
    QJsonObject toJson() const;

    bool operator==(const Station &) const = default;
};

// Текущий трек, как его отдал провайдер метаданных. Пустые `artist`
// и `artworkUrl` означают, что их нет.
struct TrackInfo {
    QString title;
    QString artist;
    QString artworkUrl;

    // `Исполнитель — Трек`, а без исполнителя — просто название.
    QString display() const;

    bool operator==(const TrackInfo &) const = default;
};

// Подборка станций, публикуемая в репозитории (см. `docs/architecture.md`).
struct Catalog {
    QString id;
    QString name;
    qint64 revision = 0;
    QList<Station> stations;

    // Пусто, если каталог битый или его схема новее поддерживаемой.
    // Каталог приходит из сети, поэтому доверять ему нельзя: одна опечатка
    // в JSON не должна лишать пользователя всего списка станций.
    static std::optional<Catalog> fromJson(const QJsonValue &data);
};

// Правки пользователя поверх каталогов (см. `docs/architecture.md`).
//
// Хранится отдельно от каталогов, поэтому их обновление не затирает
// переименования, скрытия и порядок.
struct Overlay {
    QStringList order;
    QSet<QString> hidden;
    // Переименования станций каталога: id → новое название. В файле лежат
    // как `{"id": {"name": "…"}}` — место для будущих полей оставлено.
    QMap<QString, QString> renamed;
    QList<Station> custom;

    QJsonObject toJson() const;
    // Битый оверлей — это потеря пользовательских правок, но не повод падать.
    static Overlay fromJson(const QJsonValue &data);
};

// Разбирает `icy-title` вида `ARTIST - Title`.
//
// Разделитель ` - ` не гарантирован стандартом, поэтому при его отсутствии
// всю строку считаем названием трека.
std::optional<TrackInfo> splitIcyTitle(const QString &raw);

}  // namespace radio

Q_DECLARE_METATYPE(radio::Station)
Q_DECLARE_METATYPE(radio::TrackInfo)
