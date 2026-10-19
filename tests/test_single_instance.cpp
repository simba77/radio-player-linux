// Протокол повторного запуска.
//
// Второй запуск не открывает второе окно, а просит первый показать своё — и
// передаёт токен активации. На Wayland без него композитор фокус не отдаст,
// так что «показали окно» превратилось бы в мигание в панели задач.

#include "paths.hpp"
#include "single_instance.hpp"
#include "support.hpp"

#include <QFileInfo>
#include <QSignalSpy>

#include <catch2/catch_test_macros.hpp>

#include <unistd.h>

using namespace radio;
using namespace Qt::StringLiterals;

namespace {

// Токен активации — не секрет: композитор выдаёт его запускаемому процессу.
const QString TOKEN = u"wayland-token-abc123"_s;

// Переменная окружения на время теста.
class EnvGuard {
public:
    explicit EnvGuard(const char *name)
        : name_(name), had_(qEnvironmentVariableIsSet(name)), old_(qgetenv(name))
    {
    }
    ~EnvGuard()
    {
        if (had_)
            qputenv(name_, old_);
        else
            qunsetenv(name_);
    }

private:
    const char *name_;
    bool had_;
    QByteArray old_;
};

}  // namespace

TEST_CASE("the request carries the token")
{
    CHECK(instance::decodeRequest(instance::encodeRequest(TOKEN)) == TOKEN);
}

TEST_CASE("a request without a token is still valid")
{
    // Запуск из терминала токена не получает — окно всё равно надо показать.
    CHECK(instance::decodeRequest(instance::encodeRequest({})) == QString());
}

TEST_CASE("foreign data is ignored")
{
    CHECK_FALSE(instance::decodeRequest(QByteArray("\x00\x01whatever\n", 11)));
}

TEST_CASE("the token is read from the environment")
{
    EnvGuard wayland("XDG_ACTIVATION_TOKEN");
    qputenv("XDG_ACTIVATION_TOKEN", TOKEN.toUtf8());
    CHECK(instance::activationToken() == TOKEN);
}

TEST_CASE("the X11 startup id is accepted too")
{
    EnvGuard wayland("XDG_ACTIVATION_TOKEN");
    EnvGuard x11("DESKTOP_STARTUP_ID");
    qunsetenv("XDG_ACTIVATION_TOKEN");
    qputenv("DESKTOP_STARTUP_ID", "startup-id-1");
    CHECK(instance::activationToken() == "startup-id-1"_L1);
}

TEST_CASE("there is no token outside a session")
{
    EnvGuard wayland("XDG_ACTIVATION_TOKEN");
    EnvGuard x11("DESKTOP_STARTUP_ID");
    qunsetenv("XDG_ACTIVATION_TOKEN");
    qunsetenv("DESKTOP_STARTUP_ID");
    CHECK(instance::activationToken().isEmpty());
}

TEST_CASE("a second launch hands the token to the first")
{
    // Полный круг: сервер слушает, второй экземпляр стучится, токен доходит.
    EnvGuard wayland("XDG_ACTIVATION_TOKEN");
    qputenv("XDG_ACTIVATION_TOKEN", TOKEN.toUtf8());

    InstanceServer server;
    QSignalSpy shown(&server, &InstanceServer::showRequested);
    CHECK(instance::notifyRunningInstance());
    REQUIRE(test::waitUntil([&] { return !shown.isEmpty(); }, 2000));
    CHECK(shown[0][0].toString() == TOKEN);
    server.close();
}

TEST_CASE("nothing answers when no instance runs")
{
    CHECK_FALSE(instance::notifyRunningInstance());
}

TEST_CASE("the socket lives in the runtime directory")
{
    // В общем /tmp сокет был бы один на всю машину: второй пользователь,
    // запустив приложение, поднимал бы окно первому.
    CHECK(QFileInfo(paths::ipcSocket()).path() == qEnvironmentVariable("XDG_RUNTIME_DIR"));
}

TEST_CASE("the socket falls back per user")
{
    EnvGuard runtime("XDG_RUNTIME_DIR");
    qunsetenv("XDG_RUNTIME_DIR");
    CHECK(paths::ipcSocket().contains(QString::number(::getuid())));
}
