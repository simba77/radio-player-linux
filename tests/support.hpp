#pragma once

// Общее для тестов: временный каталог теста, файлы и печать типов Qt в отчётах.
//
// Тесты не должны трогать настоящие `~/.config` и `~/.cache`, поэтому каждому
// тесту слушатель из support.cpp выдаёт свежий временный каталог и направляет
// в него `XDG_CONFIG_HOME`, `XDG_CACHE_HOME` и `XDG_RUNTIME_DIR` — последний,
// чтобы тест не постучался в сокет живого приложения пользователя.

#include "models.hpp"

#include <QByteArray>
#include <QString>

#include <catch2/catch_tostring.hpp>

#include <functional>

class QObject;
class QTimer;

namespace radio::test {

// Временный каталог текущего теста. Удаляется после теста.
QString tempDir();
// Путь внутри исходников проекта.
QString sourcePath(const QString &relative);

QByteArray readFile(const QString &path);
// Пишет файл, создавая недостающие каталоги.
void writeFile(const QString &path, const QByteArray &data);

// Таймер объекта по имени (`objectName`).
QTimer *timer(const QObject *owner, const QString &name);
// Имитирует срабатывание таймера: настоящие интервалы измеряются секундами,
// поэтому проверяется факт, что таймер взведён, а срабатывание вызывается руками.
void fire(QTimer *timer);
// Крутит цикл событий, пока условие не выполнится; false — если вышло время.
bool waitUntil(const std::function<bool()> &condition, int timeoutMs = 5000);

}  // namespace radio::test

template <>
struct Catch::StringMaker<QString> {
    static std::string convert(const QString &value) { return '"' + value.toStdString() + '"'; }
};

template <>
struct Catch::StringMaker<radio::Station> {
    static std::string convert(const radio::Station &station)
    {
        return "Station{" + station.id.toStdString() + ", " + station.name.toStdString() + ", " +
               station.url.toStdString() + "}";
    }
};
