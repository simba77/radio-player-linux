#pragma once

// Собственная шина D-Bus для тестов.
//
// Сеансовую шину пользователя трогать нельзя: на ней живёт настоящее
// приложение, и тест занял бы его имя или показал бы лишний плеер в апплете.
// Поэтому поднимается отдельный `dbus-daemon`, а сервер и клиент получают
// к нему разные соединения — так сообщение действительно проходит через шину
// и маршалится, как для plasmashell.

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusObjectPath>
#include <QList>
#include <QObject>
#include <QProcess>
#include <QStringList>
#include <QTemporaryFile>
#include <QVariantMap>

namespace radio::test {

class PrivateBus {
public:
    PrivateBus();
    ~PrivateBus();
    PrivateBus(const PrivateBus &) = delete;
    PrivateBus &operator=(const PrivateBus &) = delete;

    // Удалось ли поднять шину. Без `dbus-daemon` тест пропускается.
    bool available() const { return !address_.isEmpty(); }
    // Адрес шины — для DBUS_SESSION_BUS_ADDRESS запускаемой программы.
    QString address() const { return address_; }
    // Новое независимое соединение с шиной.
    QDBusConnection connect();

private:
    QTemporaryFile config_;
    QProcess daemon_;
    QString address_;
    QStringList names_;
};

// Записывает принятые сигналы целиком, с сигнатурой.
class SignalRecorder : public QObject {
    Q_OBJECT
public:
    QList<QDBusMessage> messages;

public slots:
    void record(const QDBusMessage &message) { messages.append(message); }
};

// Двойник службы питания (`org.freedesktop.PowerManagement.Inhibit`).
//
// Методы объявлены с теми же типами, что у PowerDevil: `UnInhibit` с другой
// сигнатурой шина до него просто не доставит — ровно как настоящая служба.
class FakePowerManagement : public QObject {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.freedesktop.PowerManagement.Inhibit")
public:
    int inhibited = 0;
    QList<uint> released;
    uint nextCookie = 7;

    // Регистрирует службу на соединении.
    bool registerOn(QDBusConnection connection);

public slots:
    uint Inhibit(const QString &application, const QString &reason);
    void UnInhibit(uint cookie);
};

// Двойник портала запрета засыпания (`org.freedesktop.portal.Inhibit`).
//
// Метод объявлен с сигнатурой портала, `sua{sv}`: флаги не тем типом шина до
// него не доставит. Каждая блокировка — свой объект запроса, и снимается она
// закрытием этого объекта, как у настоящего портала.
class FakeInhibitPortal : public QObject {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.freedesktop.portal.Inhibit")
public:
    struct Call {
        QString window;
        uint flags = 0;
        QVariantMap options;
    };
    QList<Call> calls;
    // Пути закрытых запросов — снятые блокировки.
    QStringList closed;
    // Через сколько после ответа появляется объект запроса. У настоящего портала
    // ответ обгоняет объект, и Close, пришедший сразу, получает UnknownObject.
    int requestDelayMs = 0;

    // Регистрирует службу на соединении.
    bool registerOn(QDBusConnection connection);

public slots:
    QDBusObjectPath Inhibit(const QString &window, uint flags, const QVariantMap &options);

private:
    QDBusConnection connection_{QString()};
};

// Объект запроса, который портал отдаёт в ответ на Inhibit.
class FakePortalRequest : public QObject {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.freedesktop.portal.Request")
public:
    FakePortalRequest(FakeInhibitPortal *portal, const QString &path)
        : QObject(portal), portal_(portal), path_(path)
    {
    }

public slots:
    void Close();

private:
    FakeInhibitPortal *portal_;
    QString path_;
};

}  // namespace radio::test
