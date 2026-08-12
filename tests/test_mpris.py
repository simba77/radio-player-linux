"""MPRIS2.

Главный тест здесь — про сигнатуру `PropertiesChanged`. Ошибка в ней не видна
ни в `dbus-monitor`, ни при чтении свойств: сигнал уходит, но Qt-клиенты
(plasmashell, kded) его молча игнорируют, и плеер не получает медиаклавиши.
"""

from __future__ import annotations

import pytest

pytest.importorskip("PySide6.QtDBus")

from PySide6.QtCore import QObject, Signal
from PySide6.QtGui import QImage

from radio_player import mpris
from radio_player.models import Station, TrackInfo

pytestmark = pytest.mark.usefixtures("qt_app")


def test_invalidated_properties_is_string_array():
    """Пустой список Python Qt отправил бы как `av`, а спецификация требует `as`."""
    assert mpris._empty_string_list().currentSignature() == "as"


class FakePlayer:
    """Минимум, который читают `_metadata` и адаптер плеера."""

    def __init__(self, station=None, track=None, artwork_path=None, is_active=False):
        self.current_station = station
        self.current_track = track
        self.artwork_path = artwork_path
        self.is_active = is_active


STATION = Station(id="s1", name="Моя станция", url="https://example.org/s.m3u8")


def test_metadata_without_track_shows_station():
    metadata = mpris._metadata(FakePlayer(station=STATION))
    assert metadata["xesam:title"] == "Моя станция"
    assert "mpris:trackid" in metadata


def test_metadata_with_track():
    track = TrackInfo(title="Трек", artist="Исполнитель")
    metadata = mpris._metadata(FakePlayer(station=STATION, track=track))
    assert metadata["xesam:title"] == "Трек"
    assert metadata["xesam:artist"] == ["Исполнитель"]
    assert metadata["xesam:album"] == "Моя станция"


def test_metadata_artist_is_always_a_list():
    """`xesam:artist` по спецификации — массив строк, даже когда исполнитель один."""
    track = TrackInfo(title="Трек")
    metadata = mpris._metadata(FakePlayer(station=STATION, track=track))
    assert metadata["xesam:artist"] == []


def test_metadata_artwork_is_file_uri(tmp_path):
    cover = tmp_path / "cover.jpg"
    QImage(4, 4, QImage.Format.Format_RGB32).save(str(cover))
    track = TrackInfo(title="Трек", artwork_url="https://example.org/c.jpg")
    metadata = mpris._metadata(FakePlayer(station=STATION, track=track, artwork_path=str(cover)))
    assert metadata["mpris:artUrl"].startswith("file:///")


def test_metadata_without_artwork_has_no_art_url():
    metadata = mpris._metadata(FakePlayer(station=STATION))
    assert "mpris:artUrl" not in metadata


def test_metadata_without_station_is_still_valid():
    """Плеер без выбранной станции обязан отдавать корректный trackid."""
    metadata = mpris._metadata(FakePlayer())
    assert "mpris:trackid" in metadata


# MARK: - Состояние воспроизведения


def playback_status(*, is_active: bool) -> str:
    owner = QObject()
    adaptor = mpris._PlayerAdaptor(owner, FakePlayer(station=STATION, is_active=is_active))
    return adaptor.PlaybackStatus


def test_playback_status_reports_activity_not_sound():
    """Буферизация и переподключение остаются «Playing».

    Иначе апплет «Медиаплеер» переключался бы в «остановлено» на каждой
    заминке потока, а вместе с ним прыгала бы кнопка на медиаклавише.
    """
    assert playback_status(is_active=True) == "Playing"
    assert playback_status(is_active=False) == "Stopped"


class SignallingPlayer(QObject):
    """Плеер с сигналами — их слушает `MprisServer`."""

    state_changed = Signal()
    track_changed = Signal(object)
    station_changed = Signal(object)
    artwork_changed = Signal(object)
    volume_changed = Signal(int)

    def __init__(self):
        super().__init__()
        self.current_station = STATION
        self.current_track = None
        self.artwork_path = None
        self.is_active = False
        self.volume = 100
        self.stations = [STATION]

    def set_volume(self, value):
        self.volume = value
        self.volume_changed.emit(value)


def test_volume_change_is_published(monkeypatch):
    """Без этого слайдер громкости в апплете Plasma остаётся на старом значении."""
    player = SignallingPlayer()
    server = mpris.MprisServer(player, on_raise=lambda: None, on_quit=lambda: None)
    published: list[dict] = []
    monkeypatch.setattr(server, "_emit_properties", published.append)

    player.set_volume(42)
    assert published == [{"Volume": pytest.approx(0.42)}]
