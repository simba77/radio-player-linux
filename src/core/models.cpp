#include "models.hpp"

#include "json_values.hpp"

#include <QJsonArray>
#include <QUuid>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace radio {

namespace {

// Схемы, которые приложение готово играть.
const QStringList PLAYABLE_SCHEMES{u"http://"_s, u"https://"_s};

// Элементы списка идентификаторов: строки как есть, остальное — своей записью.
QStringList textList(const QJsonValue &value)
{
    QStringList items;
    for (const QJsonValue &item : value.toArray())
        items.append(json::text(item));
    return items;
}

}  // namespace

bool isPlayableUrl(const QString &url)
{
    return std::any_of(PLAYABLE_SCHEMES.cbegin(), PLAYABLE_SCHEMES.cend(),
                       [&](const QString &scheme) { return url.startsWith(scheme, Qt::CaseInsensitive); });
}

// MARK: - Station

Station Station::local(const QString &name, const QString &url)
{
    return {name, url, QLatin1String(LOCAL_ID_PREFIX) + QUuid::createUuid().toString(QUuid::WithoutBraces)};
}

std::optional<Station> Station::fromJson(const QJsonValue &data)
{
    if (!data.isObject())
        return std::nullopt;
    const QJsonObject object = data.toObject();

    const QString name = json::text(object.value("name"_L1)).trimmed();
    // `urlString` — имя поля в macOS-версии, поддерживаем для импорта.
    QString url = json::text(object.value("url"_L1));
    if (url.isEmpty())
        url = json::text(object.value("urlString"_L1));
    url = url.trimmed();
    if (name.isEmpty() || !isPlayableUrl(url))
        return std::nullopt;

    const QString id = json::text(object.value("id"_L1)).trimmed();
    if (id.isEmpty())
        return local(name, url);
    return Station{name, url, id};
}

bool Station::isHls() const
{
    return url.section(u'?', 0, 0).endsWith(".m3u8"_L1);
}

bool Station::isCustom() const
{
    return id.startsWith(QLatin1String(LOCAL_ID_PREFIX));
}

QJsonObject Station::toJson() const
{
    return {{"id"_L1, id}, {"name"_L1, name}, {"url"_L1, url}};
}

// MARK: - TrackInfo

QString TrackInfo::display() const
{
    return artist.isEmpty() ? title : artist + u" — "_s + title;
}

// MARK: - Catalog

std::optional<Catalog> Catalog::fromJson(const QJsonValue &data)
{
    if (!data.isObject())
        return std::nullopt;
    const QJsonObject object = data.toObject();

    const auto schema = json::integer(object.value("schema"_L1));
    if (!schema || *schema > CATALOG_SCHEMA_VERSION)
        return std::nullopt;
    const QString id = json::text(object.value("id"_L1)).trimmed();
    if (id.isEmpty())
        return std::nullopt;

    Catalog catalog;
    catalog.id = id;
    catalog.name = json::text(object.value("name"_L1));
    if (catalog.name.isEmpty())
        catalog.name = id;
    catalog.revision = json::integer(object.value("revision"_L1)).value_or(0);

    QSet<QString> seen;
    for (const QJsonValue &raw : object.value("stations"_L1).toArray()) {
        const auto station = Station::fromJson(raw);
        // Станция каталога обязана иметь стабильный id — без него мерж развалится.
        if (!station || station->isCustom() || seen.contains(station->id))
            continue;
        seen.insert(station->id);
        catalog.stations.append(*station);
    }
    if (catalog.stations.isEmpty())
        return std::nullopt;
    return catalog;
}

// MARK: - Overlay

QJsonObject Overlay::toJson() const
{
    QStringList sortedHidden(hidden.cbegin(), hidden.cend());
    sortedHidden.sort();

    QJsonObject overrides;
    for (auto it = renamed.cbegin(); it != renamed.cend(); ++it)
        overrides.insert(it.key(), QJsonObject{{"name"_L1, it.value()}});

    QJsonArray customArray;
    for (const Station &station : custom)
        customArray.append(station.toJson());

    return {
        {"schema"_L1, OVERLAY_SCHEMA_VERSION},
        {"order"_L1, QJsonArray::fromStringList(order)},
        {"hidden"_L1, QJsonArray::fromStringList(sortedHidden)},
        {"overrides"_L1, overrides},
        {"custom"_L1, customArray},
    };
}

Overlay Overlay::fromJson(const QJsonValue &data)
{
    Overlay overlay;
    if (!data.isObject())
        return overlay;
    const QJsonObject object = data.toObject();

    for (const QJsonValue &raw : object.value("custom"_L1).toArray()) {
        if (const auto station = Station::fromJson(raw))
            overlay.custom.append(*station);
    }

    const QJsonObject overrides = object.value("overrides"_L1).toObject();
    for (auto it = overrides.constBegin(); it != overrides.constEnd(); ++it) {
        const QJsonValue name = it.value().toObject().value("name"_L1);
        if (name.isString())
            overlay.renamed.insert(it.key(), name.toString());
    }

    overlay.order = textList(object.value("order"_L1));
    const QStringList hidden = textList(object.value("hidden"_L1));
    overlay.hidden = QSet<QString>(hidden.cbegin(), hidden.cend());
    return overlay;
}

// MARK: - ICY

std::optional<TrackInfo> splitIcyTitle(const QString &raw)
{
    const QString cleaned = raw.trimmed();
    if (cleaned.isEmpty())
        return std::nullopt;

    const qsizetype separator = cleaned.indexOf(" - "_L1);
    if (separator >= 0) {
        const QString artist = cleaned.left(separator).trimmed();
        const QString title = cleaned.mid(separator + 3).trimmed();
        if (!artist.isEmpty() && !title.isEmpty())
            return TrackInfo{title, artist, {}};
    }
    return TrackInfo{cleaned, {}, {}};
}

}  // namespace radio
