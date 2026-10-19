#include "autostart.hpp"

#include "app_info.hpp"
#include "paths.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QStandardPaths>

using namespace Qt::StringLiterals;

namespace radio::autostart {

namespace {

// Локализованные поля `.desktop` — часть формата, а не строки интерфейса:
// среда сама выбирает нужный вариант по своей локали.
const QString TEMPLATE = uR"([Desktop Entry]
Type=Application
Name=%1
Comment=Internet radio player
Comment[ru]=Плеер интернет-радио
Exec=%2
Icon=%3
Terminal=false
Categories=AudioVideo;Audio;Player;
X-GNOME-Autostart-enabled=true
)"_s;

// Аргумент `.desktop`-файла: пробелы допустимы только внутри кавычек.
QString quoted(const QString &value)
{
    return value.contains(u' ') ? u'"' + value + u'"' : value;
}

}  // namespace

bool isEnabled()
{
    // Файл под старым именем тоже включает автозапуск: сессия читает каталог
    // целиком и не знает, как файл назывался в прошлой версии.
    return QFile::exists(paths::autostartFile()) || QFile::exists(paths::legacyAutostartFile());
}

bool setEnabled(bool enabled)
{
    const QString path = paths::autostartFile();
    if (!enabled) {
        QFile::remove(path);
        QFile::remove(paths::legacyAutostartFile());
        return isEnabled();
    }

    QSaveFile file(path);
    const bool written =
        QDir().mkpath(QFileInfo(path).absolutePath()) && file.open(QIODevice::WriteOnly) &&
        file.write(TEMPLATE.arg(QLatin1String(APP_NAME), execLine(), QLatin1String(APP_RDNS_ID)).toUtf8()) >=
            0 &&
        file.commit();
    if (!written)
        return isEnabled();
    // Иначе приложение запускалось бы дважды: старый файл никуда не делся.
    QFile::remove(paths::legacyAutostartFile());
    return true;
}

QString execLine()
{
    const QString installed = QStandardPaths::findExecutable(QLatin1String(APP_ID));
    return quoted(installed.isEmpty() ? QCoreApplication::applicationFilePath() : installed);
}

}  // namespace radio::autostart
