"""Границы слоёв.

Разложить модули по каталогам мало: без проверки первый же удобный импорт
свяжет домен с виджетами обратно. Здесь описано, кто кого имеет право знать,
и проверяется это по импортам в исходниках.

Направление зависимостей: ui → playback/desktop → core. Обратных стрелок нет.
"""

from __future__ import annotations

import ast
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parent.parent / "radio_player"

#: Слой → что ему запрещено импортировать.
FORBIDDEN = {
    # Домен описывает данные и правила над ними. Из Qt ему нужны только сигналы
    # и загрузка каталогов по сети; виджеты, D-Bus и звук — не его дело.
    "core": (
        "PySide6.QtWidgets",
        "PySide6.QtDBus",
        "PySide6.QtGui",
        "mpv",
        "radio_player.ui",
        "radio_player.desktop",
        "radio_player.playback",
        "radio_player.i18n",
    ),
    # Воспроизведение не знает ни про окна, ни про D-Bus: MPRIS и запрет
    # засыпания подписываются на него сами.
    "playback": ("PySide6.QtWidgets", "radio_player.ui", "radio_player.desktop"),
    # Интеграция с окружением обходится без интерфейса — иначе её нельзя было бы
    # отключить на системе без трея или без шины.
    "desktop": ("radio_player.ui",),
}


def imports_of(path: Path) -> list[str]:
    """Полные имена того, что импортирует модуль."""
    tree = ast.parse(path.read_text(encoding="utf-8"))
    package = ".".join(path.relative_to(ROOT.parent).with_suffix("").parts[:-1])

    found: list[str] = []
    for node in ast.walk(tree):
        if isinstance(node, ast.Import):
            found.extend(alias.name for alias in node.names)
        elif isinstance(node, ast.ImportFrom):
            module = node.module or ""
            if node.level:
                # Относительный импорт: поднимаемся на нужное число уровней.
                parts = package.split(".")
                base = ".".join(parts[: len(parts) - node.level + 1])
                module = f"{base}.{module}" if module else base
            found.extend(f"{module}.{alias.name}" for alias in node.names)
            found.append(module)
    return found


def layer_modules(layer: str) -> list[Path]:
    return sorted(p for p in (ROOT / layer).rglob("*.py"))


@pytest.mark.parametrize("layer", sorted(FORBIDDEN))
def test_layer_has_files(layer):
    assert layer_modules(layer), f"слой {layer} пуст — тест ничего не проверяет"


@pytest.mark.parametrize("layer", sorted(FORBIDDEN))
def test_layer_keeps_its_boundaries(layer):
    violations = []
    for path in layer_modules(layer):
        for imported in imports_of(path):
            for banned in FORBIDDEN[layer]:
                if imported == banned or imported.startswith(f"{banned}."):
                    violations.append(f"{path.name} → {imported}")
    assert not violations, f"слой {layer} тянет лишнее: {violations}"


def test_ui_is_the_only_place_that_knows_about_widgets():
    """Обратная проверка: слой интерфейса действительно существует и работает
    с виджетами — иначе предыдущие тесты проходили бы на пустом месте."""
    widget_users = {
        path.name
        for path in (ROOT / "ui").rglob("*.py")
        if any(i.startswith("PySide6.QtWidgets") for i in imports_of(path))
    }
    assert "main_window.py" in widget_users
