"""Переводы.

Главный тест здесь — про рассинхрон: `lupdate` запускается руками, поэтому
новая строка в коде легко останется без перевода, и обнаружится это только
глазами на чужом языке.
"""

from __future__ import annotations

import ast
import os
import xml.etree.ElementTree as ET
from pathlib import Path

import pytest

pytest.importorskip("PySide6.QtCore")

from PySide6.QtCore import QCoreApplication, QTranslator

from radio_player import i18n

ROOT = Path(__file__).resolve().parent.parent
SOURCES = sorted((ROOT / "radio_player").rglob("*.py"))


@pytest.fixture(scope="module", autouse=True)
def qt_app():
    app = QCoreApplication.instance() or QCoreApplication([])
    yield app


# MARK: - Выбор языка


@pytest.mark.parametrize("language", i18n.LANGUAGES)
def test_explicit_language_wins(language):
    assert i18n.resolve(language) == language


def test_unknown_language_falls_back_to_a_known_one():
    """Мусор в settings.json не должен оставлять интерфейс пустым."""
    assert i18n.resolve("klingon") in i18n.LANGUAGES


def test_prepare_announces_language_to_qt(monkeypatch):
    """Qt выбирает свои переводы по LANGUAGE ещё до создания QApplication."""
    monkeypatch.delenv("LANGUAGE", raising=False)
    i18n.prepare("ru")
    assert os.environ["LANGUAGE"] == "ru"


# MARK: - Файлы перевода


@pytest.mark.parametrize("language", i18n.LANGUAGES)
def test_compiled_translation_is_shipped(language):
    """`.qm` коммитятся: установка не должна требовать инструментов Qt."""
    assert (ROOT / "radio_player" / "resources" / "i18n" / f"radio_player_{language}.qm").exists()


def test_russian_translation_loads_and_translates():
    translator = QTranslator()
    assert translator.load("radio_player_ru", str(ROOT / "radio_player" / "resources" / "i18n"))
    assert translator.translate("TrayIcon", "Quit") == "Выход"


@pytest.mark.parametrize("language", i18n.LANGUAGES)
def test_no_unfinished_translations(language):
    """Незаконченный перевод молча уходит в .qm исходной строкой."""
    tree = ET.parse(ROOT / "translations" / f"radio_player_{language}.ts")
    unfinished = [
        message.find("source").text
        for message in tree.getroot().iter("message")
        if message.find("translation").get("type") == "unfinished"
    ]
    assert unfinished == []


def test_translation_catalog_matches_the_code():
    """Каждая строка из tr() должна быть в .ts — иначе забыли `make i18n-update`."""
    known = {
        (context.find("name").text, message.find("source").text)
        for context in ET.parse(ROOT / "translations" / "radio_player_ru.ts").getroot()
        for message in context.iter("message")
    }
    assert sorted(set(_translatable_strings()) - known) == []


# MARK: - Разбор исходников


def _translatable_strings() -> list[tuple[str, str]]:
    """Пары (контекст, строка) из вызовов `self.tr()` и `translate()`."""
    found: list[tuple[str, str]] = []
    for path in SOURCES:
        found.extend(_Collector().collect(ast.parse(path.read_text(encoding="utf-8"))))
    return found


class _Collector(ast.NodeVisitor):
    def __init__(self) -> None:
        self.classes: list[str] = []
        self.found: list[tuple[str, str]] = []

    def collect(self, tree: ast.AST) -> list[tuple[str, str]]:
        self.visit(tree)
        return self.found

    def visit_ClassDef(self, node: ast.ClassDef) -> None:
        self.classes.append(node.name)
        self.generic_visit(node)
        self.classes.pop()

    def visit_Call(self, node: ast.Call) -> None:
        function = node.func
        if isinstance(function, ast.Attribute) and node.args:
            first = node.args[0]
            if function.attr == "tr" and _is_string(first) and self.classes:
                self.found.append((self.classes[-1], first.value))
            elif (
                function.attr == "translate"
                and len(node.args) >= 2
                and _is_string(first)
                and _is_string(node.args[1])
            ):
                self.found.append((first.value, node.args[1].value))
        self.generic_visit(node)


def _is_string(node: ast.AST) -> bool:
    return isinstance(node, ast.Constant) and isinstance(node.value, str)
