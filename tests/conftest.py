"""Общие фикстуры.

Тесты не должны трогать настоящие `~/.config` и `~/.cache`, поэтому XDG-переменные
подменяются на временный каталог до импорта тестируемого кода.
"""

from __future__ import annotations

import os

import pytest


@pytest.fixture(autouse=True)
def isolated_xdg(tmp_path, monkeypatch):
    monkeypatch.setenv("XDG_CONFIG_HOME", str(tmp_path / "config"))
    monkeypatch.setenv("XDG_CACHE_HOME", str(tmp_path / "cache"))
    return tmp_path


@pytest.fixture(scope="session")
def qt_app():
    """Экземпляр приложения для объектов Qt: таймеры и сигналы без него не живут.

    Создаётся `QApplication`, а не `QCoreApplication`: экземпляр в процессе
    может быть только один, а тестам интерфейса нужны виджеты. Платформа —
    offscreen, поэтому дисплей не требуется.

    Модулю, которому он нужен целиком, достаточно строки
    `pytestmark = pytest.mark.usefixtures("qt_app")`.
    """
    os.environ.setdefault("QT_QPA_PLATFORM", "offscreen")

    from PySide6.QtWidgets import QApplication

    return QApplication.instance() or QApplication([])
