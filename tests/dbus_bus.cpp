#include "dbus_bus.hpp"

#include <QStandardPaths>
#include <QTimer>

namespace radio::test {

namespace {

// Конфигурация без каталогов служб: стандартная сеансовая умеет запускать
// службы по первому обращению (AT-SPI, порталы), и тест поднял бы настоящие
// процессы пользователя на своей шине.
const QByteArray CONFIG = R"(<!DOCTYPE busconfig PUBLIC "-//freedesktop//DTD D-Bus Bus Configuration 1.0//EN"
 "http://www.freedesktop.org/standards/dbus/1.0/busconfig.dtd">
<busconfig>
  <type>session</type>
  <listen>unix:tmpdir=/tmp</listen>
  <auth>EXTERNAL</auth>
  <policy context="default">
    <allow send_destination="*" eavesdrop="true"/>
    <allow eavesdrop="true"/>
    <allow own="*"/>
  </policy>
</busconfig>
)";

}  // namespace

PrivateBus::PrivateBus()
{
    if (QStandardPaths::findExecutable(QStringLiteral("dbus-daemon")).isEmpty() || !config_.open())
        return;
    config_.write(CONFIG);
    config_.flush();
    daemon_.start(QStringLiteral("dbus-daemon"),
                  {QStringLiteral("--config-file=") + config_.fileName(), QStringLiteral("--nofork"),
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

bool FakeInhibitPortal::registerOn(QDBusConnection connection)
{
    connection_ = connection;
    return connection.registerObject(QStringLiteral("/org/freedesktop/portal/desktop"), this,
                                     QDBusConnection::ExportAllSlots) &&
           connection.registerService(QStringLiteral("org.freedesktop.portal.Desktop"));
}

QDBusObjectPath FakeInhibitPortal::Inhibit(const QString &window, uint flags, const QVariantMap &options)
{
    calls.append({window, flags, options});
    const QString path = QStringLiteral("/org/freedesktop/portal/desktop/request/test/%1").arg(calls.size());
    auto exportRequest = [this, path] {
        connection_.registerObject(path, new FakePortalRequest(this, path), QDBusConnection::ExportAllSlots);
    };
    if (requestDelayMs > 0)
        QTimer::singleShot(requestDelayMs, this, exportRequest);
    else
        exportRequest();
    return QDBusObjectPath(path);
}

void FakePortalRequest::Close()
{
    portal_->closed.append(path_);
    deleteLater();
}

}  // namespace radio::test
