"""Запрет засыпания системы на время воспроизведения.

libmpv питанием не управляет (`stop-screensaver` работает только при видео),
а MPRIS сам по себе основанием не считается: ни PowerDevil в Plasma, ни
gnome-settings-daemon не смотрят на активный плеер. Без явной блокировки
автосуспенд по бездействию обрывает эфир через 5–20 минут.

Блокируем **только засыпание системы**: гашение экрана и блокировка сеанса
радио не мешают, подавлять их незачем.

Интерфейс `org.freedesktop.PowerManagement.Inhibit` — legacy-API, но его
реализуют и PowerDevil, и gnome-settings-daemon, поэтому он покрывает оба
целевых окружения без ветвлений.
"""

from __future__ import annotations

from PySide6.QtCore import Q_ARG, QMetaObject, QObject, Slot
from PySide6.QtDBus import QDBusConnection, QDBusInterface, QDBusMessage, QDBusPendingCallWatcher

from . import APP_NAME
from .player import PlayerManager

_SERVICE = "org.freedesktop.PowerManagement"
_PATH = "/org/freedesktop/PowerManagement/Inhibit"
_INTERFACE = "org.freedesktop.PowerManagement.Inhibit"
_REASON = "Воспроизведение радио"

#: Служба локальная, отвечает мгновенно; долго ждать её незачем.
_CALL_TIMEOUT_MS = 3000


class SleepInhibitor(QObject):
    """Держит блокировку засыпания, пока плеер играет.

    Все вызовы асинхронные: `Inhibit` уходит через `asyncCall`, ответ приходит
    в `_on_inhibited`. Между запросом и ответом воспроизведение может успеть
    остановиться, поэтому намерение (`_wanted`) хранится отдельно от факта
    (`_cookie`) — как `_should_be_playing` в `PlayerManager`.
    """

    def __init__(self, player: PlayerManager, parent: QObject | None = None) -> None:
        super().__init__(parent)
        self._player = player
        self._interface: QDBusInterface | None = None
        self._cookie: int | None = None
        self._wanted = False
        self._pending = False
        self._warned = False

        player.state_changed.connect(self._sync)

    def release(self) -> None:
        """Снимает блокировку. Вызывается перед выходом из приложения."""
        self._set_wanted(False)

    # MARK: - Намерение

    @Slot()
    def _sync(self) -> None:
        # Переподключение тоже удерживает: заснуть в паузе между попытками —
        # то же самое, что заснуть посреди эфира.
        self._set_wanted(self._player.is_playing or self._player.is_reconnecting)

    def _set_wanted(self, wanted: bool) -> None:
        if wanted == self._wanted:
            return
        self._wanted = wanted
        if wanted:
            self._acquire()
        else:
            self._drop()

    # MARK: - D-Bus

    def _acquire(self) -> None:
        if self._cookie is not None or self._pending:
            return
        interface = self._connect()
        if interface is None:
            return
        pending = interface.asyncCallWithArgumentList("Inhibit", [APP_NAME, _REASON])
        watcher = QDBusPendingCallWatcher(pending, self)
        watcher.finished.connect(self._on_inhibited)
        self._pending = True

    @Slot(QDBusPendingCallWatcher)
    def _on_inhibited(self, watcher: QDBusPendingCallWatcher) -> None:
        watcher.deleteLater()
        self._pending = False

        reply = watcher.reply()
        if reply.type() != QDBusMessage.MessageType.ReplyMessage:
            self._warn(reply.errorMessage() or "служба не ответила")
            return
        arguments = reply.arguments()
        if not arguments or not isinstance(arguments[0], int):
            self._warn("служба вернула не cookie")
            return

        self._apply_cookie(arguments[0])

    def _apply_cookie(self, cookie: int) -> None:
        self._cookie = cookie
        if not self._wanted:
            # Воспроизведение успело кончиться, пока летел запрос.
            self._drop()

    def _drop(self) -> None:
        if self._cookie is None:
            return
        cookie, self._cookie = self._cookie, None
        interface = self._connect()
        if interface is None:
            return
        # `UnInhibit` требует uint32, а Python-число PySide6 маршалит как int32,
        # и вызов отлетает с UnknownMethod (signature 'i'). Тип задаётся явно
        # только через метаобъект интерфейса, полученный при интроспекции.
        QMetaObject.invokeMethod(interface, "UnInhibit", Q_ARG("uint", cookie))

    def _connect(self) -> QDBusInterface | None:
        """Интерфейс службы питания или None, если её нет.

        Невалидный интерфейс не кэшируется: PowerDevil мог ещё не стартовать,
        и следующая попытка — при следующем включении — может пройти.
        """
        if self._interface is not None:
            return self._interface

        connection = QDBusConnection.sessionBus()
        if not connection.isConnected():
            self._warn("нет шины сеанса")
            return None

        interface = QDBusInterface(_SERVICE, _PATH, _INTERFACE, connection, self)
        interface.setTimeout(_CALL_TIMEOUT_MS)
        if not interface.isValid():
            self._warn("служба управления питанием недоступна")
            return None

        self._interface = interface
        return interface

    def _warn(self, message: str) -> None:
        """Одного сообщения за сеанс достаточно: без блокировки радио играет,
        просто система может заснуть — ломать этим UI не за что."""
        if self._warned:
            return
        self._warned = True
        print(f"Запрет засыпания недоступен ({message}) — система может заснуть во время эфира.")
