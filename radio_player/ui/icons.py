"""Иконки.

Берём из системной темы — так они автоматически совпадают со светлым и тёмным
оформлением. Там, где темы нет (минимальные окружения, не-KDE системы),
подставляем встроенные в Qt стандартные пиктограммы.
"""

from __future__ import annotations

from pathlib import Path

from PySide6.QtGui import QIcon
from PySide6.QtWidgets import QApplication, QStyle

from .. import APP_RDNS_ID

PLAY = "media-playback-start"
STOP = "media-playback-stop"
PAUSE = "media-playback-pause"
NEXT = "media-skip-forward"
PREVIOUS = "media-skip-backward"
#: Иконка самого приложения. Ставится в hicolor, поэтому находится по теме;
#: при запуске из исходников её там ещё нет — тогда берём файл из пакета.
RADIO = APP_RDNS_ID
SETTINGS = "configure"

_BUNDLED_ICON = (
    Path(__file__).parent.parent / "resources" / "icons" / "hicolor" / "scalable" / "apps"
) / f"{APP_RDNS_ID}.svg"

_FALLBACKS = {
    PLAY: QStyle.StandardPixmap.SP_MediaPlay,
    STOP: QStyle.StandardPixmap.SP_MediaStop,
    PAUSE: QStyle.StandardPixmap.SP_MediaPause,
    NEXT: QStyle.StandardPixmap.SP_MediaSkipForward,
    PREVIOUS: QStyle.StandardPixmap.SP_MediaSkipBackward,
    RADIO: QStyle.StandardPixmap.SP_MediaVolume,
    SETTINGS: QStyle.StandardPixmap.SP_FileDialogDetailedView,
}


def get(name: str) -> QIcon:
    icon = QIcon.fromTheme(name)
    if not icon.isNull():
        return icon
    if name == RADIO and _BUNDLED_ICON.exists():
        bundled = QIcon(str(_BUNDLED_ICON))
        if not bundled.isNull():
            return bundled
    standard = _FALLBACKS.get(name)
    app = QApplication.instance()
    # Стандартные пиктограммы живёт в стиле QApplication: в консольном
    # приложении (QCoreApplication) его нет, и подставить нам нечего.
    if standard is None or not isinstance(app, QApplication):
        return QIcon()
    return app.style().standardIcon(standard)
