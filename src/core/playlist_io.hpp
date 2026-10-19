#pragma once

// Импорт и экспорт списков станций.
//
// Поддерживаются три формата на входе: JSON macOS-версии, `.m3u`/`.m3u8` и `.pls`.
// Все импортированные станции становятся своими (`local:`), даже если в исходнике
// были идентификаторы: чужие id не должны притворяться станциями каталога.

#include "models.hpp"

#include <QList>
#include <QString>

namespace radio::playlist {

// Разбирает список станций, определяя формат по содержимому и имени файла.
QList<Station> parse(const QString &text, const QString &filename = {});

// JSON macOS-версии: массив `{id, name, urlString}`.
QList<Station> parseJson(const QString &text);
QList<Station> parseM3u(const QString &text);
QList<Station> parsePls(const QString &text);

QString exportM3u(const QList<Station> &stations);

// Станции из файла; пусто, если файл не читается.
QList<Station> readFile(const QString &path);

}  // namespace radio::playlist
