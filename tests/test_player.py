"""Состояния плеера: запуск потока, обрыв, реконнект, громкость.

libmpv здесь не участвует — вместо него подставляется двойник. Проверяется
именно логика: намерение играть отдельно от факта, реконнект по обрыву и по
зависшей буферизации, и то, что собственный `stop()` реконнект не запускает.

Таймеры не ждём: реальные интервалы измеряются секундами, поэтому срабатывание
имитируется через `timeout.emit()`, а проверяется факт, что таймер взведён.
"""

from __future__ import annotations

import pytest

from radio_player import storage
from radio_player.models import Station
from radio_player.player import PlayerManager

pytestmark = pytest.mark.usefixtures("qt_app")

FIRST = Station(id="first", name="Первая", url="https://example.org/first")
SECOND = Station(id="second", name="Вторая", url="https://example.org/second.m3u8")

#: Коды `mpv_end_file_reason`, как их отдаёт python-mpv.
EOF, STOP, QUIT, ERROR, REDIRECT = 0, 2, 3, 4, 5


class FakeMpv:
    """Двойник libmpv: запоминает вызовы и умеет отдавать события."""

    def __init__(self) -> None:
        self.properties: dict[str, object] = {}
        self.observers: dict[str, list] = {}
        self.event_handlers: dict[str, object] = {}
        self.played: list[str] = []
        self.commands: list[tuple] = []
        self.terminated = False
        self.fail_on_play = False

    # MARK: - Интерфейс, которым пользуется PlayerManager

    def __setitem__(self, name: str, value: object) -> None:
        self.properties[name] = value

    def observe_property(self, name: str, callback) -> None:
        self.observers.setdefault(name, []).append(callback)

    def unobserve_property(self, name: str, callback) -> None:
        self.observers.get(name, []).remove(callback)

    def event_callback(self, name: str):
        def register(handler):
            self.event_handlers[name] = handler
            return handler

        return register

    def play(self, url: str) -> None:
        if self.fail_on_play:
            raise OSError("поток не открылся")
        self.played.append(url)

    def command(self, *args) -> None:
        self.commands.append(args)

    def terminate(self) -> None:
        self.terminated = True

    # MARK: - Помощники теста

    def emit(self, name: str, value) -> None:
        for callback in list(self.observers.get(name, [])):
            callback(name, value)

    def end_file(self, reason: int) -> None:
        event = type("Event", (), {"data": type("Data", (), {"reason": reason})()})()
        self.event_handlers["end-file"](event)


class FakeStore:
    def __init__(self, stations: list[Station]) -> None:
        self._stations = stations

    @property
    def stations(self) -> list[Station]:
        return list(self._stations)


@pytest.fixture
def mpv():
    return FakeMpv()


@pytest.fixture
def player(mpv):
    manager = PlayerManager(FakeStore([FIRST, SECOND]), mpv_factory=lambda: mpv)
    yield manager
    manager.shutdown()


def start_playing(player: PlayerManager, mpv: FakeMpv, station: Station = FIRST) -> None:
    """Станция запущена и поток действительно пошёл."""
    player.play(station)
    mpv.emit("core-idle", False)


# MARK: - Запуск


def test_play_opens_the_stream(player, mpv):
    player.play(FIRST)
    assert mpv.played == [FIRST.url]


def test_playback_is_confirmed_by_mpv(player, mpv):
    player.play(FIRST)
    assert player.is_loading is True

    mpv.emit("core-idle", False)
    assert player.is_loading is False
    assert player.is_playing is True


def test_failure_to_open_reports_an_error(player, mpv):
    mpv.fail_on_play = True
    errors = []
    player.error_occurred.connect(errors.append)

    player.play(FIRST)
    assert player.is_playing is False
    assert player.is_loading is False
    assert len(errors) == 1


def test_stop_leaves_nothing_running(player, mpv):
    start_playing(player, mpv)
    player.stop()

    assert player.is_playing is False
    assert ("stop",) in mpv.commands
    assert player._reconnect_timer.isActive() is False
    assert player._stall_timer.isActive() is False


def test_last_station_is_remembered(player, mpv):
    player.play(SECOND)
    assert storage.load_state()["last_station_id"] == SECOND.id


def test_last_station_is_restored_on_startup(mpv):
    storage.update_state(last_station_id=SECOND.id)
    restored = PlayerManager(FakeStore([FIRST, SECOND]), mpv_factory=lambda: mpv)
    try:
        assert restored.current_station == SECOND
        # Восстановление — не воспроизведение: станция выбрана, поток молчит.
        assert restored.is_playing is False
        assert mpv.played == []
    finally:
        restored.shutdown()


# MARK: - Обрыв и реконнект


