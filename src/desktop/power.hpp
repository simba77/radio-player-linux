#pragma once

// Запрет засыпания системы на время воспроизведения.
//
// libmpv питанием не управляет (`stop-screensaver` работает только при видео),
// а MPRIS сам по себе основанием не считается: ни PowerDevil в Plasma, ни
// gnome-settings-daemon не смотрят на активный плеер. Без явной блокировки
// автосуспенд по бездействию обрывает эфир через 5–20 минут.
//
// Блокируем **только засыпание системы**: гашение экрана и блокировка сеанса
// радио не мешают, подавлять их незачем.
//
// Интерфейс `org.freedesktop.PowerManagement.Inhibit` — legacy-API, но его
// реализуют и PowerDevil, и gnome-settings-daemon, поэтому он покрывает оба
// целевых окружения без ветвлений.

#include <QDBusConnection>
#include <QObject>

#include <optional>

class QDBusPendingCallWatcher;

namespace radio {

class PlayerManager;

// Держит блокировку засыпания, пока плеер активен.
//
// Все вызовы асинхронные: интерфейс не ждёт службу питания. Между запросом
// и ответом воспроизведение может успеть остановиться, поэтому намерение
// (`wanted_`) хранится отдельно от факта (`cookie_`) — как намерение играть
// в `PlayerManager`.
class SleepInhibitor : public QObject {
    Q_OBJECT
public:
    // Шина — параметр: тесты поднимают свою со службой-двойником.
    SleepInhibitor(PlayerManager *player, const QDBusConnection &connection = QDBusConnection::sessionBus(),
                   QObject *parent = nullptr);

    // Держится ли блокировка прямо сейчас.
    bool isHeld() const { return cookie_.has_value(); }
    // Снимает блокировку. Вызывается перед выходом из приложения.
    void release();

private:
    void sync();
    void setWanted(bool wanted);
    void acquire();
    void onInhibited(QDBusPendingCallWatcher *watcher);
    void drop();
    // Одного сообщения за сеанс достаточно: без блокировки радио играет,
    // просто система может заснуть — ломать этим интерфейс не за что.
    void warn(const QString &reason);

    PlayerManager *player_;
    QDBusConnection connection_;
    std::optional<uint> cookie_;
    bool wanted_ = false;
    bool pending_ = false;
    bool warned_ = false;
};

}  // namespace radio
