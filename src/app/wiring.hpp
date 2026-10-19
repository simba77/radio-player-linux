#pragma once

// Связки между подсистемами, которые живут в точке входа.
//
// `main()` целиком в тестах не запускается — ему нужны окно, трей и шина, — но
// ошибка в связке видна только так: список каталогов правился в настройках,
// а обновление ходило по списку, снятому при запуске.

namespace radio {

class CatalogUpdater;
class SettingsStore;
class StationStore;

// Связывает список каталогов с теми, кто на него опирается.
//
// Правится он в настройках, а нужен и обновлению, и списку станций — причём
// сразу: добавленный каталог должен заработать, а удалённый перестать
// поставлять станции, не дожидаясь перезапуска.
void wireCatalogSources(SettingsStore *settings, CatalogUpdater *updater, StationStore *store);

// Прошёл ли с последнего обновления каталогов заданный в настройках интервал.
bool catalogUpdateDue(const SettingsStore &settings);

// Обновляет каталоги в фоне, если пора. Ждёт пару секунд после запуска,
// чтобы не тормозить его.
void scheduleCatalogUpdate(CatalogUpdater *updater, const SettingsStore &settings);

}  // namespace radio
