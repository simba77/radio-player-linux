#include "dbus_bus.hpp"

#include <QStandardPaths>

namespace radio::test {

PrivateBus::PrivateBus()
{
    if (QStandardPaths::findExecutable(QStringLiteral("dbus-daemon")).isEmpty())
        return;
    daemon_.start(QStringLiteral("dbus-daemon"), {QStringLiteral("--session"), QStringLiteral("--nofork"),
                                                  QStringLiteral("--print-address")});
    if (!daemon_.waitForStarted(5000))
        return;
    while (!daemon_.canReadLine() && daemon_.waitForReadyRead(5000)) {
    }
    address_ = QString::fromUtf8(daemon_.readLine()).trimmed();
}

PrivateBus::~PrivateBus()
{
    for (const QString &name : std::as_const(names_))
        QDBusConnection::disconnectFromBus(name);
    if (daemon_.state() != QProcess::NotRunning) {
        daemon_.terminate();
        if (!daemon_.waitForFinished(3000))
            daemon_.kill();
    }
}

QDBusConnection PrivateBus::connect()
{
    static int counter = 0;
    const QString name = QStringLiteral("radio-test-bus-%1").arg(++counter);
    names_.append(name);
    return QDBusConnection::connectToBus(address_, name);
}

bool FakePowerManagement::registerOn(QDBusConnection connection)
{
    return connection.registerObject(QStringLiteral("/org/freedesktop/PowerManagement/Inhibit"), this,
                                     QDBusConnection::ExportAllSlots) &&
           connection.registerService(QStringLiteral("org.freedesktop.PowerManagement"));
}

uint FakePowerManagement::Inhibit(const QString &, const QString &)
{
    ++inhibited;
    return nextCookie++;
}

void FakePowerManagement::UnInhibit(uint cookie)
{
    released.append(cookie);
}

}  // namespace radio::test
