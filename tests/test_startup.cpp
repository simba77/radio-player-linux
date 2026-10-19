// Запуск приложения целиком.
//
// Модульные тесты собирают части по отдельности, а `main()` связывает их вместе —
// и ошибка в этой связке видна только запуском. Здесь программа стартует
// настоящим процессом под offscreen-платформой, со своими XDG-каталогами и своей
// шиной D-Bus, и завершается по SIGTERM: сеанс при выходе присылает именно его.

#include "app_info.hpp"
#include "dbus_bus.hpp"
#include "mpris.hpp"
#include "support.hpp"

#include <QDBusConnectionInterface>
#include <QDir>
#include <QProcess>
#include <QTest>

#include <catch2/catch_test_macros.hpp>

#include <csignal>
#include <memory>

using namespace radio;
using namespace Qt::StringLiterals;

namespace {

// Шина, которой нет: без неё приложение обязано работать, просто без MPRIS.
const QString NO_BUS = u"unix:path=/nonexistent/radio-player-test"_s;

std::unique_ptr<QProcess> launch(const QString &busAddress,
                                 const QStringList &arguments = {u"--log-level"_s, u"debug"_s})
{
    // XDG-каталоги теста процесс наследует: их уже выставил слушатель тестов,
    // в том числе свой XDG_RUNTIME_DIR — иначе тест постучался бы в сокет живого
    // приложения пользователя и попросил его показать окно.
    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    environment.insert(u"QT_QPA_PLATFORM"_s, u"offscreen"_s);
    environment.insert(u"DBUS_SESSION_BUS_ADDRESS"_s, busAddress);
    auto process = std::make_unique<QProcess>();
    process->setProcessEnvironment(environment);
    process->setProcessChannelMode(QProcess::MergedChannels);
    process->start(QStringLiteral(RADIO_APP_BINARY), arguments);
    REQUIRE(process->waitForStarted(5000));
    return process;
}

// Признак жизни — каталоги, которые приложение создаёт сразу после старта:
// дожидаться их надёжнее, чем спать наугад.
void waitUntilRunning(QProcess &process)
{
    const QString marker = test::tempDir() + u"/cache/radio-player/catalogs"_s;
    const bool started = test::waitUntil(
        [&] { return QDir(marker).exists() || process.state() == QProcess::NotRunning; }, 15'000);
    INFO(process.readAll().toStdString());
    REQUIRE(started);
    REQUIRE(process.state() == QProcess::Running);
}

void terminate(QProcess &process)
{
    if (process.state() == QProcess::NotRunning)
        return;
    ::kill(pid_t(process.processId()), SIGTERM);
    if (!process.waitForFinished(10'000))
        process.kill();
}

}  // namespace

TEST_CASE("the application starts and stops on SIGTERM")
{
    const auto process = launch(NO_BUS);
    waitUntilRunning(*process);
    // Даём циклу событий поработать: часть инициализации идёт по таймерам.
    QTest::qWait(1000);
    REQUIRE(process->state() == QProcess::Running);

    // SIGTERM обязан приводить к штатному выходу, иначе имя на шине и mpv
    // остаются на совести ядра.
    ::kill(pid_t(process->processId()), SIGTERM);
    REQUIRE(process->waitForFinished(10'000));
    INFO(process->readAll().toStdString());
    CHECK(process->exitStatus() == QProcess::NormalExit);
    CHECK(process->exitCode() == 0);
}

TEST_CASE("the configuration directories are created")
{
    const auto process = launch(NO_BUS);
    waitUntilRunning(*process);
    CHECK(QDir(test::tempDir() + u"/config/radio-player"_s).exists());
    terminate(*process);
}

TEST_CASE("a second launch exits without a second window")
{
    const auto first = launch(NO_BUS);
    waitUntilRunning(*first);
    QTest::qWait(500);

    const auto second = launch(NO_BUS);
    REQUIRE(second->waitForFinished(15'000));
    CHECK(second->exitCode() == 0);
    CHECK(first->state() == QProcess::Running);
    terminate(*first);
}

TEST_CASE("the player appears on the session bus")
{
    test::PrivateBus bus;
    if (!bus.available())
        SKIP("dbus-daemon недоступен");
    const auto process = launch(bus.address());
    waitUntilRunning(*process);

    const QDBusConnection client = bus.connect();
    CHECK(test::waitUntil([&] { return client.interface()->isServiceRegistered(mprisBusName()).value(); }));

    terminate(*process);
    // Выход по SIGTERM снимает имя с шины сам, не дожидаясь смерти соединения.
    CHECK(process->exitStatus() == QProcess::NormalExit);
    CHECK(process->exitCode() == 0);
    CHECK_FALSE(client.interface()->isServiceRegistered(mprisBusName()).value());
}

TEST_CASE("the version is printed")
{
    QProcess process;
    process.start(QStringLiteral(RADIO_APP_BINARY), {u"--version"_s});
    REQUIRE(process.waitForFinished(10'000));
    CHECK(process.exitCode() == 0);
    CHECK(QString::fromUtf8(process.readAllStandardOutput()).trimmed() ==
          QLatin1String(APP_ID) + u' ' + QLatin1String(APP_VERSION));
}

TEST_CASE("a bad log level is refused with a usage error")
{
    QProcess process;
    process.start(QStringLiteral(RADIO_APP_BINARY), {u"--log-level"_s, u"громко"_s});
    REQUIRE(process.waitForFinished(10'000));
    CHECK(process.exitCode() == 2);
}
