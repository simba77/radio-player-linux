"""Воспроизведение: обёртка над libmpv с состояниями, реконнектом и метаданными.

Прямой аналог `PlayerManager` из macOS-версии. Ключевые решения оттуда сохранены:

* «намерение играть» (`_should_be_playing`) живёт отдельно от факта воспроизведения
  и сбрасывается только явным `stop()` — обрыв сети его не отменяет;
* состояния берутся из наблюдения за mpv, а не выставляются оптимистично;
* перед каждым запуском потока и в `stop()` снимаются все наблюдатели и таймеры.

Наблюдаемое состояние — одно поле `state`. Раньше их было четыре независимых
флага, и большинство их комбинаций не значили ничего: «играет и одновременно
переподключается» — не состояние, а рассинхрон. Свойства `is_playing`,
`is_loading` и `is_reconnecting` остались как удобные проверки, но теперь они
выводятся из `state` и разойтись друг с другом не могут.
"""

from __future__ import annotations

import contextlib
import locale
from collections.abc import Callable
from enum import Enum, auto
from typing import Any, Protocol

from PySide6.QtCore import QCoreApplication, QObject, QTimer, Signal, Slot
from PySide6.QtGui import QImage

from . import storage
from .artwork import ArtworkLoader
from .metadata import MetadataProvider, make_provider
from .models import Station, TrackInfo

#: Пауза между попытками переподключения — как на macOS, без backoff.
RECONNECT_INTERVAL_MS = 3000
#: Сколько ждём выхода из буферизации, прежде чем считать поток зависшим.
STALL_TIMEOUT_MS = 6000
#: Период мигания иконки в трее во время переподключения.
BLINK_INTERVAL_MS = 600
#: Пауза перед сохранением громкости. Слайдер шлёт значение на каждый пиксель,
#: а запись состояния идёт с `fsync` в потоке отрисовки — за одно перетаскивание
#: это десятки лишних обращений к диску.
VOLUME_SAVE_DELAY_MS = 800


class PlaybackState(Enum):
    """Что происходит с воспроизведением прямо сейчас."""

    #: Ничего не играет и не запускается.
    IDLE = auto()
    #: Поток открыт, но mpv ещё не подтвердил, что звук пошёл.
    LOADING = auto()
    #: Идёт эфир — это подтвердил сам mpv, а не наше намерение.
    PLAYING = auto()
    #: Связь оборвалась, идут попытки переподключения.
    RECONNECTING = auto()
    #: Поток не удалось открыть; причина — в `error_message`.
    FAILED = auto()


#: Состояния, в которых пользователь ждёт звука: кнопка показывает «стоп»,
#: система не должна засыпать, а MPRIS обязан отвечать «Playing».
_ACTIVE_STATES = frozenset({PlaybackState.LOADING, PlaybackState.PLAYING, PlaybackState.RECONNECTING})


class StationsSource(Protocol):
    """Источник списка станций. В M2 им станет `StationStore`."""

    @property
    def stations(self) -> list[Station]: ...


def create_mpv() -> Any:
    """Создаёт проигрыватель libmpv с настройками приложения.

    Импорт локальный намеренно: модуль иначе требовал бы установленный libmpv
    везде, где его просто импортируют, — а логику состояний и реконнекта
    проверяют без всякого звука.
    """
    import mpv

    # libmpv падает при не-C числовой локали, а Qt выставляет системную.
    locale.setlocale(locale.LC_NUMERIC, "C")
    return mpv.MPV(video=False, ytdl=False)


