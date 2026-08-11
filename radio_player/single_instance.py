"""Single-instance lock через QLocalServer.

На macOS этим занималась сама система (одна .app — один процесс). В Linux нужен явный
механизм: второй запуск не поднимает новое окно, а просит первый показать своё.
"""

from __future__ import annotations

from collections.abc import Callable

from PySide6.QtCore import QObject
from PySide6.QtNetwork import QLocalServer, QLocalSocket

from . import IPC_SOCKET_NAME

#: Сообщение, которым второй экземпляр просит первый показаться.
_SHOW_COMMAND = b"show\n"
_CONNECT_TIMEOUT_MS = 500


def notify_running_instance() -> bool:
    """Если приложение уже запущено — попросить его показать окно.

    Возвращает True, когда живой экземпляр найден и уведомлён: в этом случае
    текущий процесс должен молча завершиться.
    """
    socket = QLocalSocket()
    socket.connectToServer(IPC_SOCKET_NAME)
    if not socket.waitForConnected(_CONNECT_TIMEOUT_MS):
        return False
    socket.write(_SHOW_COMMAND)
    socket.waitForBytesWritten(_CONNECT_TIMEOUT_MS)
    socket.disconnectFromServer()
    return True


class InstanceServer(QObject):
    """Слушает попытки повторного запуска и дёргает колбэк показа окна."""

    def __init__(self, on_show_requested: Callable[[], None], parent: QObject | None = None) -> None:
        super().__init__(parent)
        self._on_show_requested = on_show_requested
        self._server = QLocalServer(self)
        # Осиротевший сокет остаётся после аварийного завершения и блокирует listen().
        QLocalServer.removeServer(IPC_SOCKET_NAME)
        self._server.newConnection.connect(self._handle_connection)
        self._server.listen(IPC_SOCKET_NAME)

    def _handle_connection(self) -> None:
        connection = self._server.nextPendingConnection()
        if connection is None:
            return
        connection.disconnected.connect(connection.deleteLater)
        self._on_show_requested()

    def close(self) -> None:
        self._server.close()
        QLocalServer.removeServer(IPC_SOCKET_NAME)
