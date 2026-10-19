// Аргументы командной строки.
//
// Ключи Qt (`-platform`, `-style`) разбирает сама `QApplication`, поэтому
// проверяется ровно одно: свои аргументы разобраны, чужие ошибкой не считаются.

#include "app_info.hpp"
#include "cli.hpp"
#include "support.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace radio;
using namespace Qt::StringLiterals;
using Action = cli::Options::Action;

TEST_CASE("defaults")
{
    const cli::Options options = cli::parse({u"radio-player"_s});
    CHECK(options.action == Action::Run);
    CHECK(options.logLevel == "info"_L1);
}

TEST_CASE("the log level is read")
{
    CHECK(cli::parse({u"radio-player"_s, u"--log-level"_s, u"debug"_s}).logLevel == "debug"_L1);
    CHECK(cli::parse({u"radio-player"_s, u"--log-level=warning"_s}).logLevel == "warning"_L1);
}

TEST_CASE("an unknown log level is rejected")
{
    const cli::Options options = cli::parse({u"radio-player"_s, u"--log-level"_s, u"громко"_s});
    CHECK(options.action == Action::Error);
    CHECK(options.error.contains(u"громко"_s));
}

TEST_CASE("a missing log level is rejected")
{
    CHECK(cli::parse({u"radio-player"_s, u"--log-level"_s}).action == Action::Error);
}

TEST_CASE("version and help are recognised")
{
    // Пакетчики и авторы баг-репортов ожидают `--version` по умолчанию.
    CHECK(cli::parse({u"radio-player"_s, u"--version"_s}).action == Action::Version);
    CHECK(cli::parse({u"radio-player"_s, u"-h"_s}).action == Action::Help);
    CHECK(cli::versionText() == u"radio-player "_s + QLatin1String(APP_VERSION));
}

TEST_CASE("Qt arguments are left alone")
{
    const cli::Options options =
        cli::parse({u"radio-player"_s, u"--log-level"_s, u"error"_s, u"-platform"_s, u"offscreen"_s});
    CHECK(options.action == Action::Run);
    CHECK(options.logLevel == "error"_L1);
}
