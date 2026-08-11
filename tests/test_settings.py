"""Настройки приложения. Автозапуск — в `test_autostart.py`."""

from __future__ import annotations

from radio_player import settings as settings_module
from radio_player.settings import MAX_UPDATE_INTERVAL_HOURS, Settings


def test_defaults_when_file_missing():
    loaded = settings_module.load()
    assert loaded.close_to_tray is True
    assert loaded.update_interval_hours == 24
    assert loaded.extra_catalog_urls == []


def test_roundtrip():
    settings_module.save(
        Settings(close_to_tray=False, update_interval_hours=6, extra_catalog_urls=["https://example.org/c.json"])
    )
    loaded = settings_module.load()
    assert loaded.close_to_tray is False
    assert loaded.update_interval_hours == 6
    assert loaded.extra_catalog_urls == ["https://example.org/c.json"]


def test_interval_is_clamped():
    assert Settings.from_dict({"update_interval_hours": 0}).update_interval_hours == 1
    huge = Settings.from_dict({"update_interval_hours": 10**6})
    assert huge.update_interval_hours == MAX_UPDATE_INTERVAL_HOURS


def test_broken_values_fall_back_to_defaults():
    loaded = Settings.from_dict({"update_interval_hours": "часто", "extra_catalog_urls": "нет"})
    assert loaded.update_interval_hours == 24
    assert loaded.extra_catalog_urls == []


def test_non_http_catalog_urls_are_dropped():
    loaded = Settings.from_dict({"extra_catalog_urls": ["ftp://example.org/c.json", "/etc/passwd",
                                                        "https://example.org/ok.json"]})
    assert loaded.extra_catalog_urls == ["https://example.org/ok.json"]


def test_update_interval_seconds():
    assert Settings(update_interval_hours=2).update_interval_seconds == 7200
