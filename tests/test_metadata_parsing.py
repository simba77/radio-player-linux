"""Тесты разбора метаданных: чистые функции, без сети и без Qt-цикла."""

from __future__ import annotations

import json

from radio_player.metadata.hls_daterange import _extract_cover, _parse_daterange
from radio_player.models import split_icy_title

VARIANT_PLAYLIST = """#EXTM3U
#EXT-X-VERSION:3
#EXT-X-TARGETDURATION:6
#EXT-X-PROGRAM-DATE-TIME:2026-08-10T20:09:11.371Z
#EXT-X-DATERANGE:ID="6a7a2f97",CLASS="ru.hostingradio.meta",START-DATE="2026-08-10T20:07:50.657Z",\
END-ON-NEXT=YES,X-ARTIST="BLOND_ISH",X-TITLE="$MONEY",X-METADATA-URI="metadata/6a7a2f97.json"
#EXT-X-DISCONTINUITY
#EXTINF:5.990748,
l0_6a7a2ff0.aac
"""


def test_parse_daterange_extracts_track_attributes():
    attrs = _parse_daterange(VARIANT_PLAYLIST)
    assert attrs is not None
    assert attrs["ID"] == "6a7a2f97"
    assert attrs["X-ARTIST"] == "BLOND_ISH"
    assert attrs["X-TITLE"] == "$MONEY"
    assert attrs["X-METADATA-URI"] == "metadata/6a7a2f97.json"


def test_parse_daterange_ignores_ranges_without_track_info():
    playlist = '#EXTM3U\n#EXT-X-DATERANGE:ID="x",CLASS="com.apple.hls.interstitial"\n'
    assert _parse_daterange(playlist) is None


def test_parse_daterange_returns_none_without_tag():
    assert _parse_daterange("#EXTM3U\n#EXTINF:6.0,\nseg.aac\n") is None


def test_extract_cover_prefers_300px():
    body = json.dumps(
        {
            "current": {
                "title": "X",
                "coverImageUrl100": "https://example.org/100.jpg",
                "coverImageUrl300": "https://example.org/300.jpg",
                "coverImageUrl600": "https://example.org/600.jpg",
            }
        }
    )
    assert _extract_cover(body) == "https://example.org/300.jpg"


def test_extract_cover_handles_track_without_artwork():
    """Часть станций отдаёт current без обложки — это штатная ситуация."""
    body = json.dumps({"current": {"artist": "BLOND_ISH", "title": "$MONEY"}})
    assert _extract_cover(body) is None


def test_extract_cover_survives_broken_json():
    assert _extract_cover("не json") is None


def test_split_icy_title_separates_artist():
    track = split_icy_title("TIESTO/BIA/21 SAVAGE - Both (Record Mix)")
    assert track is not None
    assert track.artist == "TIESTO/BIA/21 SAVAGE"
    assert track.title == "Both (Record Mix)"
    assert track.display == "TIESTO/BIA/21 SAVAGE — Both (Record Mix)"


def test_split_icy_title_without_separator_is_all_title():
    track = split_icy_title("Просто название")
    assert track is not None
    assert track.artist is None
    assert track.title == "Просто название"


def test_split_icy_title_ignores_empty():
    assert split_icy_title("   ") is None
