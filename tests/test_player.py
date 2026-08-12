"""Состояния плеера: запуск потока, обрыв, реконнект, громкость.

libmpv здесь не участвует — вместо него подставляется двойник. Проверяется
именно логика: намерение играть отдельно от факта, реконнект по обрыву и по
зависшей буферизации, и то, что собственный `stop()` реконнект не запускает.

Таймеры не ждём: реальные интервалы измеряются секундами, поэтому срабатывание
имитируется через `timeout.emit()`, а проверяется факт, что таймер взведён.
"""

from __future__ import annotations

import pytest
from doubles import FakeMpv, FakeStore

from radio_player.core import storage
from radio_player.core.models import Station
from radio_player.playback.player import PlaybackState, PlayerManager

pytestmark = pytest.mark.usefixtures("qt_app")

FIRST = Station(id="first", name="Первая", url="https://example.org/first")
SECOND = Station(id="second", name="Вторая", url="https://example.org/second.m3u8")

#: Коды `mpv_end_file_reason`, как их отдаёт python-mpv.
EOF, STOP, QUIT, ERROR, REDIRECT = 0, 2, 3, 4, 5


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
    assert player.state is PlaybackState.LOADING

    mpv.emit("core-idle", False)
    assert player.state is PlaybackState.PLAYING


def test_loading_is_not_reported_as_playing(player):
    """Пока mpv не подтвердил звук, «играет» — неправда.

    Наблюдаемое состояние важнее оптимистичного флага: на нём держится и
    иконка в трее, и `PlaybackStatus` в MPRIS.
    """
    player.play(FIRST)
    assert player.is_playing is False
    # Но намерение уже есть: кнопка показывает «стоп», система не спит.
    assert player.is_active is True


def test_failure_to_open_reports_an_error(player, mpv):
    mpv.fail_on_play = True
    errors = []
    player.error_occurred.connect(errors.append)

    player.play(FIRST)
    assert player.state is PlaybackState.FAILED
    assert player.error_message in errors
    assert len(errors) == 1


def test_a_new_attempt_clears_the_previous_error(player, mpv):
    mpv.fail_on_play = True
    player.play(FIRST)
    mpv.fail_on_play = False

    player.play(FIRST)
    assert player.error_message is None
    assert player.state is PlaybackState.LOADING


def test_stop_leaves_nothing_running(player, mpv):
    start_playing(player, mpv)
    player.stop()

    assert player.state is PlaybackState.IDLE
    assert player.is_active is False
    assert ("stop",) in mpv.commands
    assert player._reconnect_timer.isActive() is False
    assert player._stall_timer.isActive() is False


def test_last_station_is_remembered(player):
    player.play(SECOND)
    assert storage.load_state()["last_station_id"] == SECOND.id


def test_last_station_is_restored_on_startup(mpv):
    storage.update_state(last_station_id=SECOND.id)
    restored = PlayerManager(FakeStore([FIRST, SECOND]), mpv_factory=lambda: mpv)
    try:
        assert restored.current_station == SECOND
        # Восстановление — не воспроизведение: станция выбрана, поток молчит.
        assert restored.state is PlaybackState.IDLE
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


def test_reconnecting_still_counts_as_active(player, mpv):
    """Пауза между попытками — не остановка: система не должна засыпать,
    а кнопка обязана оставаться «стоп»."""
    start_playing(player, mpv)
    mpv.end_file(ERROR)

    assert player.is_active is True
    assert player.is_playing is False


def test_failed_attempt_during_reconnect_keeps_reconnecting(player, mpv):
    """Неудачная попытка — часть цикла, а не отдельное событие для пользователя."""
    start_playing(player, mpv)
    mpv.end_file(ERROR)
    mpv.fail_on_play = True
    player._reconnect_timer.timeout.emit()

    assert player.state is PlaybackState.RECONNECTING
    assert player.error_message is None


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


