"""MPRIS2.

Главный тест здесь — про сигнатуру `PropertiesChanged`. Ошибка в ней не видна
ни в `dbus-monitor`, ни при чтении свойств: сигнал уходит, но Qt-клиенты
(plasmashell, kded) его молча игнорируют, и плеер не получает медиаклавиши.
"""

from __future__ import annotations

import pytest

pytest.importorskip("PySide6.QtDBus")

from PySide6.QtCore import QCoreApplication  # noqa: E402
from PySide6.QtGui import QImage  # noqa: E402

from radio_player import mpris  # noqa: E402
from radio_player.models import Station, TrackInfo  # noqa: E402


@pytest.fixture(scope="module", autouse=True)
def qt_app():
    app = QCoreApplication.instance() or QCoreApplication([])
    yield app


def test_invalidated_properties_is_string_array():
    """Пустой список Python Qt отправил бы как `av`, а спецификация требует `as`."""
    assert mpris._empty_string_list().currentSignature() == "as"


class FakePlayer:
    """Минимум, который читает `_metadata`."""

    def __init__(self, station=None, track=None, artwork_path=None):
        self.current_station = station
        self.current_track = track
        self.artwork_path = artwork_path


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
