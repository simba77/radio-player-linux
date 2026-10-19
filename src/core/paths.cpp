#include "paths.hpp"

#include "app_info.hpp"

#include <QDir>

#include <unistd.h>

using namespace Qt::StringLiterals;

namespace radio::paths {

namespace {

// Корень по XDG: значение переменной или запасной каталог в домашнем.
QString xdgRoot(const char *variable, const QString &fallback)
{
    const QString raw = qEnvironmentVariable(variable);
    return raw.isEmpty() ? QDir::homePath() + u'/' + fallback : raw;
}

QString appDir(const char *variable, const QString &fallback)
{
    return xdgRoot(variable, fallback) + u'/' + QLatin1String(APP_ID);
}

QString autostartDir()
{
    return xdgRoot("XDG_CONFIG_HOME", u".config"_s) + u"/autostart"_s;
}

}  // namespace

QString configDir()
{
    return appDir("XDG_CONFIG_HOME", u".config"_s);
}

QString cacheDir()
{
    return appDir("XDG_CACHE_HOME", u".cache"_s);
}

QString settingsFile()
{
    return configDir() + u"/settings.json"_s;
}

QString overlayFile()
{
    return configDir() + u"/overlay.json"_s;
}

QString stateFile()
{
    return configDir() + u"/state.json"_s;
}

QString catalogCacheDir()
{
    return cacheDir() + u"/catalogs"_s;
}

QString artworkCacheDir()
{
    return cacheDir() + u"/artwork"_s;
}

QString ipcSocket()
{
    QString root = qEnvironmentVariable("XDG_RUNTIME_DIR");
    if (root.isEmpty())
        root = u"/tmp/%1-%2"_s.arg(QLatin1String(APP_ID)).arg(::getuid());
    QDir().mkpath(root);
    return root + u'/' + QLatin1String(APP_ID) + u".socket"_s;
}

QString autostartFile()
{
    return autostartDir() + u'/' + QLatin1String(APP_RDNS_ID) + u".desktop"_s;
}

QString legacyAutostartFile()
{
    return autostartDir() + u'/' + QLatin1String(APP_ID) + u".desktop"_s;
}

void ensureDirs()
{
    for (const QString &path : {configDir(), catalogCacheDir(), artworkCacheDir()})
        QDir().mkpath(path);
}

}  // namespace radio::paths
