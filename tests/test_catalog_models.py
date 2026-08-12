"""Валидация каталогов и оверлея.

Каталог приходит из сети, оверлей — с диска: обе стороны могут оказаться битыми,
и ни одна не должна ронять приложение или обнулять список станций.
"""

from __future__ import annotations

import json

from radio_player import catalog as catalog_module
from radio_player.models import Catalog, Overlay, Station

VALID = {
    "schema": 1,
    "id": "record",
    "name": "Radio Record",
    "revision": 3,
    "stations": [{"id": "record-main", "name": "Main", "url": "https://example.org/main.m3u8"}],
}


def test_valid_catalog_parses():
    parsed = Catalog.from_dict(VALID)
    assert parsed is not None
    assert parsed.id == "record"
    assert parsed.revision == 3
    assert parsed.stations[0].id == "record-main"


def test_catalog_with_newer_schema_is_rejected():
    assert Catalog.from_dict({**VALID, "schema": 99}) is None


def test_catalog_without_schema_is_rejected():
    payload = {key: value for key, value in VALID.items() if key != "schema"}
    assert Catalog.from_dict(payload) is None


def test_catalog_without_stations_is_rejected():
    assert Catalog.from_dict({**VALID, "stations": []}) is None


def test_catalog_skips_broken_stations_but_keeps_good_ones():
    parsed = Catalog.from_dict(
        {
            **VALID,
            "stations": [
                {"id": "ok", "name": "Хорошая", "url": "https://example.org/ok.m3u8"},
                {"id": "no-url", "name": "Без адреса"},
                {"name": "Без id", "url": "https://example.org/x.m3u8"},
                "мусор",
            ],
        }
    )
    assert parsed is not None
    assert [s.id for s in parsed.stations] == ["ok"]


def test_catalog_ignores_duplicate_station_ids():
    parsed = Catalog.from_dict(
        {
            **VALID,
            "stations": [
                {"id": "dup", "name": "Первая", "url": "https://example.org/1.m3u8"},
                {"id": "dup", "name": "Вторая", "url": "https://example.org/2.m3u8"},
            ],
        }
    )
    assert parsed is not None
    assert [s.name for s in parsed.stations] == ["Первая"]


def test_catalog_ignores_unknown_fields():
    parsed = Catalog.from_dict(
        {
            **VALID,
            "publisher": "кто-то",
            "stations": [
                {"id": "x", "name": "X", "url": "https://example.org/x.m3u8", "tags": ["electronic"]},
            ],
        }
    )
    assert parsed is not None


def test_station_accepts_macos_field_name():
    """macOS-версия писала поле `urlString` — нужно для импорта."""
    station = Station.from_dict({"id": "x", "name": "X", "urlString": "https://example.org/x.m3u8"})
    assert station is not None
    assert station.url == "https://example.org/x.m3u8"


def test_overlay_roundtrip():
    overlay = Overlay(
        order=["a", "b"],
        hidden={"c"},
        overrides={"a": {"name": "Моё"}},
        custom=[Station(id="local:1", name="Своя", url="https://example.org/s.mp3")],
    )
    restored = Overlay.from_dict(overlay.to_dict())
    assert restored.order == ["a", "b"]
    assert restored.hidden == {"c"}
    assert restored.overrides == {"a": {"name": "Моё"}}
    assert restored.custom[0].id == "local:1"


def test_broken_overlay_degrades_to_empty():
    assert Overlay.from_dict("не словарь").custom == []
    assert Overlay.from_dict({"custom": ["мусор"], "overrides": {"a": "строка"}}).overrides == {}


def test_bundled_catalog_is_valid():
    """Встроенный каталог обязан парситься — иначе приложение стартует пустым."""
    catalogs = catalog_module.load_bundled()
    assert len(catalogs) == 1
    assert catalogs[0].id == "record"
    assert len(catalogs[0].stations) == 6


BUNDLED_URL = catalog_module.DEFAULT_CATALOG_URLS[0]
EXTRA_URL = "https://example.org/catalog.json"


def put_in_cache(url: str, data: dict) -> None:
    path = catalog_module._cache_path(url)
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(data), encoding="utf-8")


