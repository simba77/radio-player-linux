"""Single-instance lock через QLocalServer.

На macOS этим занималась сама система (одна .app — один процесс). В Linux нужен явный
механизм: второй запуск не поднимает новое окно, а просит первый показать своё.

Вместе с просьбой передаётся токен активации. На Wayland поднять и сфокусировать
окно чужому процессу нельзя — композитор пускает вперёд только того, кому токен
выдан. Токен есть у запускаемого процесса (`XDG_ACTIVATION_TOKEN`, на X11 —
`DESKTOP_STARTUP_ID`), а окно принадлежит первому: без передачи окно в лучшем
случае мигнёт в панели задач.
"""

from __future__ import annotations

import os
from collections.abc import Callable

from PySide6.QtCore import QObject
from PySide6.QtNetwork import QLocalServer, QLocalSocket

from .. import IPC_SOCKET_NAME

#: Команда второго экземпляра: показать окно, вот токен активации (может быть пуст).
_SHOW_COMMAND = "show"
_CONNECT_TIMEOUT_MS = 500


def activation_token() -> str:
    """Токен, выданный окружением этому запуску."""
    return os.environ.get("XDG_ACTIVATION_TOKEN") or os.environ.get("DESKTOP_STARTUP_ID") or ""


def notify_running_instance() -> bool:
    """Если приложение уже запущено — попросить его показать окно.

    Возвращает True, когда живой экземпляр найден и уведомлён: в этом случае
    текущий процесс должен молча завершиться.
    """
    socket = QLocalSocket()
    socket.connectToServer(IPC_SOCKET_NAME)
    if not socket.waitForConnected(_CONNECT_TIMEOUT_MS):
        return False
    socket.write(encode_request(activation_token()))
    socket.waitForBytesWritten(_CONNECT_TIMEOUT_MS)
    socket.disconnectFromServer()
    return True


def encode_request(token: str) -> bytes:
    return f"{_SHOW_COMMAND} {token}\n".encode()


def decode_request(data: bytes) -> str | None:
    """Токен из запроса или None, если это не наша команда.

    Токена может не быть (запуск из терминала) — тогда возвращается пустая строка.
    """
    text = data.decode("utf-8", errors="replace").strip()
    if not text.startswith(_SHOW_COMMAND):
        return None
    return text[len(_SHOW_COMMAND) :].strip()


class InstanceServer(QObject):
    """Слушает попытки повторного запуска и дёргает колбэк показа окна."""

    def __init__(self, on_show_requested: Callable[[str], None], parent: QObject | None = None) -> None:
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
        # Данные приходят отдельным событием: показывать окно нужно после них,
        # иначе токен активации опоздает и фокус не перейдёт.
        connection.readyRead.connect(lambda: self._handle_request(connection))

    def _handle_request(self, connection: QLocalSocket) -> None:
        token = decode_request(bytes(connection.readAll().data()))
        if token is None:
            return
        self._on_show_requested(token)

    def close(self) -> None:
        self._server.close()
        QLocalServer.removeServer(IPC_SOCKET_NAME)
