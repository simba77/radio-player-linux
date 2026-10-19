#include "power.hpp"

#include "app_info.hpp"
#include "player.hpp"

#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDebug>

#include <utility>

using namespace Qt::StringLiterals;

namespace radio {

namespace {

const QString SERVICE = u"org.freedesktop.PowerManagement"_s;
const QString PATH = u"/org/freedesktop/PowerManagement/Inhibit"_s;
const QString INTERFACE = u"org.freedesktop.PowerManagement.Inhibit"_s;
// Служба локальная, отвечает мгновенно; долго ждать её незачем.
constexpr int CALL_TIMEOUT_MS = 3000;

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
    if (cookie_ || pending_)
        return;
    if (!connection_.isConnected()) {
        warn(u"нет шины сеанса"_s);
        return;
    }
    QDBusMessage call = QDBusMessage::createMethodCall(SERVICE, PATH, INTERFACE, u"Inhibit"_s);
    call << QLatin1String(APP_NAME) << tr("Radio playback");
    auto *watcher = new QDBusPendingCallWatcher(connection_.asyncCall(call, CALL_TIMEOUT_MS), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, &SleepInhibitor::onInhibited);
    pending_ = true;
}

void SleepInhibitor::onInhibited(QDBusPendingCallWatcher *watcher)
{
    watcher->deleteLater();
    pending_ = false;

    const QDBusPendingReply<uint> reply = *watcher;
    if (reply.isError()) {
        warn(reply.error().message());
        return;
    }
    cookie_ = reply.value();
    if (!wanted_)
        drop();  // Воспроизведение успело кончиться, пока летел запрос.
}

void SleepInhibitor::drop(bool waitForDelivery)
{
    if (!cookie_)
        return;
    QDBusMessage call = QDBusMessage::createMethodCall(SERVICE, PATH, INTERFACE, u"UnInhibit"_s);
    // `UnInhibit` требует uint32: в Python-версии это обходили через метаобъект,
    // здесь тип задаётся самим значением.
    call << *std::exchange(cookie_, std::nullopt);
    // Ожидание крутит цикл событий, а не блокирует его: служба может жить
    // в этом же процессе (так устроены тесты).
    connection_.call(call, waitForDelivery ? QDBus::BlockWithGui : QDBus::NoBlock, CALL_TIMEOUT_MS);
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
