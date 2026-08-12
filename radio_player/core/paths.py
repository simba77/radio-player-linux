"""Пути к пользовательским файлам по XDG Base Directory."""

from __future__ import annotations

import os
from pathlib import Path

from .. import APP_ID, APP_RDNS_ID


def _base(env_var: str, fallback: str) -> Path:
    raw = os.environ.get(env_var)
    root = Path(raw) if raw else Path.home() / fallback
    return root / APP_ID


def config_dir() -> Path:
    """`~/.config/radio-player` — settings.json, overlay.json, state.json."""
    return _base("XDG_CONFIG_HOME", ".config")


def cache_dir() -> Path:
    """`~/.cache/radio-player` — кэш каталогов и обложек."""
    return _base("XDG_CACHE_HOME", ".cache")


def autostart_file() -> Path:
    """`.desktop`-файл автозапуска (аналог SMAppService на macOS)."""
    return _autostart_dir() / f"{APP_RDNS_ID}.desktop"


def legacy_autostart_file() -> Path:
    """Как файл автозапуска назывался до перехода на reverse-DNS.

    Он остался у тех, кто включил автозапуск в ранних версиях, и продолжает
    работать сам по себе — поэтому его нужно убирать явно, иначе приложение
    будет запускаться дважды.
    """
    return _autostart_dir() / f"{APP_ID}.desktop"


def _autostart_dir() -> Path:
    raw = os.environ.get("XDG_CONFIG_HOME")
    root = Path(raw) if raw else Path.home() / ".config"
    return root / "autostart"


def ensure_dirs() -> None:
    """Создаёт директории, в которые приложение пишет. Идемпотентно."""
    for path in (config_dir(), cache_dir() / "catalogs", cache_dir() / "artwork"):
        path.mkdir(parents=True, exist_ok=True)
