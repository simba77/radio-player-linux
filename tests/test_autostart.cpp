// Автозапуск при входе в систему.
//
// Отдельная тема от настроек: состояние живёт не в `settings.json`, а в наличии
// `.desktop`-файла — второго источника правды здесь нет намеренно.

#include "autostart.hpp"
#include "paths.hpp"
#include "support.hpp"

#include <QCoreApplication>
#include <QFile>

#include <catch2/catch_test_macros.hpp>

using namespace radio;
using namespace Qt::StringLiterals;

namespace {

// PATH на время теста: установленная копия приложения у разработчика
// не должна влиять на результат.
class PathGuard {
public:
    explicit PathGuard(const QString &path) : old_(qgetenv("PATH"))
    {
        qputenv("PATH", QFile::encodeName(path));
    }
    ~PathGuard() { qputenv("PATH", old_); }

private:
    QByteArray old_;
};

QString execLineInFile()
{
    autostart::setEnabled(true);
    for (const QString &line : QString::fromUtf8(test::readFile(paths::autostartFile())).split(u'\n')) {
        if (line.startsWith("Exec="_L1))
            return line;
    }
    return {};
}

void enableLegacyEntry()
{
    // Автозапуск, включённый версией до переименования в reverse-DNS.
    test::writeFile(paths::legacyAutostartFile(), "[Desktop Entry]\n");
}

}  // namespace

TEST_CASE("autostart is disabled by default")
{
    CHECK_FALSE(autostart::isEnabled());
}

TEST_CASE("autostart creates and removes the desktop file")
{
    CHECK(autostart::setEnabled(true));
    CHECK(autostart::isEnabled());
    CHECK_FALSE(autostart::setEnabled(false));
    CHECK_FALSE(QFile::exists(paths::autostartFile()));
}

TEST_CASE("autostart prefers the installed launcher")
{
    // Если приложение установлено, автозапуск должен звать его, а не каталог сборки.
    const QString bin = test::tempDir() + u"/bin"_s;
    test::writeFile(bin + u"/radio-player"_s, "#!/bin/sh\n");
    QFile::setPermissions(bin + u"/radio-player"_s, QFile::ReadOwner | QFile::ExeOwner);
    PathGuard path(bin);
    CHECK(execLineInFile() == u"Exec="_s + bin + u"/radio-player"_s);
}

TEST_CASE("without an installed launcher autostart runs this program")
{
    PathGuard path(test::tempDir() + u"/empty-bin"_s);
    CHECK(execLineInFile() == u"Exec="_s + QCoreApplication::applicationFilePath());
}

TEST_CASE("autostart uses the application icon")
{
    autostart::setEnabled(true);
    CHECK(test::readFile(paths::autostartFile()).contains("\nIcon=io.github.simba77.RadioPlayer\n"));
}

// MARK: - Переименование в reverse-DNS

TEST_CASE("a legacy entry still counts as enabled")
{
    // Сессия читает каталог целиком, поэтому старый файл продолжает работать.
    enableLegacyEntry();
    CHECK(autostart::isEnabled());
}

TEST_CASE("enabling replaces the legacy entry")
{
    enableLegacyEntry();
    autostart::setEnabled(true);
    CHECK(QFile::exists(paths::autostartFile()));
    // Иначе приложение запускалось бы дважды.
    CHECK_FALSE(QFile::exists(paths::legacyAutostartFile()));
}

TEST_CASE("disabling removes the legacy entry")
{
    enableLegacyEntry();
    CHECK_FALSE(autostart::setEnabled(false));
    CHECK_FALSE(autostart::isEnabled());
    CHECK_FALSE(QFile::exists(paths::legacyAutostartFile()));
}
