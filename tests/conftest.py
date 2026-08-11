"""Общие фикстуры.

Тесты не должны трогать настоящие `~/.config` и `~/.cache`, поэтому XDG-переменные
подменяются на временный каталог до импорта тестируемого кода.
"""

from __future__ import annotations

import pytest


@pytest.fixture(autouse=True)
def isolated_xdg(tmp_path, monkeypatch):
    monkeypatch.setenv("XDG_CONFIG_HOME", str(tmp_path / "config"))
    monkeypatch.setenv("XDG_CACHE_HOME", str(tmp_path / "cache"))
    return tmp_path


@pytest.fixture(scope="session")
def qt_app():
    """Экземпляр приложения для объектов Qt: таймеры и сигналы без него не живут.

    Модулю, которому он нужен целиком, достаточно строки
    `pytestmark = pytest.mark.usefixtures("qt_app")`.
    """
    from PySide6.QtCore import QCoreApplication

    return QCoreApplication.instance() or QCoreApplication([])
