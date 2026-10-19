#pragma once

// Аргументы командной строки и журнал в stderr.
//
// Ключи Qt (`-platform`, `-style`) разбирает сама `QApplication`, и перечислять
// их у себя нельзя — они меняются от версии к версии. Поэтому неизвестные
// аргументы не ошибка, а то, что достаётся Qt.

#include <QString>
#include <QStringList>
#include <QtGlobal>

namespace radio::cli {

struct Options {
    enum class Action { Run, Version, Help, Error };

    Action action = Action::Run;
    // Сколько писать в stderr: debug, info, warning или error.
    QString logLevel = QStringLiteral("info");
    // Что не так с аргументами, когда `action == Error`.
    QString error;
};

// Разбирает аргументы; первый — имя программы.
Options parse(const QStringList &arguments);

// `radio-player 1.0.0` — для `--version`.
QString versionText();
QString usage();

// Направляет сообщения Qt в stderr в виде `УРОВЕНЬ: текст`, отбрасывая те,
// что ниже заданного уровня.
//
// В stderr, а не в stdout: при запуске из `.desktop` вывод забирает journald,
// и диагностика не должна смешиваться с обычным выводом программы.
void installLogging(const QString &logLevel);

}  // namespace radio::cli
