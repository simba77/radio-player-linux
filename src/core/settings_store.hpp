#pragma once

// Настройки приложения как единый источник правды.
//
// Правка и сохранение — одна операция, а об изменении сообщает сигнал, поэтому
// забыть уведомить потребителя нечем: список каталогов, поправленный в окне
// настроек, сразу доходит до обновления каталогов и списка станций.
//
// Симметрично `StationStore`: тот владеет списком станций, этот — настройками.

#include "settings.hpp"

#include <QObject>

namespace radio {

class SettingsStore : public QObject {
    Q_OBJECT
public:
    // Настройки читаются с диска.
    explicit SettingsStore(QObject *parent = nullptr);
    explicit SettingsStore(const Settings &settings, QObject *parent = nullptr);

    bool closeToTray() const { return settings_.closeToTray; }
    int updateIntervalHours() const { return settings_.updateIntervalHours; }
    qint64 updateIntervalSeconds() const { return settings_.updateIntervalSeconds(); }
    QString language() const { return settings_.language; }
    // Копия: список настроек нельзя править в обход хранилища.
    QStringList extraCatalogUrls() const { return settings_.extraCatalogUrls; }

    void setCloseToTray(bool value);
    void setUpdateIntervalHours(int hours);
    void setLanguage(const QString &language);

    // Добавляет каталог. False — если такой уже есть или адрес не годится
    // (только HTTPS, см. `isAcceptableCatalogUrl`).
    bool addCatalogUrl(const QString &url);
    bool removeCatalogUrl(const QString &url);

signals:
    // Изменилась любая настройка.
    void changed();
    // Изменился список пользовательских каталогов.
    void catalogUrlsChanged();

private:
    void update(const Settings &next);
    void commit();

    Settings settings_;
};

}  // namespace radio
