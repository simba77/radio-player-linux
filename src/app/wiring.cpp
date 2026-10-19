#include "wiring.hpp"

#include "catalog.hpp"
#include "settings_store.hpp"
#include "station_store.hpp"

#include <QTimer>

namespace radio {

namespace {

// Задержка перед фоновым обновлением каталогов, чтобы не тормозить запуск.
constexpr int CATALOG_UPDATE_DELAY_MS = 2000;

}  // namespace

void wireCatalogSources(SettingsStore *settings, CatalogUpdater *updater, StationStore *store)
{
    QObject::connect(settings, &SettingsStore::catalogUrlsChanged, store, [settings, updater, store] {
        updater->setExtraUrls(settings->extraCatalogUrls());
        store->setCatalogs(catalog::loadInitial(updater->urls()));
    });
}

bool catalogUpdateDue(const SettingsStore &settings)
{
    const auto elapsed = catalog::secondsSinceUpdate();
    return !elapsed || *elapsed >= double(settings.updateIntervalSeconds());
}

void scheduleCatalogUpdate(CatalogUpdater *updater, const SettingsStore &settings)
{
    if (catalogUpdateDue(settings))
        QTimer::singleShot(CATALOG_UPDATE_DELAY_MS, updater, &CatalogUpdater::updateNow);
}

}  // namespace radio
