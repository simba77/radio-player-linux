"""Виджет «Сейчас играет»: обложка, название трека, управление, громкость."""

from __future__ import annotations

from collections.abc import Callable

from PySide6.QtCore import QCoreApplication, Qt, QTimer, Signal
from PySide6.QtGui import (
    QGuiApplication,
    QImage,
    QMouseEvent,
    QPainter,
    QPainterPath,
    QPalette,
    QPixmap,
)
from PySide6.QtWidgets import (
    QGridLayout,
    QHBoxLayout,
    QLabel,
    QPushButton,
    QSizePolicy,
    QSlider,
    QVBoxLayout,
    QWidget,
)

from ..playback.player import PlayerManager
from . import icons

ARTWORK_SIZE = 72
#: Сколько показывать подтверждение после копирования названия трека.
_COPIED_HINT_MS = 1500


class ClickableLabel(QLabel):
    """QLabel, реагирующий на клик. Нужен для копирования названия трека."""

    clicked = Signal()

    def mousePressEvent(self, event: QMouseEvent) -> None:  # имя переопределяемого метода Qt
        if event.button() == Qt.MouseButton.LeftButton:
            self.clicked.emit()
        super().mousePressEvent(event)


class NowPlayingWidget(QWidget):
    def __init__(self, player: PlayerManager, parent: QWidget | None = None) -> None:
        super().__init__(parent)
        self._player = player

        self._artwork = QLabel()
        self._artwork.setFixedSize(ARTWORK_SIZE, ARTWORK_SIZE)
        self._artwork.setAlignment(Qt.AlignmentFlag.AlignCenter)

        self._title = ClickableLabel()
        self._title.setWordWrap(True)
        self._title.setTextFormat(Qt.TextFormat.PlainText)
        self._title.setStyleSheet("font-weight: 600;")
        self._title.setCursor(Qt.CursorShape.PointingHandCursor)
        self._title.setToolTip(self.tr("Click to copy the track name"))
        self._title.clicked.connect(self._copy_track)

        self._subtitle = QLabel()
        self._subtitle.setTextFormat(Qt.TextFormat.PlainText)
        # Приглушаем прозрачностью, а не фиксированным цветом: так подпись
        # остаётся читаемой и в светлой, и в тёмной теме.
        subtitle_palette = self._subtitle.palette()
        muted = subtitle_palette.color(QPalette.ColorRole.WindowText)
        muted.setAlphaF(0.65)
        subtitle_palette.setColor(QPalette.ColorRole.WindowText, muted)
        self._subtitle.setPalette(subtitle_palette)

        self._error = QLabel()
        self._error.setWordWrap(True)
        # В палитре Qt нет роли для ошибки, а красный должен читаться в обеих темах.
        self._error.setStyleSheet("color: #e05252;")
        self._error.hide()

        self._previous_button = _tool_button(
            icons.PREVIOUS, self.tr("Previous station"), player.play_previous
        )
        self._play_button = _tool_button(icons.PLAY, self.tr("Play"), player.toggle)
        self._next_button = _tool_button(icons.NEXT, self.tr("Next station"), player.play_next)

        self._volume = QSlider(Qt.Orientation.Horizontal)
        self._volume.setRange(0, 100)
        self._volume.setValue(player.volume)
        self._volume.setFixedWidth(90)
        self._volume.setToolTip(self.tr("Volume"))
        self._volume.valueChanged.connect(player.set_volume)
        # Громкость меняют и снаружи — медиаклавишами и апплетом через MPRIS.
        player.volume_changed.connect(self._on_volume_changed)

        controls = QHBoxLayout()
        controls.setContentsMargins(0, 0, 0, 0)
        controls.addWidget(self._previous_button)
        controls.addWidget(self._play_button)
        controls.addWidget(self._next_button)
        controls.addStretch()
        controls.addWidget(self._volume)

        text_column = QVBoxLayout()
        text_column.setContentsMargins(0, 0, 0, 0)
        text_column.setSpacing(2)
        text_column.addWidget(self._title)
        text_column.addWidget(self._subtitle)
        text_column.addWidget(self._error)
        text_column.addStretch()
        text_column.addLayout(controls)

        layout = QGridLayout(self)
        layout.setContentsMargins(12, 12, 12, 12)
        layout.addWidget(self._artwork, 0, 0, Qt.AlignmentFlag.AlignTop)
        layout.addLayout(text_column, 0, 1)
        layout.setColumnStretch(1, 1)
        layout.setHorizontalSpacing(12)

        self._copied_timer = QTimer(self)
        self._copied_timer.setSingleShot(True)
        self._copied_timer.setInterval(_COPIED_HINT_MS)
        self._copied_timer.timeout.connect(self.refresh)

        player.state_changed.connect(self.refresh)
        player.track_changed.connect(lambda _track: self.refresh())
        player.station_changed.connect(lambda _station: self.refresh())
        player.artwork_changed.connect(self._set_artwork)

        self._set_artwork(None)
        self.refresh()

    # MARK: - Обновление

    def refresh(self) -> None:
        player = self._player
        self._title.setText(_title_text(player))
        self._title.setEnabled(player.current_track is not None)

        subtitle = _subtitle_text(player)
        self._subtitle.setText(subtitle)
        self._subtitle.setVisible(bool(subtitle))

        self._error.setText(player.error_message or "")
        self._error.setVisible(bool(player.error_message))

        stop_state = player.is_active
        self._play_button.setIcon(icons.get(icons.STOP if stop_state else icons.PLAY))
        self._play_button.setToolTip(self.tr("Stop") if stop_state else self.tr("Play"))

    def _set_artwork(self, image: QImage | None) -> None:
        if image is None or image.isNull():
            self._artwork.setPixmap(_placeholder_pixmap(self._artwork.devicePixelRatioF()))
            return
        self._artwork.setPixmap(_rounded_pixmap(image, self._artwork.devicePixelRatioF()))

    def _on_volume_changed(self, value: int) -> None:
        if self._volume.value() == value:
            return
        # Без блокировки слайдер отправил бы значение обратно плееру.
        self._volume.blockSignals(True)
        self._volume.setValue(value)
        self._volume.blockSignals(False)

    def _copy_track(self) -> None:
        track = self._player.current_track
        if track is None:
            return
        QGuiApplication.clipboard().setText(track.display)
        self._subtitle.setText(self.tr("Copied"))
        self._subtitle.show()
        self._copied_timer.start()


