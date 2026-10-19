#include "settings_store.hpp"

namespace radio {

SettingsStore::SettingsStore(QObject *parent) : SettingsStore(loadSettings(), parent) {}

SettingsStore::SettingsStore(const Settings &settings, QObject *parent) : QObject(parent), settings_(settings)
{
}

void SettingsStore::setCloseToTray(bool value)
{
    Settings next = settings_;
    next.closeToTray = value;
    update(next);
}

void SettingsStore::setUpdateIntervalHours(int hours)
{
    Settings next = settings_;
    next.updateIntervalHours = hours;
    update(next);
}

void SettingsStore::setLanguage(const QString &language)
{
    Settings next = settings_;
    next.language = language;
    update(next);
}

bool SettingsStore::addCatalogUrl(const QString &url)
{
    const QString cleaned = url.trimmed();
    if (!isAcceptableCatalogUrl(cleaned) || settings_.extraCatalogUrls.contains(cleaned))
        return false;
    settings_.extraCatalogUrls.append(cleaned);
    commit();
    emit catalogUrlsChanged();
    return true;
}

bool SettingsStore::removeCatalogUrl(const QString &url)
{
    if (!settings_.extraCatalogUrls.removeOne(url))
        return false;
    commit();
    emit catalogUrlsChanged();
    return true;
}

void SettingsStore::update(const Settings &next)
{
    // Настройки приходят из виджетов, а те шлют сигнал и когда значение
    // не изменилось: без этой проверки каждый показ окна писал бы файл.
    if (next == settings_)
        return;
    settings_ = next;
    commit();
    emit changed();
}

void SettingsStore::commit()
{
    // Значения нормализуются тем же кодом, что и при чтении с диска:
    // интервал обновления, например, ограничен разумными рамками.
    settings_ = Settings::fromJson(settings_.toJson());
    saveSettings(settings_);
}

}  // namespace radio
