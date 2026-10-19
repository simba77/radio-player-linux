#include "playlist_io.hpp"

#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMap>
#include <QRegularExpression>

using namespace Qt::StringLiterals;

namespace radio::playlist {

namespace {

// `#EXTINF:<секунды>,<название>` — название может содержать запятые.
const QRegularExpression EXTINF(u"^#EXTINF:[^,]*,(.*)$"_s, QRegularExpression::CaseInsensitiveOption);
// Строки `.pls`: `File1=...`, `Title1=...`.
const QRegularExpression PLS_ENTRY(u"^(File|Title)(\\d+)\\s*=\\s*(.*)$"_s,
                                   QRegularExpression::CaseInsensitiveOption);
const QRegularExpression LINE_BREAK(u"\\r\\n|\\r|\\n"_s);

QStringList lines(const QString &text)
{
    return text.split(LINE_BREAK);
}

// Запасное имя, когда в файле его не было.
QString nameFromUrl(const QString &url)
{
    const qsizetype scheme = url.indexOf("//"_L1);
    const QString rest = scheme >= 0 ? url.mid(scheme + 2) : url;
    const QString host = rest.section(u'/', 0, 0);
    return host.isEmpty() ? url : host;
}

}  // namespace

QList<Station> parse(const QString &text, const QString &filename)
{
    const QString stripped = text.trimmed();
    // `.pls` тоже начинается с квадратной скобки, поэтому проверяем его первым.
    if (stripped.startsWith("[playlist]"_L1, Qt::CaseInsensitive) ||
        filename.endsWith(".pls"_L1, Qt::CaseInsensitive))
        return parsePls(text);
    if (stripped.startsWith(u'[') || stripped.startsWith(u'{'))
        return parseJson(text);
    return parseM3u(text);
}

QList<Station> parseJson(const QString &text)
{
    QJsonParseError error{};
    const QJsonDocument document = QJsonDocument::fromJson(text.toUtf8(), &error);
    if (error.error != QJsonParseError::NoError)
        return {};
    const QJsonArray items =
        document.isObject() ? document.object().value("stations"_L1).toArray() : document.array();

    QList<Station> stations;
    for (const QJsonValue &raw : items) {
        // Идентификаторы из чужого файла не переносим — станция становится своей.
        if (const auto station = Station::fromJson(raw))
            stations.append(Station::local(station->name, station->url));
    }
    return stations;
}

QList<Station> parseM3u(const QString &text)
{
    QList<Station> stations;
    QString pendingName;

    for (const QString &raw : lines(text)) {
        const QString line = raw.trimmed();
        if (line.isEmpty())
            continue;
        if (line.startsWith(u'#')) {
            const QRegularExpressionMatch match = EXTINF.match(line);
            if (match.hasMatch())
                pendingName = match.captured(1).trimmed();
            continue;
        }
        if (!isPlayableUrl(line))
            continue;
        stations.append(Station::local(pendingName.isEmpty() ? nameFromUrl(line) : pendingName, line));
        pendingName.clear();
    }
    return stations;
}

QList<Station> parsePls(const QString &text)
{
    // QMap упорядочен по ключу — станции идут по номерам, как в файле.
    QMap<int, QString> urls;
    QMap<int, QString> titles;

    for (const QString &raw : lines(text)) {
        const QRegularExpressionMatch match = PLS_ENTRY.match(raw.trimmed());
        if (!match.hasMatch())
            continue;
        const int index = match.captured(2).toInt();
        const QString value = match.captured(3).trimmed();
        if (match.captured(1).compare("file"_L1, Qt::CaseInsensitive) == 0)
            urls.insert(index, value);
        else
            titles.insert(index, value);
    }

    QList<Station> stations;
    for (auto it = urls.cbegin(); it != urls.cend(); ++it) {
        if (!isPlayableUrl(it.value()))
            continue;
        const QString title = titles.value(it.key());
        stations.append(Station::local(title.isEmpty() ? nameFromUrl(it.value()) : title, it.value()));
    }
    return stations;
}

QString exportM3u(const QList<Station> &stations)
{
    QStringList out{u"#EXTM3U"_s};
    for (const Station &station : stations) {
        out.append(u"#EXTINF:-1,"_s + station.name);
        out.append(station.url);
    }
    return out.join(u'\n') + u'\n';
}

QList<Station> readFile(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    // Неверные байты UTF-8 заменяются, а не роняют разбор: в плейлисте из
    // чужой кодировки останутся адреса, даже если названия пострадают.
    return parse(QString::fromUtf8(file.readAll()), QFileInfo(path).fileName());
}

}  // namespace radio::playlist
