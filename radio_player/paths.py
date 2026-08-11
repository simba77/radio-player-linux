"""Пути к пользовательским файлам по XDG Base Directory."""

from __future__ import annotations

import os
from pathlib import Path

from . import APP_ID


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
    raw = os.environ.get("XDG_CONFIG_HOME")
    root = Path(raw) if raw else Path.home() / ".config"
    return root / "autostart" / f"{APP_ID}.desktop"


def ensure_dirs() -> None:
    """Создаёт директории, в которые приложение пишет. Идемпотентно."""
    for path in (config_dir(), cache_dir() / "catalogs", cache_dir() / "artwork"):
        path.mkdir(parents=True, exist_ok=True)
