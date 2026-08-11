"""Автозапуск при входе в систему.

Отдельная тема от настроек: состояние живёт не в `settings.json`, а в наличии
`.desktop`-файла — второго источника правды здесь нет намеренно.
"""

from __future__ import annotations

from radio_player import autostart, paths


def test_disabled_by_default():
    assert autostart.is_enabled() is False


def test_creates_and_removes_desktop_file():
    assert autostart.set_enabled(True) is True
    assert autostart.is_enabled() is True
    assert autostart.set_enabled(False) is False
    assert paths.autostart_file().exists() is False


def _exec_line() -> str:
    autostart.set_enabled(True)
    return next(
        line
        for line in paths.autostart_file().read_text(encoding="utf-8").splitlines()
        if line.startswith("Exec=")
    )


def test_from_sources_passes_pythonpath(monkeypatch):
    """При запуске из исходников автозапуску нужен явный PYTHONPATH:
    сессия стартует приложение из чужого рабочего каталога."""
    monkeypatch.setattr(autostart.shutil, "which", lambda _name: None)
    monkeypatch.setattr(autostart, "_is_installed", lambda _root: False)
    exec_line = _exec_line()
    assert "-m radio_player" in exec_line
    assert "PYTHONPATH=" in exec_line


def test_prefers_installed_launcher(monkeypatch):
    """Если приложение установлено, автозапуск должен звать его обёртку."""
    monkeypatch.setattr(autostart.shutil, "which", lambda _name: "/usr/bin/radio-player")
    assert _exec_line() == "Exec=/usr/bin/radio-player"


# MARK: - Переименование в reverse-DNS


def enable_legacy_entry() -> None:
    """Автозапуск, включённый версией до переименования."""
    legacy = paths.legacy_autostart_file()
    legacy.parent.mkdir(parents=True, exist_ok=True)
    legacy.write_text("[Desktop Entry]\n", encoding="utf-8")


def test_legacy_entry_still_counts_as_enabled():
    """Сессия читает каталог целиком, поэтому старый файл продолжает работать."""
    enable_legacy_entry()
    assert autostart.is_enabled() is True


def test_enabling_replaces_the_legacy_entry():
    enable_legacy_entry()
    autostart.set_enabled(True)
    assert paths.autostart_file().exists() is True
    # Иначе приложение запускалось бы дважды.
    assert paths.legacy_autostart_file().exists() is False


def test_disabling_removes_the_legacy_entry():
    enable_legacy_entry()
    assert autostart.set_enabled(False) is False
    assert autostart.is_enabled() is False
    assert paths.legacy_autostart_file().exists() is False
