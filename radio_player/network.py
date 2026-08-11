"""Мониторинг доступности сети — аналог `NWPathMonitor` из macOS-версии.

Бэкенд `QNetworkInformation` есть не в каждой сборке Qt и не в каждой среде,
поэтому его отсутствие не должно ничего ломать: без него остаётся обычный
цикл переподключения, просто без мгновенной реакции на возврат сети.
"""

from __future__ import annotations

from PySide6.QtCore import QObject
from PySide6.QtNetwork import QNetworkInformation

from .player import PlayerManager


def install_network_monitor(player: PlayerManager, parent: QObject | None = None) -> bool:
    """Подписывает плеер на изменения доступности сети. Возвращает False, если бэкенда нет."""
    if not QNetworkInformation.loadDefaultBackend():
        return False
    info = QNetworkInformation.instance()
    if info is None:
        return False

    def on_changed(reachability: QNetworkInformation.Reachability) -> None:
        player.on_network_reachable(reachability == QNetworkInformation.Reachability.Online)

    info.reachabilityChanged.connect(on_changed)
    if parent is not None:
        info.setParent(parent)
    return True
