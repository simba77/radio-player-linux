"""Опрос HLS-манифеста.

Сеть не трогаем: ответы сервера подаются прямо в обработчики. Проверяется то,
из-за чего провайдер зря нагружал чужие серверы — он выбирается по расширению
`.m3u8`, а тег `#EXT-X-DATERANGE` отдаёт только hostingradio.
"""

from __future__ import annotations

import pytest

from radio_player.metadata.hls_daterange import (
    _EMPTY_POLLS_BEFORE_GIVING_UP,
    HlsDaterangeProvider,
)
from radio_player.models import TrackInfo

pytestmark = pytest.mark.usefixtures("qt_app")

PLAIN_MANIFEST = "#EXTM3U\n#EXT-X-TARGETDURATION:6\nsegment1.ts\n"
MANIFEST_WITH_TRACK = (
    "#EXTM3U\n"
    '#EXT-X-DATERANGE:ID="abc",CLASS="ru.hostingradio.meta",'
    'X-ARTIST="Кто-то",X-TITLE="Что-то"\n'
    "segment1.ts\n"
)


@pytest.fixture
def provider():
    made = HlsDaterangeProvider()
    # Состояние «мастер-плейлист разобран, идёт опрос variant» выставляется
    # напрямую: иначе провайдер полез бы в сеть за настоящим манифестом.
    made._variant_url = "https://example.org/live/variant.m3u8?hlssid=xyz"
    made._timer.start()
    yield made
    made.stop()


def test_master_playlist_selects_the_variant(monkeypatch):
    """Ссылка на variant несёт hlssid — без него DATERANGE не отдаётся."""
    made = HlsDaterangeProvider()
    monkeypatch.setattr(made, "_poll_variant", lambda: None)
    made._master_url = "https://example.org/live/playlist.m3u8"

    made._handle_master("#EXTM3U\n#EXT-X-STREAM-INF:BANDWIDTH=128000\nvariant.m3u8?hlssid=xyz\n")
    assert made._variant_url == "https://example.org/live/variant.m3u8?hlssid=xyz"
    made.stop()


def test_media_playlist_without_variants_is_polled_directly(monkeypatch):
    made = HlsDaterangeProvider()
    monkeypatch.setattr(made, "_poll_variant", lambda: None)
    made._master_url = "https://example.org/live/playlist.m3u8"

    made._handle_master(PLAIN_MANIFEST)
    assert made._variant_url == made._master_url
    made.stop()


def test_track_is_read_from_the_manifest(provider):
    tracks: list[TrackInfo] = []
    provider.track_changed.connect(tracks.append)

    provider._handle_variant(MANIFEST_WITH_TRACK)
    assert [(t.artist, t.title) for t in tracks] == [("Кто-то", "Что-то")]


def test_polling_continues_while_metadata_appears(provider):
    for _ in range(_EMPTY_POLLS_BEFORE_GIVING_UP * 2):
        provider._handle_variant(MANIFEST_WITH_TRACK)
    assert provider._timer.isActive() is True


def test_polling_stops_when_the_station_never_has_metadata(provider):
    """Час прослушивания чужой HLS-станции — 720 бесполезных запросов."""
    for _ in range(_EMPTY_POLLS_BEFORE_GIVING_UP - 1):
        provider._handle_variant(PLAIN_MANIFEST)
    assert provider._timer.isActive() is True

    provider._handle_variant(PLAIN_MANIFEST)
    assert provider._timer.isActive() is False


def test_a_single_track_resets_the_patience(provider):
    """Метаданные могут пропадать между треками — это не повод сдаваться."""
    for _ in range(_EMPTY_POLLS_BEFORE_GIVING_UP - 1):
        provider._handle_variant(PLAIN_MANIFEST)
    provider._handle_variant(MANIFEST_WITH_TRACK)

    for _ in range(_EMPTY_POLLS_BEFORE_GIVING_UP - 1):
        provider._handle_variant(PLAIN_MANIFEST)
    assert provider._timer.isActive() is True


def test_stop_clears_the_state(provider):
    provider._handle_variant(MANIFEST_WITH_TRACK)
    provider.stop()

    assert provider._timer.isActive() is False
    assert provider._last_range_id is None
    assert provider._empty_polls == 0