def test_network_events_are_ignored_when_not_playing(player):
    player.on_network_reachable(False)
    assert player.is_reconnecting is False


# MARK: - Переключение станций


def test_next_and_previous_wrap_around(player, mpv):
    start_playing(player, mpv, SECOND)
    player.play_next()
    assert player.current_station == FIRST

    player.play_previous()
    assert player.current_station == SECOND


def test_toggle_starts_the_first_station_when_nothing_was_chosen(player):
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


def test_volume_reaches_the_disk_after_the_pause(player):
    player.set_volume(42)
    player._volume_save_timer.timeout.emit()
    assert storage.load_state()["volume"] == 42


def test_dragging_the_slider_writes_once(player, monkeypatch):
    """Слайдер шлёт значение на каждый пиксель, а запись идёт с fsync
    в потоке отрисовки — за одно перетаскивание это десятки обращений к диску."""
    writes = []
    monkeypatch.setattr(storage, "update_state", lambda **values: writes.append(values))

    for value in range(40, 60):
        player.set_volume(value)
    assert writes == []

    player._volume_save_timer.timeout.emit()
    assert writes == [{"volume": 59}]


def test_volume_is_saved_on_shutdown(player):
    """Между последним движением слайдера и выходом пауза не успевает истечь."""
    player.set_volume(42)
    player.shutdown()
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


# MARK: - Пауза между попытками


def test_reconnect_pause_grows_with_each_failure(player, mpv, monkeypatch):
    """Сервер радио может лежать часами — стучаться каждые три секунды невежливо."""
    from radio_player.playback import player as player_module

    monkeypatch.setattr(player_module, "_with_jitter", int)
    start_playing(player, mpv)
    mpv.end_file(ERROR)

    delays = [player._reconnect_timer.interval()]
    for _ in range(3):
        player._reconnect_timer.timeout.emit()
        delays.append(player._reconnect_timer.interval())

    assert delays == [3000, 6000, 12000, 24000]


def test_reconnect_pause_stops_at_the_ceiling(player, mpv, monkeypatch):
    from radio_player.playback import player as player_module

    monkeypatch.setattr(player_module, "_with_jitter", int)
    start_playing(player, mpv)
    mpv.end_file(ERROR)
    for _ in range(20):
        player._reconnect_timer.timeout.emit()

    assert player._reconnect_timer.interval() == player_module.RECONNECT_MAX_INTERVAL_MS


def test_reconnect_pause_starts_over_after_a_new_break(player, mpv, monkeypatch):
    """Связь восстановилась — накопленная задержка больше не про эту ситуацию."""
    from radio_player.playback import player as player_module

    monkeypatch.setattr(player_module, "_with_jitter", int)
    start_playing(player, mpv)
    mpv.end_file(ERROR)
    player._reconnect_timer.timeout.emit()
    mpv.emit("core-idle", False)

    mpv.end_file(ERROR)
    assert player._reconnect_timer.interval() == player_module.RECONNECT_INTERVAL_MS


def test_jitter_stays_within_the_declared_spread():
    from radio_player.playback.player import RECONNECT_JITTER, _with_jitter

    values = {_with_jitter(10_000) for _ in range(200)}
    assert min(values) >= 10_000 * (1 - RECONNECT_JITTER)
    assert max(values) <= 10_000 * (1 + RECONNECT_JITTER)
    # Разброс должен быть настоящим, иначе смысла в нём нет.
    assert len(values) > 1


def test_volume_change_is_announced(player):
    """Громкость меняют и снаружи: слайдер в окне и апплет MPRIS узнают об этом
    только из сигнала."""
    announced = []
    player.volume_changed.connect(announced.append)

    player.set_volume(55)
    assert announced == [55]


def test_setting_the_same_volume_announces_nothing(player):
    player.set_volume(55)
    announced = []
    player.volume_changed.connect(announced.append)

    player.set_volume(55)
    assert announced == []
