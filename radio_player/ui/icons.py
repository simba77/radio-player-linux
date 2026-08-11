"""Иконки.

Берём из системной темы — так они автоматически совпадают со светлым и тёмным
оформлением. Там, где темы нет (минимальные окружения, не-KDE системы),
подставляем встроенные в Qt стандартные пиктограммы.
"""

from __future__ import annotations

from PySide6.QtGui import QIcon
from PySide6.QtWidgets import QApplication, QStyle

PLAY = "media-playback-start"
STOP = "media-playback-stop"
PAUSE = "media-playback-pause"
NEXT = "media-skip-forward"
PREVIOUS = "media-skip-backward"
RADIO = "audio-radio"
SETTINGS = "configure"

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
    standard = _FALLBACKS.get(name)
    app = QApplication.instance()
    # Стандартные пиктограммы живёт в стиле QApplication: в консольном
    # приложении (QCoreApplication) его нет, и подставить нам нечего.
    if standard is None or not isinstance(app, QApplication):
        return QIcon()
    return app.style().standardIcon(standard)
