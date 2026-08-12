"""Запуск приложения целиком.

Модульные тесты собирают части по отдельности, а `main()` связывает их вместе —
и ошибка в этой связке видна только запуском. Здесь приложение стартует
настоящим процессом под offscreen-платформой, со своими XDG-каталогами, и
завершается по SIGTERM: сеанс при выходе присылает именно его.

libmpv нужен по-настоящему — без него тест пропускается.
"""

from __future__ import annotations

import os
import signal
import subprocess
import sys
import time
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parent.parent

pytestmark = pytest.mark.skipif(
    subprocess.run(
        [sys.executable, "-c", "import mpv"], capture_output=True, check=False
    ).returncode
    != 0,
    reason="python-mpv недоступен — запускать приложение нечем",
)


def launch(tmp_path: Path) -> subprocess.Popen[str]:
    environment = {
        **os.environ,
        "XDG_CONFIG_HOME": str(tmp_path / "config"),
        "XDG_CACHE_HOME": str(tmp_path / "cache"),
        # Свой runtime-каталог: иначе тест постучится в сокет живого приложения
        # пользователя и попросит его показать окно.
        "XDG_RUNTIME_DIR": str(tmp_path / "run"),
        "QT_QPA_PLATFORM": "offscreen",
        "PYTHONPATH": str(ROOT),
    }
    (tmp_path / "run").mkdir(parents=True, exist_ok=True)
    return subprocess.Popen(
        [sys.executable, "-m", "radio_player", "--log-level", "debug"],
        env=environment,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
    )


def wait_until_running(process: subprocess.Popen[str], tmp_path: Path, seconds: float = 15.0) -> None:
    """Ждёт, пока приложение не заработает (или не упадёт).

    Признак жизни — каталоги, которые создаёт `paths.ensure_dirs()` сразу после
    старта: дожидаться их надёжнее, чем спать наугад.
    """
    marker = tmp_path / "cache" / "radio-player" / "catalogs"
    deadline = time.monotonic() + seconds
    while time.monotonic() < deadline:
        if process.poll() is not None:
            output = process.stdout.read() if process.stdout else ""
            pytest.fail(f"приложение завершилось на старте ({process.returncode}):\n{output}")
        if marker.is_dir():
            return
        time.sleep(0.1)
    pytest.fail("приложение не подало признаков жизни")


def test_application_starts_and_stops_on_sigterm(tmp_path):
    process = launch(tmp_path)
    try:
        wait_until_running(process, tmp_path)
        # Даём циклу событий поработать: часть инициализации идёт по таймерам.
        time.sleep(1)
        assert process.poll() is None, "приложение не удержалось и секунды"

        # SIGTERM приходит при завершении сеанса: он обязан приводить к штатному
        # выходу, иначе имя на шине и mpv остаются на совести ядра.
        process.send_signal(signal.SIGTERM)
        assert process.wait(timeout=10) == 0
    finally:
        if process.poll() is None:
            process.kill()
            process.wait(timeout=5)


def test_configuration_directories_are_created(tmp_path):
    process = launch(tmp_path)
    try:
        wait_until_running(process, tmp_path)
        assert (tmp_path / "config" / "radio-player").is_dir()
        assert (tmp_path / "cache" / "radio-player" / "catalogs").is_dir()
    finally:
        process.send_signal(signal.SIGTERM)
        process.wait(timeout=10)


def test_second_launch_exits_without_a_second_window(tmp_path):
    first = launch(tmp_path)
    try:
        wait_until_running(first, tmp_path)
        time.sleep(1)

        second = launch(tmp_path)
        assert second.wait(timeout=15) == 0
        assert first.poll() is None, "первый экземпляр не должен был завершиться"
    finally:
        first.send_signal(signal.SIGTERM)
        first.wait(timeout=10)
