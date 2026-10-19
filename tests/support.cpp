#include "support.hpp"

#include <QDeadlineTimer>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMetaObject>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>

#include <catch2/catch_test_case_info.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/reporters/catch_reporter_event_listener.hpp>
#include <catch2/reporters/catch_reporter_registrars.hpp>

#include <memory>

namespace radio::test {

namespace {

std::unique_ptr<QTemporaryDir> g_dir;

// Свежие XDG-каталоги на каждый прогон теста — аналог autouse-фикстуры.
class IsolatedXdg : public Catch::EventListenerBase {
public:
    using EventListenerBase::EventListenerBase;

    void testCasePartialStarting(const Catch::TestCaseInfo &, uint64_t) override
    {
        g_dir = std::make_unique<QTemporaryDir>();
        const QString root = g_dir->path();
        qputenv("XDG_CONFIG_HOME", QFile::encodeName(root + QStringLiteral("/config")));
        qputenv("XDG_CACHE_HOME", QFile::encodeName(root + QStringLiteral("/cache")));
        QDir().mkpath(root + QStringLiteral("/run"));
        qputenv("XDG_RUNTIME_DIR", QFile::encodeName(root + QStringLiteral("/run")));
    }

    void testCasePartialEnded(const Catch::TestCaseStats &, uint64_t) override { g_dir.reset(); }
};

}  // namespace

QString tempDir()
{
    return g_dir ? g_dir->path() : QString();
}

QString sourcePath(const QString &relative)
{
    return QStringLiteral(RADIO_SOURCE_DIR) + u'/' + relative;
}

QByteArray readFile(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return file.readAll();
}

void writeFile(const QString &path, const QByteArray &data)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    REQUIRE(file.open(QIODevice::WriteOnly));
    file.write(data);
}

QTimer *timer(const QObject *owner, const QString &name)
{
    auto *found = owner->findChild<QTimer *>(name);
    REQUIRE(found != nullptr);
    return found;
}

void fire(QTimer *timer)
{
    REQUIRE(QMetaObject::invokeMethod(timer, "timeout", Qt::DirectConnection));
}

bool waitUntil(const std::function<bool()> &condition, int timeoutMs)
{
    const QDeadlineTimer deadline(timeoutMs);
    while (!condition()) {
        if (deadline.hasExpired())
            return false;
        QTest::qWait(10);
    }
    return true;
}

}  // namespace radio::test

CATCH_REGISTER_LISTENER(radio::test::IsolatedXdg)
