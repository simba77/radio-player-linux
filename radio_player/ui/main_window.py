"""Главное окно: «Сейчас играет» сверху, список станций снизу."""

from __future__ import annotations

from PySide6.QtCore import QSize, Qt
from PySide6.QtGui import QCloseEvent, QFont, QIcon, QKeySequence, QPixmap, QShortcut
from PySide6.QtWidgets import (
    QAbstractItemView,
    QApplication,
    QFrame,
    QHBoxLayout,
    QListWidget,
    QListWidgetItem,
    QPushButton,
    QVBoxLayout,
    QWidget,
)

from .. import APP_NAME
from ..catalog import CatalogUpdater
from ..player import PlayerManager
from ..settings_store import SettingsStore
from ..station_store import StationStore
from . import icons
from .settings_dialog import SettingsDialog
from .widgets import NowPlayingWidget

#: Роль Qt (`Qt.UserRole`) для хранения id станции в элементе списка.
_STATION_ID_ROLE = int(Qt.ItemDataRole.UserRole)
#: Размер иконки состояния в строке списка.
_ROW_ICON_SIZE = 14

_blank_icon_cache: QIcon | None = None


def _blank_icon() -> QIcon:
    global _blank_icon_cache
    if _blank_icon_cache is None:
        pixmap = QPixmap(_ROW_ICON_SIZE, _ROW_ICON_SIZE)
        pixmap.fill(Qt.GlobalColor.transparent)
        _blank_icon_cache = QIcon(pixmap)
    return _blank_icon_cache


class MainWindow(QWidget):
    def __init__(
        self,
        player: PlayerManager,
        store: StationStore,
        settings: SettingsStore,
        updater: CatalogUpdater,
    ) -> None:
        super().__init__()
        self._player = player
        self._store = store
        self._settings = settings
        self._updater = updater
        self._settings_dialog: SettingsDialog | None = None
        self._closable = False

        self.setWindowTitle(APP_NAME)
        self.setWindowIcon(icons.get(icons.RADIO))
        self.resize(340, 460)

        self._now_playing = NowPlayingWidget(player)

        self._list = QListWidget()
        self._list.setFrameShape(QFrame.Shape.NoFrame)
        self._list.setSelectionMode(QAbstractItemView.SelectionMode.SingleSelection)
        self._list.setVerticalScrollMode(QAbstractItemView.ScrollMode.ScrollPerPixel)
        self._list.setIconSize(QSize(_ROW_ICON_SIZE, _ROW_ICON_SIZE))
        self._list.setStyleSheet(
            "QListWidget { padding: 4px 6px; } QListWidget::item { padding: 5px 6px; }"
        )
        self._list.itemClicked.connect(self._on_item_clicked)

        separator = QFrame()
        separator.setFrameShape(QFrame.Shape.HLine)
        separator.setFrameShadow(QFrame.Shadow.Sunken)

        settings_button = QPushButton(self.tr("Settings..."))
        settings_button.setIcon(icons.get(icons.SETTINGS))
        settings_button.setFlat(True)
        settings_button.clicked.connect(self.open_settings)

        footer = QHBoxLayout()
        footer.setContentsMargins(8, 4, 8, 6)
        footer.addWidget(settings_button)
        footer.addStretch()

        layout = QVBoxLayout(self)
        layout.setContentsMargins(0, 0, 0, 0)
        layout.setSpacing(0)
        layout.addWidget(self._now_playing)
        layout.addWidget(separator)
        layout.addWidget(self._list, 1)
        layout.addLayout(footer)

        store.stations_changed.connect(self._reload_stations)
        player.state_changed.connect(self._refresh_rows)
        player.station_changed.connect(lambda _station: self._refresh_rows())

        # Горячие клавиши как в macOS-версии.
        QShortcut(QKeySequence("P"), self, player.toggle)
        QShortcut(QKeySequence("Q"), self, self._quit)

        self._reload_stations()

    # MARK: - Список станций

    def _reload_stations(self) -> None:
        self._list.clear()
        stations = self._store.stations
        if not stations:
            placeholder = QListWidgetItem(self.tr("The station list is empty"))
            placeholder.setFlags(Qt.ItemFlag.NoItemFlags)
            self._list.addItem(placeholder)
            return

        for station in stations:
            item = QListWidgetItem(station.name)
            item.setData(_STATION_ID_ROLE, station.id)
            item.setToolTip(station.url)
            self._list.addItem(item)
        self._refresh_rows()

    def _refresh_rows(self) -> None:
        """Отмечает активную станцию иконкой и полужирным начертанием."""
        current_id = self._player.current_station.id if self._player.current_station else None
        # Загрузка и переподключение для строки списка — то же, что игра:
        # иконка не должна прыгать на каждой заминке потока.
        playing = self._player.is_active

        for row in range(self._list.count()):
            item = self._list.item(row)
            station_id = item.data(_STATION_ID_ROLE)
            if station_id is None:
                continue

            is_current = station_id == current_id
            font = item.font()
            font.setWeight(QFont.Weight.DemiBold if is_current else QFont.Weight.Normal)
            item.setFont(font)

            if is_current and playing:
                item.setIcon(icons.get(icons.PLAY))
            elif is_current:
                item.setIcon(icons.get(icons.STOP))
            else:
                # Пустая иконка того же размера, иначе строки разъезжаются по горизонтали.
                item.setIcon(_blank_icon())

    def _on_item_clicked(self, item: QListWidgetItem) -> None:
        station_id = item.data(_STATION_ID_ROLE)
        if station_id is None:
            return
        station = self._store.find(station_id)
        if station is None:
            return
        current = self._player.current_station
        if self._player.is_active and current is not None and current.id == station.id:
            self._player.stop()
        else:
            self._player.play(station)

    # MARK: - Окно

    def allow_close(self) -> None:
        """Разрешает закрытие окна: без трея прятать его некуда."""
        self._closable = True

    def open_settings(self) -> None:
        """Показывает окно настроек, переиспользуя уже открытое."""
        if self._settings_dialog is None:
            self._settings_dialog = SettingsDialog(self._store, self._settings, self._updater, self)
        self._settings_dialog.show()
        self._settings_dialog.raise_()
        self._settings_dialog.activateWindow()

    def _quit(self) -> None:
        QApplication.quit()

    def closeEvent(self, event: QCloseEvent) -> None:  # noqa: N802 — Qt API
        if self._closable or not self._settings.close_to_tray:
            event.accept()
            QApplication.quit()
            return
        event.ignore()
        self.hide()

    def toggle_visibility(self) -> None:
        if self.isVisible() and not self.isMinimized():
            self.hide()
        else:
            self.show_and_raise()

    def show_and_raise(self) -> None:
        self.showNormal()
        self.raise_()
        self.activateWindow()
