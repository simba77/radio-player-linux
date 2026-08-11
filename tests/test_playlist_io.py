"""Импорт и экспорт списков станций."""

from __future__ import annotations

import json

from radio_player import playlist_io
from radio_player.models import Station

M3U = """#EXTM3U
#EXTINF:-1,Radio Record — Main
https://example.org/record/playlist.m3u8
# просто комментарий
#EXTINF:-1,Станция, с запятой
https://example.org/two.mp3
https://example.org/three.aac
/local/file.mp3
"""

PLS = """[playlist]
NumberOfEntries=2
File1=https://example.org/one.mp3
Title1=Первая
Length1=-1
File2=https://example.org/two.mp3
Title2=Вторая
"""

MACOS_JSON = json.dumps([
    {"id": "5F5E1E9A-0000-0000-0000-000000000001", "name": "Lady Waks", "urlString": "https://example.org/lw.m3u8"},
    {"id": "5F5E1E9A-0000-0000-0000-000000000002", "name": "Main", "urlString": "https://example.org/main.m3u8"},
])


def test_parse_m3u_reads_names_and_urls():
    stations = playlist_io.parse(M3U)
    assert [s.name for s in stations] == [
        "Radio Record — Main",
        "Станция, с запятой",
        "example.org",
    ]
    assert stations[0].url == "https://example.org/record/playlist.m3u8"


def test_parse_m3u_skips_local_files():
    """Плеер работает с сетевыми потоками — локальные пути не импортируем."""
    assert all(s.url.startswith("http") for s in playlist_io.parse(M3U))


def test_parse_pls_pairs_titles_with_urls():
    stations = playlist_io.parse(PLS)
    assert [(s.name, s.url) for s in stations] == [
        ("Первая", "https://example.org/one.mp3"),
        ("Вторая", "https://example.org/two.mp3"),
    ]


def test_parse_pls_detected_by_extension_without_header():
    stations = playlist_io.parse("File1=https://example.org/x.mp3\nTitle1=X\n", filename="list.pls")
    assert [s.name for s in stations] == ["X"]


def test_parse_macos_json():
    stations = playlist_io.parse(MACOS_JSON)
    assert [s.name for s in stations] == ["Lady Waks", "Main"]
    assert stations[0].url == "https://example.org/lw.m3u8"


def test_imported_stations_become_local():
    """Чужие id не должны притворяться станциями каталога."""
    stations = playlist_io.parse(MACOS_JSON)
    assert all(station.is_custom for station in stations)


def test_parse_broken_input_returns_empty():
    assert playlist_io.parse("{не json") == []
    assert playlist_io.parse("") == []


def test_export_m3u_roundtrip():
    original = [
        Station(name="Первая", url="https://example.org/one.mp3"),
        Station(name="Вторая", url="https://example.org/two.m3u8"),
    ]
    restored = playlist_io.parse(playlist_io.export_m3u(original))
    assert [(s.name, s.url) for s in restored] == [(s.name, s.url) for s in original]


def test_read_file_missing_returns_empty(tmp_path):
    assert playlist_io.read_file(tmp_path / "нет.m3u") == []
