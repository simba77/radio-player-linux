#include "station_store.hpp"

#include "paths.hpp"
#include "storage.hpp"

#include <QHash>

#include <algorithm>

namespace radio {

namespace {

// Вставляет скрытые станции обратно, сохраняя их привязку к соседям.
//
// Пользователь двигает только видимые строки, но `order` описывает весь список.
// Каждую скрытую станцию привязываем к ближайшей видимой сверху и возвращаем
// сразу после неё — иначе возвращённая из скрытых станция уезжала бы в конец.
QStringList mergeOrder(const QStringList &visibleOrder, const QStringList &fullOrder)
{
    const QSet<QString> visible(visibleOrder.cbegin(), visibleOrder.cend());
    QStringList leading;
    QHash<QString, QStringList> followers;
    QString anchor;

    for (const QString &stationId : fullOrder) {
        if (visible.contains(stationId))
            anchor = stationId;
        else if (anchor.isNull())
            leading.append(stationId);
        else
            followers[anchor].append(stationId);
    }

    QStringList result = leading;
    for (const QString &stationId : visibleOrder) {
        result.append(stationId);
        result.append(followers.value(stationId));
    }
    return result;
}

}  // namespace

StationStore::StationStore(const QList<Catalog> &catalogs, QObject *parent)
    : QObject(parent), catalogs_(catalogs),
      overlay_(Overlay::fromJson(storage::readDocument(paths::overlayFile(), OVERLAY_SCHEMA_VERSION)))
{
    rebuild();
}

// MARK: - Чтение

QList<Station> StationStore::hiddenStations() const
{
    QList<Station> hidden;
    for (const Station &station : catalogStations()) {
        if (overlay_.hidden.contains(station.id))
            hidden.append(applyRename(station));
    }
    return hidden;
}

std::optional<Station> StationStore::find(const QString &stationId) const
{
    for (const Station &station : stations_) {
        if (station.id == stationId)
            return station;
    }
    return std::nullopt;
}

// MARK: - Каталоги

void StationStore::setCatalogs(const QList<Catalog> &catalogs)
{
    catalogs_ = catalogs;
    rebuild();
}

// MARK: - Операции пользователя

void StationStore::hide(const QString &stationId)
{
    if (overlay_.hidden.contains(stationId))
        return;
    overlay_.hidden.insert(stationId);
    commit();
}

void StationStore::unhide(const QString &stationId)
{
    if (!overlay_.hidden.remove(stationId))
        return;
    commit();
}

void StationStore::rename(const QString &stationId, const QString &name)
{
    const QString cleaned = name.trimmed();
    if (cleaned.isEmpty())
        return;

    for (Station &station : overlay_.custom) {
        if (station.id == stationId) {
            station.name = cleaned;
            commit();
            return;
        }
    }

    const auto original = catalogStation(stationId);
    if (!original)
        return;
    if (original->name == cleaned)
        overlay_.renamed.remove(stationId);
    else
        overlay_.renamed.insert(stationId, cleaned);
    commit();
}

std::optional<Station> StationStore::addCustom(const QString &name, const QString &url)
{
    const QString cleanedName = name.trimmed();
    const QString cleanedUrl = url.trimmed();
    // Проверка здесь, а не только в диалоге: станции приходят ещё и импортом.
    if (cleanedName.isEmpty() || !isPlayableUrl(cleanedUrl))
        return std::nullopt;
    const Station station = Station::local(cleanedName, cleanedUrl);
    overlay_.custom.append(station);
    overlay_.order = currentOrder() << station.id;
    commit();
    return station;
}

void StationStore::removeCustom(const QString &stationId)
{
    const auto removed =
        overlay_.custom.removeIf([&](const Station &station) { return station.id == stationId; });
    if (removed == 0)
        return;
    overlay_.order.removeAll(stationId);
    commit();
}

std::optional<Station> StationStore::duplicate(const QString &stationId)
{
    const auto original = find(stationId);
    if (!original || original->isCustom())
        return std::nullopt;
    const Station copy = Station::local(original->name, original->url);
    QStringList order = currentOrder();
    std::replace(order.begin(), order.end(), stationId, copy.id);
    overlay_.custom.append(copy);
    overlay_.hidden.insert(stationId);
    overlay_.order = order;
    commit();
    return copy;
}

void StationStore::setCustomUrl(const QString &stationId, const QString &url)
{
    const QString cleaned = url.trimmed();
    if (!isPlayableUrl(cleaned))
        return;
    for (Station &station : overlay_.custom) {
        if (station.id == stationId) {
            station.url = cleaned;
            commit();
            return;
        }
    }
}

void StationStore::setOrder(const QStringList &visibleOrder)
{
    QSet<QString> known;
    for (const Station &station : stations_)
        known.insert(station.id);
    QStringList filtered;
    for (const QString &stationId : visibleOrder) {
        if (known.contains(stationId))
            filtered.append(stationId);
    }
    if (filtered.size() != known.size())
        return;  // Пришёл неполный список — не рискуем порядком.
    overlay_.order = mergeOrder(filtered, currentOrder());
    commit();
}

void StationStore::move(int from, int to)
{
    QStringList order;
    for (const Station &station : stations_)
        order.append(station.id);
    if (from < 0 || from >= order.size() || to < 0 || to >= order.size())
        return;
    order.move(from, to);
    // В order попадают и скрытые станции — их относительный порядок надо сохранить.
    overlay_.order = mergeOrder(order, currentOrder());
    commit();
}

void StationStore::reset()
{
    overlay_ = Overlay();
    commit();
}

// MARK: - Мерж

void StationStore::rebuild()
{
    QList<Station> merged;
    QSet<QString> seen;

    // 1-2. Станции каталогов, затем свои станции пользователя;
    // при коллизии id побеждает тот, кто раньше.
    const QList<Station> sources = catalogStations() + overlay_.custom;
    for (const Station &station : sources) {
        if (seen.contains(station.id))
            continue;
        seen.insert(station.id);
        merged.append(station);
    }

    // 3-4. Скрытые убираем, переименования применяем.
    QList<Station> visible;
    for (const Station &station : merged) {
        if (!overlay_.hidden.contains(station.id))
            visible.append(applyRename(station));
    }

    // 5. Порядок из оверлея; новые станции каталога уходят в конец.
    QHash<QString, qsizetype> position;
    for (qsizetype index = 0; index < overlay_.order.size(); ++index)
        position.insert(overlay_.order[index], index);
    const qsizetype tail = position.size();
    // Сортировка устойчивая, поэтому станции вне order сохраняют порядок каталога.
    std::stable_sort(visible.begin(), visible.end(), [&](const Station &left, const Station &right) {
        return position.value(left.id, tail) < position.value(right.id, tail);
    });

    stations_ = visible;
    emit stationsChanged();
}

QList<Station> StationStore::catalogStations() const
{
    QList<Station> stations;
    for (const Catalog &catalog : catalogs_)
        stations.append(catalog.stations);
    return stations;
}

std::optional<Station> StationStore::catalogStation(const QString &stationId) const
{
    for (const Station &station : catalogStations()) {
        if (station.id == stationId)
            return station;
    }
    return std::nullopt;
}

Station StationStore::applyRename(Station station) const
{
    const QString name = overlay_.renamed.value(station.id);
    if (!name.isEmpty())
        station.name = name;
    return station;
}

QStringList StationStore::currentOrder() const
{
    // Текущий порядок с учётом станций, которых ещё нет в оверлее.
    const QSet<QString> known(overlay_.order.cbegin(), overlay_.order.cend());
    QStringList order = overlay_.order;
    for (const Station &station : stations_) {
        if (!known.contains(station.id))
            order.append(station.id);
    }
    return order;
}

// MARK: - Сохранение

void StationStore::commit()
{
    storage::writeJson(paths::overlayFile(), overlay_.toJson());
    rebuild();
}

}  // namespace radio
