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
    parsed = Catalog.from_dict({
        **VALID,
        "stations": [
            {"id": "ok", "name": "Хорошая", "url": "https://example.org/ok.m3u8"},
            {"id": "no-url", "name": "Без адреса"},
            {"name": "Без id", "url": "https://example.org/x.m3u8"},
            "мусор",
        ],
    })
    assert parsed is not None
    assert [s.id for s in parsed.stations] == ["ok"]


def test_catalog_ignores_duplicate_station_ids():
    parsed = Catalog.from_dict({
        **VALID,
        "stations": [
            {"id": "dup", "name": "Первая", "url": "https://example.org/1.m3u8"},
            {"id": "dup", "name": "Вторая", "url": "https://example.org/2.m3u8"},
        ],
    })
    assert parsed is not None
    assert [s.name for s in parsed.stations] == ["Первая"]


def test_catalog_ignores_unknown_fields():
    parsed = Catalog.from_dict({**VALID, "publisher": "кто-то", "stations": [
        {"id": "x", "name": "X", "url": "https://example.org/x.m3u8", "tags": ["electronic"]},
    ]})
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


def test_load_initial_falls_back_to_bundled_without_cache():
    assert [c.id for c in catalog_module.load_initial()] == ["record"]


def test_load_initial_prefers_cache_over_bundled(tmp_path):
    cached = {**VALID, "id": "record", "revision": 99, "name": "Из кэша"}
    cache_dir = catalog_module._cache_dir()
    cache_dir.mkdir(parents=True, exist_ok=True)
    (cache_dir / "record.json").write_text(json.dumps(cached), encoding="utf-8")

    loaded = catalog_module.load_initial()
    assert [c.name for c in loaded] == ["Из кэша"]


def test_bundled_wins_when_it_is_newer_than_cache():
    """После обновления приложения встроенная копия может обогнать кэш."""
    stale = {**VALID, "id": "record", "revision": 0, "name": "Устаревший кэш"}
    cache_dir = catalog_module._cache_dir()
    cache_dir.mkdir(parents=True, exist_ok=True)
    (cache_dir / "record.json").write_text(json.dumps(stale), encoding="utf-8")

    loaded = catalog_module.load_initial()
    assert [c.name for c in loaded] == ["Radio Record"]
