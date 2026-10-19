#include "storage.hpp"

#include "json_values.hpp"
#include "paths.hpp"

#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>

#include <cstdio>
#include <unistd.h>

using namespace Qt::StringLiterals;

namespace radio::storage {

QJsonValue readJson(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return QJsonValue::Undefined;
    QJsonParseError error{};
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &error);
    if (error.error != QJsonParseError::NoError)
        return QJsonValue::Undefined;
    if (document.isArray())
        return document.array();
    return document.object();
}

bool writeJson(const QString &path, const QJsonValue &data)
{
    const QByteArray bytes =
        data.isArray() ? QJsonDocument(data.toArray()).toJson() : QJsonDocument(data.toObject()).toJson();
    const QString tmp = path + u".tmp"_s;

    QFile file(tmp);
    const bool written = QDir().mkpath(QFileInfo(path).absolutePath()) && file.open(QIODevice::WriteOnly) &&
                         file.write(bytes) == bytes.size() && file.flush() && ::fsync(file.handle()) == 0;
    file.close();
    // rename(2), а не QFile::rename: тот отказывается заменять существующий файл,
    // а замена одним системным вызовом и делает запись атомарной.
    if (written && std::rename(QFile::encodeName(tmp).constData(), QFile::encodeName(path).constData()) == 0)
        return true;

    // Уборка сама может не пройти (каталога нет, прав нет) — но обещание
    // функции «вернуть false, а не бросить» важнее удалённого хвоста.
    QFile::remove(tmp);
    return false;
}

QJsonObject readDocument(const QString &path, int supportedSchema)
{
    const QJsonValue data = readJson(path);
    if (!data.isObject())
        return {};
    const QJsonObject document = data.toObject();

    const auto schema = json::integer(document.value("schema"_L1));
    if (schema && *schema > supportedSchema) {
        const QFileInfo info(path);
        const QString backup = info.path() + u'/' + info.completeBaseName() + u".bak-v%1"_s.arg(*schema);
        qWarning().noquote() << u"%1 создан более новой версией (schema %2) — отложен в %3"_s.arg(
            info.fileName(), QString::number(*schema), QFileInfo(backup).fileName());
        std::rename(QFile::encodeName(path).constData(), QFile::encodeName(backup).constData());
        return {};
    }
    return document;
}

QJsonObject loadState()
{
    return readJson(paths::stateFile()).toObject();
}

bool updateState(const QJsonObject &values)
{
    QJsonObject state = loadState();
    for (auto it = values.constBegin(); it != values.constEnd(); ++it)
        state.insert(it.key(), it.value());
    return writeJson(paths::stateFile(), state);
}

}  // namespace radio::storage
