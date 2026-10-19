#include "power.hpp"

#include "app_info.hpp"
#include "player.hpp"

#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDebug>
#include <QEventLoop>
#include <QTimer>

#include <utility>

using namespace Qt::StringLiterals;

namespace radio {

namespace {

const QString PORTAL_SERVICE = u"org.freedesktop.portal.Desktop"_s;
const QString PORTAL_PATH = u"/org/freedesktop/portal/desktop"_s;
const QString PORTAL_INTERFACE = u"org.freedesktop.portal.Inhibit"_s;
const QString REQUEST_INTERFACE = u"org.freedesktop.portal.Request"_s;
// Флаг портала «засыпание». Экран и блокировку сеанса (флаг Idle) не трогаем.
constexpr uint PORTAL_SUSPEND = 4;

const QString LEGACY_SERVICE = u"org.freedesktop.PowerManagement"_s;
const QString LEGACY_PATH = u"/org/freedesktop/PowerManagement/Inhibit"_s;
const QString LEGACY_INTERFACE = u"org.freedesktop.PowerManagement.Inhibit"_s;

// Службы локальные, отвечают мгновенно; долго ждать их незачем.
constexpr int CALL_TIMEOUT_MS = 3000;

// Портал отвечает на Inhibit раньше, чем заводит объект запроса: Close, пришедший
// сразу за ответом, получает UnknownObject, а блокировка остаётся висеть до выхода
// из приложения (проверено на xdg-desktop-portal-kde). Объект появляется за
// миллисекунды, поэтому Close повторяется, пока объекта нет.
const QString UNKNOWN_OBJECT = u"org.freedesktop.DBus.Error.UnknownObject"_s;
constexpr int PORTAL_CLOSE_ATTEMPTS = 20;
constexpr int PORTAL_CLOSE_RETRY_MS = 100;

}  // namespace

SleepInhibitor::SleepInhibitor(PlayerManager *player, const QDBusConnection &connection, QObject *parent)
    : QObject(parent), player_(player), connection_(connection)
{
    connect(player, &PlayerManager::stateChanged, this, &SleepInhibitor::sync);
}

void SleepInhibitor::release()
{
    // Запрос, который ещё летит, снимет `onInhibited`: намерения больше нет.
    wanted_ = false;
    drop(true);
}

// MARK: - Намерение

void SleepInhibitor::sync()
{
    // Загрузка и переподключение тоже удерживают: заснуть в паузе между
    // попытками — то же самое, что заснуть посреди эфира.
    setWanted(player_->isActive());
}

void SleepInhibitor::setWanted(bool wanted)
{
    if (wanted == wanted_)
        return;
    wanted_ = wanted;
    if (wanted)
        acquire();
    else
        drop();
}

// MARK: - D-Bus

void SleepInhibitor::acquire()
{
    if (lock_ || pending_)
        return;
    if (!connection_.isConnected()) {
        warn(u"нет шины сеанса"_s);
        return;
    }

    const Backend backend = backend_;
    QDBusMessage call;
    if (backend == Backend::Portal) {
        call = QDBusMessage::createMethodCall(PORTAL_SERVICE, PORTAL_PATH, PORTAL_INTERFACE, u"Inhibit"_s);
        // Окно не указываем: блокировка относится к приложению, а не к окну,
        // и окна может не быть вовсе — оно спрятано в трей.
        call << QString() << PORTAL_SUSPEND << QVariantMap{{u"reason"_s, tr("Radio playback")}};
    } else {
        call = QDBusMessage::createMethodCall(LEGACY_SERVICE, LEGACY_PATH, LEGACY_INTERFACE, u"Inhibit"_s);
        call << QLatin1String(APP_NAME) << tr("Radio playback");
    }
    auto *watcher = new QDBusPendingCallWatcher(connection_.asyncCall(call, CALL_TIMEOUT_MS), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this,
            [this, backend](QDBusPendingCallWatcher *finished) { onInhibited(finished, backend); });
    pending_ = true;
}

void SleepInhibitor::onInhibited(QDBusPendingCallWatcher *watcher, Backend backend)
{
    watcher->deleteLater();
    pending_ = false;

    Lock lock{backend, {}, 0};
    QString error;
    if (backend == Backend::Portal) {
        const QDBusPendingReply<QDBusObjectPath> reply = *watcher;
        if (reply.isError())
            error = reply.error().message();
        else
            lock.handle = reply.value();
    } else {
        const QDBusPendingReply<uint> reply = *watcher;
        if (reply.isError())
            error = reply.error().message();
        else
            lock.cookie = reply.value();
    }

    if (!error.isEmpty()) {
        if (backend == Backend::Portal) {
            // Портала нет (старое окружение, минимальная сессия) или он отказал —
            // служба питания на хосте обычно доступна напрямую.
            qDebug().noquote() << u"Портал Inhibit недоступен (%1) — переходим на PowerManagement."_s.arg(
                error);
            backend_ = Backend::PowerManagement;
            if (wanted_)
                acquire();
            return;
        }
        warn(error);
        return;
    }

    lock_ = lock;
    if (!wanted_)
        drop();  // Воспроизведение успело кончиться, пока летел запрос.
}

void SleepInhibitor::drop(bool waitForDelivery)
{
    if (!lock_)
        return;
    const Lock lock = *std::exchange(lock_, std::nullopt);
    if (lock.backend == Backend::Portal) {
        // Блокировка портала живёт, пока жив её запрос: закрыть его и значит снять.
        closePortalRequest(lock.handle, PORTAL_CLOSE_ATTEMPTS, waitForDelivery);
        return;
    }
    QDBusMessage call =
        QDBusMessage::createMethodCall(LEGACY_SERVICE, LEGACY_PATH, LEGACY_INTERFACE, u"UnInhibit"_s);
    // `UnInhibit` требует uint32: тип задаётся самим значением. Число по умолчанию
    // ушло бы int32, и вызов отлетел бы с UnknownMethod.
    call << lock.cookie;
    // Ожидание крутит цикл событий, а не блокирует его: служба может жить
    // в этом же процессе (так устроены тесты).
    connection_.call(call, waitForDelivery ? QDBus::BlockWithGui : QDBus::NoBlock, CALL_TIMEOUT_MS);
}

void SleepInhibitor::closePortalRequest(const QDBusObjectPath &handle, int attemptsLeft, bool waitForDelivery)
{
    const QDBusMessage call =
        QDBusMessage::createMethodCall(PORTAL_SERVICE, handle.path(), REQUEST_INTERFACE, u"Close"_s);
    if (waitForDelivery) {
        // Выход: таймерам уже не сработать, поэтому повторы — здесь же, с ожиданием
        // во вложенном цикле событий.
        for (int attempt = 0; attempt < attemptsLeft; ++attempt) {
            const QDBusMessage reply = connection_.call(call, QDBus::BlockWithGui, CALL_TIMEOUT_MS);
            if (reply.type() != QDBusMessage::ErrorMessage || reply.errorName() != UNKNOWN_OBJECT)
                return;
            QEventLoop pause;
            QTimer::singleShot(PORTAL_CLOSE_RETRY_MS, &pause, &QEventLoop::quit);
            pause.exec();
        }
        return;
    }

    auto *watcher = new QDBusPendingCallWatcher(connection_.asyncCall(call, CALL_TIMEOUT_MS), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this,
            [this, handle, attemptsLeft](QDBusPendingCallWatcher *finished) {
                finished->deleteLater();
                const QDBusMessage reply = finished->reply();
                if (reply.type() != QDBusMessage::ErrorMessage || reply.errorName() != UNKNOWN_OBJECT ||
                    attemptsLeft <= 1)
                    return;
                QTimer::singleShot(PORTAL_CLOSE_RETRY_MS, this, [this, handle, attemptsLeft] {
                    closePortalRequest(handle, attemptsLeft - 1, false);
                });
            });
}

void SleepInhibitor::warn(const QString &reason)
{
    if (warned_)
        return;
    warned_ = true;
    qWarning().noquote() << u"Запрет засыпания недоступен (%1) — система может заснуть во время эфира."_s.arg(
        reason);
}

}  // namespace radio