# MARK: - Отрисовка


def _rounded_pixmap(image: QImage, ratio: float) -> QPixmap:
    side = int(ARTWORK_SIZE * ratio)
    scaled = image.scaled(
        side,
        side,
        Qt.AspectRatioMode.KeepAspectRatioByExpanding,
        Qt.TransformationMode.SmoothTransformation,
    )
    pixmap = QPixmap(side, side)
    pixmap.fill(Qt.GlobalColor.transparent)

    painter = QPainter(pixmap)
    painter.setRenderHint(QPainter.RenderHint.Antialiasing)
    path = QPainterPath()
    path.addRoundedRect(0, 0, side, side, 6 * ratio, 6 * ratio)
    painter.setClipPath(path)
    # Кадрируем по центру: обложки бывают не квадратные.
    painter.drawImage(-(scaled.width() - side) // 2, -(scaled.height() - side) // 2, scaled)
    painter.end()

    pixmap.setDevicePixelRatio(ratio)
    return pixmap


def _placeholder_pixmap(ratio: float) -> QPixmap:
    """Заглушка на случай, когда обложки нет — у части станций её не бывает."""
    side = int(ARTWORK_SIZE * ratio)
    pixmap = QPixmap(side, side)
    pixmap.fill(Qt.GlobalColor.transparent)

    painter = QPainter(pixmap)
    painter.setRenderHint(QPainter.RenderHint.Antialiasing)
    path = QPainterPath()
    path.addRoundedRect(0, 0, side, side, 6 * ratio, 6 * ratio)
    painter.fillPath(path, Qt.GlobalColor.gray)
    painter.end()

    icon = icons.get(icons.RADIO)
    if not icon.isNull():
        overlay = icon.pixmap(int(side * 0.5), int(side * 0.5))
        painter = QPainter(pixmap)
        painter.drawPixmap((side - overlay.width()) // 2, (side - overlay.height()) // 2, overlay)
        painter.end()

    pixmap.setDevicePixelRatio(ratio)
    return pixmap


def _tool_button(icon_name: str, tooltip: str, slot: Callable[[], None]) -> QPushButton:
    button = QPushButton()
    button.setIcon(icons.get(icon_name))
    button.setToolTip(tooltip)
    button.setFlat(True)
    button.setFixedSize(32, 28)
    button.setSizePolicy(QSizePolicy.Policy.Fixed, QSizePolicy.Policy.Fixed)
    button.clicked.connect(slot)
    return button


# MARK: - Тексты


def _title_text(player: PlayerManager) -> str:
    # Вне виджета `self.tr()` недоступен, а контекст нужен тот же. Литералы
    # стоят прямо в вызовах: lupdate собирает строки статически и обёртку
    # вокруг translate() уже не разберёт.
    if player.is_reconnecting:
        return QCoreApplication.translate("NowPlayingWidget", "Reconnecting...")
    if player.is_loading:
        return QCoreApplication.translate("NowPlayingWidget", "Loading...")
    if player.current_track is not None:
        return player.current_track.display
    if player.current_station is not None:
        return player.current_station.name
    return QCoreApplication.translate("NowPlayingWidget", "Not playing")


def _subtitle_text(player: PlayerManager) -> str:
    """Станция показывается второй строкой, только когда первую занял трек."""
    if player.current_station is None:
        return ""
    if player.current_track is not None or player.is_loading or player.is_reconnecting:
        return player.current_station.name
    return ""
