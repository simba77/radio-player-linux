"""Автозапуск при входе в систему — аналог `SMAppService` из macOS-версии.

В Linux это `.desktop`-файл в `~/.config/autostart`. Команду запуска приходится
определять по обстановке: установленный пакет запускается своим скриптом,
а исходники — через `python -m` с явным `PYTHONPATH`.
"""

from __future__ import annotations

import shutil
import site
import sys
import sysconfig
from pathlib import Path

from . import APP_ID, APP_NAME, paths

# Локализованные поля `.desktop` — часть формата, а не строки интерфейса:
# среда сама выбирает нужный вариант по своей локали.
_TEMPLATE = """[Desktop Entry]
Type=Application
Name={name}
Comment=Internet radio player
Comment[ru]=Плеер интернет-радио
Exec={exec_line}
Icon=audio-radio
Terminal=false
Categories=AudioVideo;Audio;Player;
X-GNOME-Autostart-enabled=true
"""


def is_enabled() -> bool:
    return paths.autostart_file().exists()


def set_enabled(enabled: bool) -> bool:
    """Включает или выключает автозапуск. Возвращает получившееся состояние."""
    path = paths.autostart_file()
    if not enabled:
        try:
            path.unlink(missing_ok=True)
        except OSError:
            return is_enabled()
        return False

    try:
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(
            _TEMPLATE.format(name=APP_NAME, exec_line=_exec_line()),
            encoding="utf-8",
        )
    except OSError:
        return False
    return True


def _exec_line() -> str:
    """Команда запуска приложения в том виде, в каком оно сейчас работает."""
    installed = shutil.which(APP_ID)
    if installed:
        return _quote(installed)

    package_root = Path(__file__).resolve().parent.parent
    command = f"{_quote(sys.executable)} -m radio_player"
    if _is_installed(package_root):
        return command
    # Запуск из исходников: сессия стартует автозапуск из другого рабочего каталога,
    # поэтому путь к пакету нужно передать явно.
    return f"env {_quote(f'PYTHONPATH={package_root}')} {command}"


def _is_installed(package_root: Path) -> bool:
    """Лежит ли пакет там, куда ставят пакеты Python (site-packages)."""
    candidates = [sysconfig.get_paths().get(name) for name in ("purelib", "platlib")]
    candidates.extend(site.getsitepackages() if hasattr(site, "getsitepackages") else [])
    return any(path and package_root == Path(path).resolve() for path in candidates)


def _quote(value: str) -> str:
    """Аргумент `.desktop`-файла: пробелы допустимы только внутри кавычек."""
    return f'"{value}"' if " " in value else value
