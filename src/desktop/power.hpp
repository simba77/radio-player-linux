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
// Первым пробуется портал `org.freedesktop.portal.Inhibit`: он работает и из
// песочницы Flatpak, где прямого доступа к службе питания нет. Если портала нет
// или он отказал, приложение переходит на legacy-интерфейс
// `org.freedesktop.PowerManagement.Inhibit`, который реализуют и PowerDevil,
// и gnome-settings-daemon, — и остаётся на нём до конца сеанса.

#include <QDBusConnection>
#include <QDBusObjectPath>
#include <QObject>

#include <optional>

class QDBusPendingCallWatcher;

namespace radio {

class PlayerManager;

// Держит блокировку засыпания, пока плеер активен.
//
// Все вызовы асинхронные: интерфейс не ждёт службу питания. Между запросом
// и ответом воспроизведение может успеть остановиться, поэтому намерение
// (`wanted_`) хранится отдельно от факта (`lock_`) — как намерение играть
// в `PlayerManager`.
class SleepInhibitor : public QObject {
    Q_OBJECT
public:
    enum class Backend { Portal, PowerManagement };

    // Шина — параметр: тесты поднимают свою со службами-двойниками.
    SleepInhibitor(PlayerManager *player, const QDBusConnection &connection = QDBusConnection::sessionBus(),
                   QObject *parent = nullptr);

    // Держится ли блокировка прямо сейчас.
    bool isHeld() const { return lock_.has_value(); }
    // Через что блокировка берётся сейчас: портал, пока он не подвёл.
    Backend backend() const { return backend_; }
    // Снимает блокировку. Вызывается перед выходом из приложения.
    void release();

private:
    // Взятая блокировка: handle запроса портала или cookie службы питания.
    struct Lock {
        Backend backend;
        QDBusObjectPath handle;
        uint cookie = 0;
    };

    void sync();
    void setWanted(bool wanted);
    void acquire();
    void onInhibited(QDBusPendingCallWatcher *watcher, Backend backend);
    // Снимает блокировку. При выходе ждёт, пока служба примет вызов: асинхронное
    // сообщение может уйти вместе с соединением, не дойдя до шины.
    void drop(bool waitForDelivery = false);
    // Закрывает запрос портала, повторяя попытку, пока объекта запроса ещё нет.
    void closePortalRequest(const QDBusObjectPath &handle, int attemptsLeft, bool waitForDelivery);
    // Одного сообщения за сеанс достаточно: без блокировки радио играет,
    // просто система может заснуть — ломать этим интерфейс не за что.
    void warn(const QString &reason);

    PlayerManager *player_;
    QDBusConnection connection_;
    Backend backend_ = Backend::Portal;
    std::optional<Lock> lock_;
    bool wanted_ = false;
    bool pending_ = false;
    bool warned_ = false;
};

}  // namespace radio
