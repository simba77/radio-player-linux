#pragma once

// Мониторинг доступности сети.
//
// Бэкенд `QNetworkInformation` есть не в каждой сборке Qt и не в каждой среде,
// поэтому его отсутствие не должно ничего ломать: без него остаётся обычный
// цикл переподключения, просто без мгновенной реакции на возврат сети.

namespace radio {

class PlayerManager;

// Подписывает плеер на изменения доступности сети. False — если бэкенда нет.
bool installNetworkMonitor(PlayerManager *player);

}  // namespace radio