class PlayerManager(QObject):
    #: Изменилось `state` или `error_message`.
    state_changed = Signal()
    #: Сменилась станция: `Station | None`.
    station_changed = Signal(object)
    #: Сменился трек: `TrackInfo | None`.
    track_changed = Signal(object)
    #: Сменилась обложка: `QImage | None`.
    artwork_changed = Signal(object)
    #: Фаза мигания во время переподключения.
    blink_toggled = Signal(bool)
    error_occurred = Signal(str)

    #: Внутренний мост из потока mpv в поток Qt.
    _mpv_signal = Signal(str, object)

    def __init__(
        self,
        store: StationsSource,
        parent: QObject | None = None,
        *,
        mpv_factory: Callable[[], Any] = create_mpv,
    ) -> None:
        super().__init__(parent)
        self._store = store

        self.state = PlaybackState.IDLE
        self.blink_on = False
        self.current_station: Station | None = None
        self.current_track: TrackInfo | None = None
        self.artwork_image: QImage | None = None
        self.artwork_path: str | None = None
        self.error_message: str | None = None

        self._should_be_playing = False
        self._provider: MetadataProvider | None = None

        state = storage.load_state()
        self._volume = int(state.get("volume", 100))

        self._mpv = mpv_factory()
        self._mpv["volume"] = self._volume
        self._mpv_signal.connect(self._handle_mpv_signal)
        self._mpv.observe_property("core-idle", self._emit_property)
        self._mpv.observe_property("paused-for-cache", self._emit_property)
        self._mpv.event_callback("end-file")(self._on_end_file)

        self._artwork = ArtworkLoader(self)
        self._artwork.artwork_ready.connect(self._on_artwork_ready)

        self._reconnect_timer = _make_timer(self, RECONNECT_INTERVAL_MS, self._attempt_reconnect)
        self._stall_timer = _make_timer(self, STALL_TIMEOUT_MS, self._on_stalled, single_shot=True)
        self._blink_timer = _make_timer(self, BLINK_INTERVAL_MS, self._on_blink)
        self._volume_save_timer = _make_timer(
            self, VOLUME_SAVE_DELAY_MS, self._save_volume, single_shot=True
        )

        self._restore_last_station(state.get("last_station_id"))

    # MARK: - Состояние

    @property
    def is_active(self) -> bool:
        """Пользователь ждёт звука: играет, загружается или переподключается.

        Именно это, а не факт звука, определяет вид кнопки, запрет засыпания
        и `PlaybackStatus` в MPRIS — иначе они мигали бы на каждой буферизации.
        """
        return self.state in _ACTIVE_STATES

    @property
    def is_playing(self) -> bool:
        """Эфир действительно идёт — по данным mpv, а не по нашему намерению."""
        return self.state is PlaybackState.PLAYING

    @property
    def is_loading(self) -> bool:
        return self.state is PlaybackState.LOADING

    @property
    def is_reconnecting(self) -> bool:
        return self.state is PlaybackState.RECONNECTING

    def _set_state(self, state: PlaybackState, *, error: str | None = None) -> None:
        if state is self.state and error == self.error_message:
            return
        self.state = state
        self.error_message = error
        self.state_changed.emit()

    # MARK: - Публичное управление

    def play(self, station: Station) -> None:
        self.current_station = station
        storage.update_state(last_station_id=station.id)
        self.station_changed.emit(station)

        self._set_track(None)
        self._should_be_playing = True
        self._reconnect_timer.stop()
        self._stop_blinking()

        self._start_metadata(station)
        self._load_and_play(station)

    def stop(self) -> None:
        self._should_be_playing = False
        self._reconnect_timer.stop()
        self._stall_timer.stop()
        self._stop_blinking()
        self._stop_metadata()

        with contextlib.suppress(OSError, RuntimeError):
            self._mpv.command("stop")

        self._set_track(None)
        self._set_state(PlaybackState.IDLE)

    def toggle(self) -> None:
        if self.is_active:
            self.stop()
        elif self.current_station is not None:
            self.play(self.current_station)
        elif self._store.stations:
            self.play(self._store.stations[0])

    def play_next(self) -> None:
        self._play_relative(1)

    def play_previous(self) -> None:
        self._play_relative(-1)

    @property
    def stations(self) -> list[Station]:
        return self._store.stations

    @property
    def volume(self) -> int:
        return self._volume

    def set_volume(self, value: int) -> None:
        self._volume = max(0, min(100, int(value)))
        try:
            self._mpv["volume"] = self._volume
        except (OSError, RuntimeError):
            return
        # Звук меняется сразу, диск ждёт: пользователь ведёт слайдер, а не
        # сохраняет настройку. Отсчёт начинается заново на каждом движении.
        self._volume_save_timer.start()

    def _save_volume(self) -> None:
        storage.update_state(volume=self._volume)

    def shutdown(self) -> None:
        """Снимает всё, что держит ресурсы. Вызывается перед выходом."""
        self._should_be_playing = False
        # Громкость, изменённую перед самым выходом, теряем только вместе
        # с несохранённым таймером — поэтому дописываем её здесь.
        if self._volume_save_timer.isActive():
            self._volume_save_timer.stop()
            self._save_volume()
        for timer in (self._reconnect_timer, self._stall_timer, self._blink_timer):
            timer.stop()
        self._stop_metadata()
        self._artwork.cancel_all()
        with contextlib.suppress(OSError, RuntimeError):
            self._mpv.terminate()

    # MARK: - Воспроизведение

    def _load_and_play(self, station: Station) -> None:
        """Запускает поток. Не трогает намерение и метаданные — используется и при
        первом запуске, и при каждом переподключении."""
        self._stall_timer.stop()
        try:
            self._mpv.play(station.url)
        except (OSError, RuntimeError) as exc:
            message = QCoreApplication.translate("PlayerManager", "Could not open the stream: {error}")
            self._fail(message.format(error=exc))
            return

        # Играем не мы, а mpv: пока он не подтвердил звук, состояние — загрузка.
        if not self.is_reconnecting:
            self._set_state(PlaybackState.LOADING)
        self._stall_timer.start()

    def _play_relative(self, offset: int) -> None:
        stations = self._store.stations
        if not stations:
            return
        if self.current_station is None:
            self.play(stations[0])
            return
        ids = [station.id for station in stations]
        try:
            index = ids.index(self.current_station.id)
        except ValueError:
            self.play(stations[0])
            return
        self.play(stations[(index + offset) % len(stations)])

    def _restore_last_station(self, last_id: str | None) -> None:
        if not last_id:
            return
        for station in self._store.stations:
            if station.id == last_id:
                self.current_station = station
                return

    # MARK: - События mpv

    def _emit_property(self, name: str, value) -> None:
        """Вызывается из потока mpv — только перебрасывает значение в поток Qt."""
        self._mpv_signal.emit(name, value)

    def _on_end_file(self, event) -> None:
        reason = getattr(getattr(event, "data", None), "reason", None)
        self._mpv_signal.emit("end-file", reason)

    @Slot(str, object)
    def _handle_mpv_signal(self, name: str, value) -> None:
        if name == "core-idle":
            if value is False:
                self._on_playback_started()
        elif name == "paused-for-cache" and value is True:
            if self._should_be_playing and not self.is_reconnecting:
                self._set_state(PlaybackState.LOADING)
                self._stall_timer.start()
        # Для live-радио и ошибка, и «конец файла» означают обрыв. Но не наши
        # собственные stop/quit и не redirect — там mpv продолжает сам.
        elif name == "end-file" and self._should_be_playing and _end_file_means_disconnect(value):
            self._begin_reconnecting()

    def _on_playback_started(self) -> None:
        """Поток реально пошёл: гасим индикаторы и сторожевые таймеры."""
        self._stall_timer.stop()
        if self.is_reconnecting:
            self._reconnect_timer.stop()
            self._stop_blinking()
        self._set_state(PlaybackState.PLAYING if self._should_be_playing else PlaybackState.IDLE)

    def _fail(self, message: str) -> None:
        # Во время переподключения неудачная попытка — часть цикла, а не событие:
        # индикатор иначе дёргался бы между «переподключение» и ошибкой.
        if self.is_reconnecting:
            return
        # Намерение играть не сбрасываем: вернётся сеть — попробуем снова.
        self._set_state(PlaybackState.FAILED, error=message)
        self.error_occurred.emit(message)

    # MARK: - Реконнект

    def _begin_reconnecting(self) -> None:
        if not self._should_be_playing or self.is_reconnecting:
            return
        self._stall_timer.stop()
        self._set_state(PlaybackState.RECONNECTING)
        self._blink_timer.start()
        self._reconnect_timer.start()

    def _attempt_reconnect(self) -> None:
        if not self._should_be_playing or self.current_station is None:
            self._reconnect_timer.stop()
            return
        self._load_and_play(self.current_station)

    def _on_stalled(self) -> None:
        """Буферизация затянулась — считаем поток зависшим."""
        if self._should_be_playing and not self.is_reconnecting:
            self._begin_reconnecting()

    def _on_blink(self) -> None:
        self.blink_on = not self.blink_on
        self.blink_toggled.emit(self.blink_on)

    def _stop_blinking(self) -> None:
        self._blink_timer.stop()
        if self.blink_on:
            self.blink_on = False
            self.blink_toggled.emit(False)

    def on_network_reachable(self, reachable: bool) -> None:
        """Реакция на изменение доступности сети (аналог NWPathMonitor)."""
        if not self._should_be_playing:
            return
        if reachable:
            # Сеть вернулась — пробуем сразу, не дожидаясь интервала цикла.
            if self.is_reconnecting:
                self._attempt_reconnect()
        else:
            self._begin_reconnecting()

    # MARK: - Метаданные

    def _start_metadata(self, station: Station) -> None:
        self._stop_metadata()
        provider = make_provider(station, self._mpv, self)
        provider.track_changed.connect(self._on_track_changed)
        provider.track_cleared.connect(lambda: self._set_track(None))
        self._provider = provider
        provider.start(station)

    def _stop_metadata(self) -> None:
        if self._provider is None:
            return
        provider, self._provider = self._provider, None
        provider.stop()
        provider.deleteLater()

    @Slot(object)
    def _on_track_changed(self, track: TrackInfo) -> None:
        previous = self.current_track
        self.current_track = track
        self.track_changed.emit(track)

        if track.artwork_url:
            self._artwork.load(track.artwork_url)
        elif previous is None or previous.title != track.title:
            # Новый трек без обложки — старую показывать нельзя.
            self._set_artwork(None, None)

    def _set_track(self, track: TrackInfo | None) -> None:
        if track is None and self.current_track is None:
            return
        self.current_track = track
        self.track_changed.emit(track)
        if track is None:
            self._set_artwork(None, None)

    @Slot(str, str, QImage)
    def _on_artwork_ready(self, url: str, path: str, image: QImage) -> None:
        if self.current_track is None or self.current_track.artwork_url != url:
            return  # Трек успел смениться.
        self._set_artwork(image, path or None)

    def _set_artwork(self, image: QImage | None, path: str | None) -> None:
        self.artwork_image = image
        self.artwork_path = path
        self.artwork_changed.emit(image)


#: Коды `mpv_end_file_reason`. python-mpv отдаёт их числом, а не enum,
#: поэтому сравнивать со строками нельзя.
_END_FILE_REASONS = {0: "eof", 2: "stop", 3: "quit", 4: "error", 5: "redirect"}
#: Причины, которые для live-потока означают именно обрыв.
_DISCONNECT_REASONS = frozenset({"eof", "error"})


def _end_file_means_disconnect(reason) -> bool:
    if isinstance(reason, int):
        name = _END_FILE_REASONS.get(reason, "")
    else:
        # На случай, если версия python-mpv отдаст enum или строку.
        name = str(getattr(reason, "name", reason)).rsplit(".", 1)[-1].lower()
    return name in _DISCONNECT_REASONS


def _make_timer(parent: QObject, interval_ms: int, slot, *, single_shot: bool = False) -> QTimer:
    timer = QTimer(parent)
    timer.setInterval(interval_ms)
    timer.setSingleShot(single_shot)
    timer.timeout.connect(slot)
    return timer
