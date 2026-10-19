#pragma once

// Автозапуск при входе в систему: `.desktop`-файл в `~/.config/autostart`.
//
// Состояние живёт не в `settings.json`, а в наличии самого файла — второго
// источника правды здесь нет намеренно.

#include <QString>

namespace radio::autostart {

bool isEnabled();
// Включает или выключает автозапуск. Возвращает получившееся состояние.
bool setEnabled(bool enabled);
// Команда запуска для записи в файл: установленная команда из PATH, а если её
// нет (запуск из каталога сборки) — путь к текущей программе.
QString execLine();

}  // namespace radio::autostart
