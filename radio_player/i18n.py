"""Язык интерфейса: выбор и установка переводчиков.

Исходные строки в коде английские, переводы лежат в `resources/i18n/*.qm`.
Язык берётся из настроек, `auto` означает системную локаль.

Смена языка применяется при следующем запуске: чтобы переключать его на лету,
каждому окну и виджету понадобился бы `retranslate()` и обработка
`LanguageChange`, а забытая подпись обнаружилась бы только глазами.
"""

from __future__ import annotations

import os
from pathlib import Path

from PySide6.QtCore import QCoreApplication, QLibraryInfo, QLocale, QTranslator

#: Языки интерфейса. `en` — исходные строки, перевод для него не нужен.
LANGUAGES = ("en", "ru")
#: Значение настройки «как в системе».
AUTO = "auto"

_TRANSLATIONS_DIR = Path(__file__).parent / "resources" / "i18n"


def resolve(language: str) -> str:
    """Язык, который будет использован на самом деле."""
    if language in LANGUAGES:
        return language
    system = QLocale.system().name().split("_")[0]
    return system if system in LANGUAGES else "en"


def prepare(language: str) -> str:
    """Выбирает язык и готовит окружение. Вызывается **до** `QApplication`.

    Qt ставит собственный переводчик по системной локали ещё при создании
    приложения, и снять его потом нечем: при выборе English в русской системе
    кнопки диалогов так и остались бы русскими. Поэтому язык сообщается Qt
    через `LANGUAGE` заранее — переменная влияет только на наш процесс.
    """
    resolved = resolve(language)
    os.environ["LANGUAGE"] = resolved
    return resolved


def install(app: QCoreApplication, language: str) -> str:
    """Ставит переводчики приложения и Qt. Возвращает выбранный язык.

    Отсутствие файла перевода не ошибка: интерфейс останется на исходном
    английском, а приложение продолжит работать.
    """
    resolved = resolve(language)
    # Отсюда берётся и формат чисел с датами, а не только язык сообщений.
    QLocale.setDefault(QLocale(resolved))
    if resolved == "en":
        return resolved

    # Свои стандартные кнопки и файловый диалог Qt переводит сам — без этого
    # «Открыть» и «Отмена» остались бы английскими посреди русского интерфейса.
    _load(app, f"qtbase_{resolved}", QLibraryInfo.path(QLibraryInfo.LibraryPath.TranslationsPath))
    _load(app, f"radio_player_{resolved}", str(_TRANSLATIONS_DIR))
    return resolved


def _load(app: QCoreApplication, name: str, directory: str) -> bool:
    # Переводчик должен пережить вызов: без родителя его соберёт сборщик мусора,
    # и перевод молча исчезнет.
    translator = QTranslator(app)
    if not translator.load(name, directory):
        return False
    return app.installTranslator(translator)