@pytest.mark.parametrize("reason", [EOF, ERROR])
def test_stream_break_starts_reconnecting(player, mpv, reason):
    """Для живого радио и «конец файла», и ошибка означают обрыв."""
    start_playing(player, mpv)
    mpv.end_file(reason)

    assert player.is_reconnecting is True
    assert player._reconnect_timer.isActive() is True
    assert player._blink_timer.isActive() is True


@pytest.mark.parametrize("reason", [STOP, QUIT, REDIRECT])
def test_our_own_stop_does_not_reconnect(player, mpv, reason):
    """Остановку и переход по редиректу mpv отрабатывает сам."""
    start_playing(player, mpv)
    mpv.end_file(reason)

    assert player.is_reconnecting is False
    assert player._reconnect_timer.isActive() is False


def test_stop_cancels_the_intention_to_play(player, mpv):
    start_playing(player, mpv)
    player.stop()
    mpv.end_file(ERROR)

    assert player.is_reconnecting is False


def test_reconnect_retries_the_same_station(player, mpv):
    start_playing(player, mpv)
    mpv.end_file(ERROR)
    player._reconnect_timer.timeout.emit()

    assert mpv.played == [FIRST.url, FIRST.url]


def test_successful_reconnect_clears_the_indication(player, mpv):
    start_playing(player, mpv)
    mpv.end_file(ERROR)
    player._reconnect_timer.timeout.emit()
    mpv.emit("core-idle", False)

    assert player.is_reconnecting is False
    assert player.is_playing is True
    assert player._blink_timer.isActive() is False
    assert player.blink_on is False


def test_reconnecting_survives_repeated_breaks(player, mpv):
    """Второй обрыв подряд не должен перезапускать мигание с нуля."""
    start_playing(player, mpv)
    mpv.end_file(ERROR)
    mpv.end_file(ERROR)

    assert player.is_reconnecting is True
    assert player._reconnect_timer.isActive() is True


def test_stalled_buffering_leads_to_reconnect(player, mpv):
    start_playing(player, mpv)
    mpv.emit("paused-for-cache", True)
    assert player.is_loading is True
    assert player._stall_timer.isActive() is True

    player._stall_timer.timeout.emit()
    assert player.is_reconnecting is True


def test_playback_cancels_the_stall_watchdog(player, mpv):
    player.play(FIRST)
    assert player._stall_timer.isActive() is True

    mpv.emit("core-idle", False)
    assert player._stall_timer.isActive() is False


# MARK: - Сеть


def test_network_loss_starts_reconnecting(player, mpv):
    start_playing(player, mpv)
    player.on_network_reachable(False)

    assert player.is_reconnecting is True


def test_network_return_retries_immediately(player, mpv):
    """Ждать конца трёхсекундного цикла, когда сеть уже вернулась, незачем."""
    start_playing(player, mpv)
    player.on_network_reachable(False)
    player.on_network_reachable(True)

    assert mpv.played == [FIRST.url, FIRST.url]


def test_network_events_are_ignored_when_not_playing(player, mpv):
    player.on_network_reachable(False)
    assert player.is_reconnecting is False


# MARK: - Переключение станций


def test_next_and_previous_wrap_around(player, mpv):
    start_playing(player, mpv, SECOND)
    player.play_next()
    assert player.current_station == FIRST

    player.play_previous()
    assert player.current_station == SECOND


def test_toggle_starts_the_first_station_when_nothing_was_chosen(player, mpv):
    player.toggle()
    assert player.current_station == FIRST


def test_toggle_stops_what_is_playing(player, mpv):
    start_playing(player, mpv)
    player.toggle()
    assert player.is_playing is False


# MARK: - Громкость


def test_volume_is_clamped_and_reaches_mpv(player, mpv):
    player.set_volume(150)
    assert player.volume == 100
    assert mpv.properties["volume"] == 100

    player.set_volume(-10)
    assert player.volume == 0


def test_volume_is_remembered(player, mpv):
    player.set_volume(42)
    assert storage.load_state()["volume"] == 42


def test_volume_is_restored_on_startup(mpv):
    storage.update_state(volume=33)
    restored = PlayerManager(FakeStore([FIRST]), mpv_factory=lambda: mpv)
    try:
        assert restored.volume == 33
        assert mpv.properties["volume"] == 33
    finally:
        restored.shutdown()


# MARK: - Завершение


def test_shutdown_releases_everything(player, mpv):
    start_playing(player, mpv)
    mpv.end_file(ERROR)
    player.shutdown()

    assert mpv.terminated is True
    assert player._reconnect_timer.isActive() is False
    assert player._blink_timer.isActive() is False
    assert player._stall_timer.isActive() is False
