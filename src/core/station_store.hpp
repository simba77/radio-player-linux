#pragma once

// Список станций: мерж каталогов с локальным оверлеем.
//
// Алгоритм описан в `docs/architecture.md`. Главное правило: каталог владеет URL
// станции, пользователь владеет видимостью, названием и порядком. Поэтому
// обновление каталога никогда не конфликтует с правками — они лежат в разных местах.

#include "models.hpp"

#include <QObject>

#include <optional>

namespace radio {

class StationStore : public QObject {
    Q_OBJECT
public:
    // Оверлей читается с диска.
    explicit StationStore(const QList<Catalog> &catalogs = {}, QObject *parent = nullptr);

    // Видимые станции в порядке отображения.
    QList<Station> stations() const { return stations_; }
    // Скрытые станции каталогов — нужны экрану настроек, чтобы их вернуть.
    QList<Station> hiddenStations() const;
    std::optional<Station> find(const QString &stationId) const;
    // Правки пользователя как они есть — для проверок в тестах.
    const Overlay &overlay() const { return overlay_; }

    // Заменяет набор каталогов (после загрузки или обновления из сети).
    void setCatalogs(const QList<Catalog> &catalogs);

    // Скрывает станцию каталога. Свои станции скрывать нечем — их удаляют.
    void hide(const QString &stationId);
    void unhide(const QString &stationId);
    // Переименовывает станцию: свою — насовсем, из каталога — через оверлей.
    void rename(const QString &stationId, const QString &name);
    std::optional<Station> addCustom(const QString &name, const QString &url);
    void removeCustom(const QString &stationId);
    // Создаёт свою копию станции каталога и скрывает оригинал.
    //
    // Так пользователь получает право менять URL, не ломая мерж: копия живёт
    // в оверлее, а каталог продолжает обновляться сам по себе.
    std::optional<Station> duplicate(const QString &stationId);
    void setCustomUrl(const QString &stationId, const QString &url);
    // Задаёт порядок видимых станций (после перетаскивания в настройках).
    // Неполный список — признак рассинхрона интерфейса, он игнорируется.
    void setOrder(const QStringList &visibleOrder);
    void move(int from, int to);
    // Возвращает всё к состоянию каталогов: правки пользователя стираются.
    void reset();

signals:
    void stationsChanged();

private:
    void rebuild();
    QList<Station> catalogStations() const;
    std::optional<Station> catalogStation(const QString &stationId) const;
    Station applyRename(Station station) const;
    QStringList currentOrder() const;
    void commit();

    QList<Catalog> catalogs_;
    Overlay overlay_;
    QList<Station> stations_;
};

}  // namespace radio
