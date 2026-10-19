#include "cli.hpp"

#include "app_info.hpp"

#include <algorithm>
#include <cstdio>

using namespace Qt::StringLiterals;

namespace radio::cli {

namespace {

const QStringList LOG_LEVELS{u"debug"_s, u"info"_s, u"warning"_s, u"error"_s};

int g_minimumRank = 1;

// Порядок важности. У QtMsgType он другой: QtInfoMsg объявлен последним.
int rank(QtMsgType type)
{
    switch (type) {
    case QtDebugMsg:
        return 0;
    case QtInfoMsg:
        return 1;
    case QtWarningMsg:
        return 2;
    case QtCriticalMsg:
    case QtFatalMsg:
        return 3;
    }
    return 3;
}

const char *label(QtMsgType type)
{
    switch (type) {
    case QtDebugMsg:
        return "DEBUG";
    case QtInfoMsg:
        return "INFO";
    case QtWarningMsg:
        return "WARNING";
    case QtCriticalMsg:
        return "ERROR";
    case QtFatalMsg:
        return "FATAL";
    }
    return "ERROR";
}

void writeMessage(QtMsgType type, const QMessageLogContext &, const QString &message)
{
    if (rank(type) < g_minimumRank && type != QtFatalMsg)
        return;
    std::fprintf(stderr, "%s: %s\n", label(type), message.toLocal8Bit().constData());
    std::fflush(stderr);
}

}  // namespace

Options parse(const QStringList &arguments)
{
    Options options;
    for (qsizetype index = 1; index < arguments.size(); ++index) {
        const QString &argument = arguments[index];
        if (argument == "--version"_L1) {
            options.action = Options::Action::Version;
            return options;
        }
        if (argument == "-h"_L1 || argument == "--help"_L1) {
            options.action = Options::Action::Help;
            return options;
        }
        QString level;
        if (argument == "--log-level"_L1) {
            if (++index >= arguments.size()) {
                options.action = Options::Action::Error;
                options.error = u"--log-level: не указан уровень"_s;
                return options;
            }
            level = arguments[index];
        } else if (argument.startsWith("--log-level="_L1)) {
            level = argument.section(u'=', 1);
        } else {
            continue;  // Аргумент Qt — его разберёт QApplication.
        }
        if (!LOG_LEVELS.contains(level)) {
            options.action = Options::Action::Error;
            options.error = u"--log-level: неизвестный уровень «%1», допустимо: %2"_s.arg(
                level, LOG_LEVELS.join(u", "_s));
            return options;
        }
        options.logLevel = level;
    }
    return options;
}

QString versionText()
{
    return QLatin1String(APP_ID) + u' ' + QLatin1String(APP_VERSION);
}

QString usage()
{
    return uR"(Использование: radio-player [--log-level УРОВЕНЬ] [--version] [-h]

Плеер интернет-радио: трей, MPRIS, HLS и прямые потоки.

  --log-level УРОВЕНЬ  сколько писать в stderr: debug, info, warning, error
                       (по умолчанию info)
  --version            показать версию и выйти
  -h, --help           показать эту справку и выйти

Остальные аргументы достаются Qt (например, -platform offscreen).
)"_s;
}

void installLogging(const QString &logLevel)
{
    g_minimumRank = int(std::max<qsizetype>(0, LOG_LEVELS.indexOf(logLevel)));
    qInstallMessageHandler(writeMessage);
}

}  // namespace radio::cli