def test_load_initial_falls_back_to_bundled_without_cache():
    assert [c.id for c in catalog_module.load_initial()] == ["record"]


def test_load_initial_prefers_cache_over_bundled():
    put_in_cache(BUNDLED_URL, {**VALID, "id": "record", "revision": 99, "name": "Из кэша"})

    loaded = catalog_module.load_initial()
    assert [c.name for c in loaded] == ["Из кэша"]


def test_bundled_wins_when_it_is_newer_than_cache():
    """После обновления приложения встроенная копия может обогнать кэш."""
    put_in_cache(BUNDLED_URL, {**VALID, "id": "record", "revision": 0, "name": "Устаревший кэш"})

    loaded = catalog_module.load_initial()
    assert [c.name for c in loaded] == ["Radio Record"]


def test_cache_of_a_removed_catalog_is_ignored():
    """Каталог, убранный из настроек, больше не поставляет станции."""
    put_in_cache(EXTRA_URL, {**VALID, "id": "extra", "name": "Лишний"})

    with_extra = catalog_module.load_initial([BUNDLED_URL, EXTRA_URL])
    assert "extra" in [c.id for c in with_extra]

    without_extra = catalog_module.load_initial([BUNDLED_URL])
    assert "extra" not in [c.id for c in without_extra]


def test_cache_of_a_removed_catalog_is_deleted():
    put_in_cache(EXTRA_URL, {**VALID, "id": "extra", "name": "Лишний"})
    catalog_module.load_initial([BUNDLED_URL])
    assert not catalog_module._cache_path(EXTRA_URL).exists()


def test_pruning_keeps_the_etags_file():
    etags_path = catalog_module._etags_path()
    etags_path.parent.mkdir(parents=True, exist_ok=True)
    etags_path.write_text(json.dumps({BUNDLED_URL: '"tag"'}), encoding="utf-8")

    catalog_module.prune_cache([BUNDLED_URL])
    assert catalog_module._etags() == {BUNDLED_URL: '"tag"'}


def test_pruning_forgets_etags_of_removed_catalogs():
    etags_path = catalog_module._etags_path()
    etags_path.parent.mkdir(parents=True, exist_ok=True)
    etags_path.write_text(json.dumps({BUNDLED_URL: '"a"', EXTRA_URL: '"b"'}), encoding="utf-8")

    catalog_module.prune_cache([BUNDLED_URL])
    assert catalog_module._etags() == {BUNDLED_URL: '"a"'}


def test_cached_catalogs_follow_the_order_of_sources():
    """Порядок источников решает, кто победит при совпадении id станций."""
    put_in_cache(BUNDLED_URL, {**VALID, "id": "record", "name": "Первый"})
    put_in_cache(EXTRA_URL, {**VALID, "id": "extra", "name": "Второй"})

    assert [c.name for c in catalog_module.load_cached([BUNDLED_URL, EXTRA_URL])] == [
        "Первый",
        "Второй",
    ]
    assert [c.name for c in catalog_module.load_cached([EXTRA_URL, BUNDLED_URL])] == [
        "Второй",
        "Первый",
    ]


# MARK: - Схема адреса


def test_station_with_a_non_web_url_is_rejected():
    """Каталог приходит из сети, а mpv откроет всё, что ему дадут."""
    for url in ("file:///etc/passwd", "avdevice://x", "/home/user/track.mp3", "ftp://example.org/s"):
        assert Station.from_dict({"id": "x", "name": "X", "url": url}) is None


def test_station_with_a_web_url_is_accepted():
    for url in ("http://example.org/stream", "HTTPS://example.org/stream.m3u8"):
        assert Station.from_dict({"id": "x", "name": "X", "url": url}) is not None


def test_catalog_drops_stations_with_unplayable_urls():
    parsed = Catalog.from_dict(
        {
            **VALID,
            "stations": [
                {"id": "ok", "name": "Хорошая", "url": "https://example.org/ok.m3u8"},
                {"id": "evil", "name": "Файл", "url": "file:///etc/shadow"},
            ],
        }
    )
    assert parsed is not None
    assert [s.id for s in parsed.stations] == ["ok"]
