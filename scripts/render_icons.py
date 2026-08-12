#!/usr/bin/env python3
"""Растеризация иконки приложения в размеры hicolor.

SVG — источник, PNG коммитятся вместе с ним: не в каждом окружении Qt-приложение
получает отрисовщик SVG, а иконка нужна и там. Запускается вручную после правки
`.svg` (`make icons`), потому что результат идёт в репозиторий.
"""

from __future__ import annotations

import os
import sys
from pathlib import Path

#: Размеры, которые ожидают оболочки: панель, список окон, лаунчер, настройки.
SIZES = (16, 24, 32, 48, 64, 128, 256)

ICONS_DIR = Path(__file__).resolve().parent.parent / "radio_player" / "resources" / "icons" / "hicolor"
NAME = "io.github.simba77.RadioPlayer"


def main() -> int:
    os.environ.setdefault("QT_QPA_PLATFORM", "offscreen")
    from PySide6.QtGui import QGuiApplication, QIcon

    QGuiApplication(sys.argv)

    source = ICONS_DIR / "scalable" / "apps" / f"{NAME}.svg"
    icon = QIcon(str(source))
    if icon.isNull():
        print(f"Не удалось прочитать {source} — нужен плагин qt6-svg", file=sys.stderr)
        return 1

    for size in SIZES:
        target_dir = ICONS_DIR / f"{size}x{size}" / "apps"
        target_dir.mkdir(parents=True, exist_ok=True)
        pixmap = icon.pixmap(size, size)
        target = target_dir / f"{NAME}.png"
        if not pixmap.save(str(target), "PNG"):
            print(f"Не удалось записать {target}", file=sys.stderr)
            return 1
        print(target.relative_to(ICONS_DIR.parent.parent.parent.parent))
    return 0


if __name__ == "__main__":
    sys.exit(main())
