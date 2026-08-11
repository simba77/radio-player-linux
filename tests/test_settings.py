"""Настройки и автозапуск."""

from __future__ import annotations

from radio_player import autostart, paths
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


# MARK: - Автозапуск


def test_autostart_disabled_by_default():
    assert autostart.is_enabled() is False


def test_autostart_creates_and_removes_desktop_file():
    assert autostart.set_enabled(True) is True
    assert autostart.is_enabled() is True
    assert autostart.set_enabled(False) is False
    assert paths.autostart_file().exists() is False


def _exec_line() -> str:
    autostart.set_enabled(True)
    return next(
        line for line in paths.autostart_file().read_text(encoding="utf-8").splitlines()
        if line.startswith("Exec=")
    )


def test_autostart_from_sources_passes_pythonpath(monkeypatch):
    """При запуске из исходников автозапуску нужен явный PYTHONPATH:
    сессия стартует приложение из чужого рабочего каталога."""
    monkeypatch.setattr(autostart.shutil, "which", lambda _name: None)
    monkeypatch.setattr(autostart, "_is_installed", lambda _root: False)
    exec_line = _exec_line()
    assert "-m radio_player" in exec_line
    assert "PYTHONPATH=" in exec_line


def test_autostart_prefers_installed_launcher(monkeypatch):
    """Если приложение установлено, автозапуск должен звать его обёртку."""
    monkeypatch.setattr(autostart.shutil, "which", lambda _name: "/usr/bin/radio-player")
    assert _exec_line() == "Exec=/usr/bin/radio-player"
