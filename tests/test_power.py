"""Запрет засыпания.

D-Bus здесь не трогаем — проверяем только логику намерения: когда блокировка
берётся, когда снимается и что происходит, если ответ службы приходит уже
после остановки воспроизведения.
"""

from __future__ import annotations

import pytest

pytest.importorskip("PySide6.QtDBus")

from PySide6.QtCore import QObject, Signal

from radio_player.desktop.power import SleepInhibitor

pytestmark = pytest.mark.usefixtures("qt_app")


class FakePlayer(QObject):
    """Двойник плеера. Инхибитору важно ровно одно: ждёт ли пользователь звука.

    Что именно считать ожиданием — загрузку, эфир, переподключение — решает
    `PlayerManager.is_active`, и проверяется это в `test_player.py`.
    """

    state_changed = Signal()

    def __init__(self):
        super().__init__()
        self.is_active = False

    def set_active(self, active: bool):
        self.is_active = active
        self.state_changed.emit()


class RecordingInhibitor(SleepInhibitor):
    """Считает обращения к службе вместо настоящих D-Bus-вызовов."""

    def __init__(self, player):
        super().__init__(player)
        self.acquired = 0
        self.dropped = []

    def _acquire(self):
        self.acquired += 1
        self._pending = True

    def _drop(self):
        if self._cookie is None:
            return
        self.dropped.append(self._cookie)
        self._cookie = None

    def reply_with(self, cookie: int):
        """Эмулирует ответ службы на `Inhibit`."""
        self._pending = False
        self._apply_cookie(cookie)


@pytest.fixture
def setup():
    player = FakePlayer()
    return player, RecordingInhibitor(player)


def test_playback_takes_the_lock(setup):
    player, inhibitor = setup
    player.set_active(True)
    assert inhibitor.acquired == 1


def test_stop_releases_the_lock(setup):
    player, inhibitor = setup
    player.set_active(True)
    inhibitor.reply_with(7)

    player.set_active(False)
    assert inhibitor.dropped == [7]


def test_lock_is_held_while_the_player_stays_active(setup):
    """Заснуть между попытками переподключения — то же, что заснуть в эфире."""
    player, inhibitor = setup
    player.set_active(True)
    inhibitor.reply_with(7)

    # Переподключение — тоже ожидание звука, активность не снимается.
    player.set_active(True)
    assert inhibitor.dropped == []


def test_repeated_state_changes_do_not_stack_locks(setup):
    player, inhibitor = setup
    player.set_active(True)
    inhibitor.reply_with(7)
    player.set_active(True)
    player.state_changed.emit()

    assert inhibitor.acquired == 1


def test_late_reply_is_released_immediately(setup):
    """Ответ пришёл, когда играть уже перестали — cookie нельзя оставлять висеть."""
    player, inhibitor = setup
    player.set_active(True)
    player.set_active(False)

    inhibitor.reply_with(7)
    assert inhibitor.dropped == [7]


def test_release_drops_the_lock_on_quit(setup):
    player, inhibitor = setup
    player.set_active(True)
    inhibitor.reply_with(7)

    inhibitor.release()
    assert inhibitor.dropped == [7]
