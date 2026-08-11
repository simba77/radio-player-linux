"""Окно настроек: основные параметры, станции и каталоги."""

from __future__ import annotations

from contextlib import contextmanager
from pathlib import Path

from PySide6.QtCore import QCoreApplication, Qt, QTimer
from PySide6.QtWidgets import (
    QAbstractItemView,
    QCheckBox,
    QComboBox,
    QDialog,
    QFileDialog,
    QFormLayout,
    QHBoxLayout,
    QInputDialog,
    QLabel,
    QLineEdit,
    QListWidget,
    QListWidgetItem,
    QMessageBox,
    QPushButton,
    QSpinBox,
    QTabWidget,
    QVBoxLayout,
    QWidget,
)

from .. import autostart, catalog, i18n, playlist_io
from .. import settings as settings_module
from ..settings import MAX_UPDATE_INTERVAL_HOURS, MIN_UPDATE_INTERVAL_HOURS, Settings
from ..station_store import StationStore

#: Роль Qt для хранения id станции в строке списка.
_STATION_ID_ROLE = int(Qt.ItemDataRole.UserRole)

#: Названия языков не переводятся: каждый пишется на себе самом.
_LANGUAGE_NAMES = {"en": "English", "ru": "Русский"}


class SettingsDialog(QDialog):
    def __init__(
        self,
        store: StationStore,
        settings: Settings,
        updater: catalog.CatalogUpdater,
        parent: QWidget | None = None,
    ) -> None:
        super().__init__(parent)
        self._store = store
        self._settings = settings
        self._updater = updater

        self.setWindowTitle(self.tr("Settings"))
        self.resize(460, 460)

        tabs = QTabWidget()
        tabs.addTab(GeneralTab(settings), self.tr("General"))
        tabs.addTab(StationsTab(store), self.tr("Stations"))
        tabs.addTab(CatalogsTab(settings, updater), self.tr("Catalogs"))

        layout = QVBoxLayout(self)
        layout.addWidget(tabs)


class GeneralTab(QWidget):
    def __init__(self, settings: Settings, parent: QWidget | None = None) -> None:
        super().__init__(parent)
        self._settings = settings

        self._autostart = QCheckBox(self.tr("Start at login"))
        self._autostart.setChecked(autostart.is_enabled())
        self._autostart.toggled.connect(self._on_autostart_toggled)

        self._close_to_tray = QCheckBox(self.tr("Closing the window hides it in the tray"))
        self._close_to_tray.setChecked(settings.close_to_tray)
        self._close_to_tray.toggled.connect(self._on_close_to_tray_toggled)

        self._interval = QSpinBox()
        self._interval.setRange(MIN_UPDATE_INTERVAL_HOURS, MAX_UPDATE_INTERVAL_HOURS)
        self._interval.setValue(settings.update_interval_hours)
        self._interval.setSuffix(self.tr(" h"))
        self._interval.valueChanged.connect(self._on_interval_changed)

        self._language = QComboBox()
        self._language.addItem(self.tr("System"), i18n.AUTO)
        for code in i18n.LANGUAGES:
            self._language.addItem(_LANGUAGE_NAMES[code], code)
        self._language.setCurrentIndex(max(0, self._language.findData(settings.language)))
        self._language.currentIndexChanged.connect(self._on_language_changed)

        self._language_hint = QLabel(self.tr("The language will change after a restart."))
        self._language_hint.setWordWrap(True)
        self._language_hint.hide()

        form = QFormLayout()
        form.addRow(self._autostart)
        form.addRow(self._close_to_tray)
        form.addRow(self.tr("Update catalogs every"), self._interval)
        form.addRow(self.tr("Interface language"), self._language)
        form.addRow("", self._language_hint)

        layout = QVBoxLayout(self)
        layout.addLayout(form)
        layout.addStretch()

    def _on_autostart_toggled(self, enabled: bool) -> None:
        actual = autostart.set_enabled(enabled)
        if actual == enabled:
            return
        # Не удалось записать `.desktop` — возвращаем флажок к правде.
        self._autostart.blockSignals(True)
        self._autostart.setChecked(actual)
        self._autostart.blockSignals(False)
        QMessageBox.warning(
            self, self.tr("Start at login"), self.tr("Could not change the startup setting.")
        )

    def _on_close_to_tray_toggled(self, enabled: bool) -> None:
        self._settings.close_to_tray = enabled
        settings_module.save(self._settings)

    def _on_interval_changed(self, hours: int) -> None:
        self._settings.update_interval_hours = hours
        settings_module.save(self._settings)

    def _on_language_changed(self) -> None:
        self._settings.language = self._language.currentData()
        settings_module.save(self._settings)
        # Перерисовать уже собранный интерфейс нечем — предупреждаем честно.
        self._language_hint.show()


class StationsTab(QWidget):
    """Управление списком: видимость, названия, порядок, свои станции."""

    def __init__(self, store: StationStore, parent: QWidget | None = None) -> None:
        super().__init__(parent)
        self._store = store
        self._applying = False

        self._list = QListWidget()
        self._list.setSelectionMode(QAbstractItemView.SelectionMode.SingleSelection)
        self._list.setDragDropMode(QAbstractItemView.DragDropMode.InternalMove)
        self._list.model().rowsMoved.connect(self._on_rows_moved)
        self._list.itemChanged.connect(self._on_item_changed)
        self._list.itemSelectionChanged.connect(self._update_buttons)

        self._add_button = QPushButton(self.tr("Add..."))
        self._add_button.clicked.connect(self._add_station)
        self._duplicate_button = QPushButton(self.tr("Duplicate"))
        self._duplicate_button.setToolTip(
            self.tr("Creates your own copy of a catalog station — its stream URL can be edited")
        )
        self._duplicate_button.clicked.connect(self._duplicate_station)
        self._remove_button = QPushButton(self.tr("Remove"))
        self._remove_button.clicked.connect(self._remove_station)
        self._import_button = QPushButton(self.tr("Import..."))
        self._import_button.clicked.connect(self._import_stations)
        self._export_button = QPushButton(self.tr("Export..."))
        self._export_button.clicked.connect(self._export_stations)
        self._reset_button = QPushButton(self.tr("Reset"))
        self._reset_button.setToolTip(self.tr("Return the list to the catalog state"))
        self._reset_button.clicked.connect(self._reset)

        buttons = QHBoxLayout()
        buttons.addWidget(self._add_button)
        buttons.addWidget(self._duplicate_button)
        buttons.addWidget(self._remove_button)
        buttons.addStretch()
        buttons.addWidget(self._import_button)
        buttons.addWidget(self._export_button)

        hint = QLabel(
            self.tr(
                "Uncheck a station to hide it. Rename with a double click, "
                "reorder by dragging."
            )
        )
        hint.setWordWrap(True)

        layout = QVBoxLayout(self)
        layout.addWidget(hint)
        layout.addWidget(self._list, 1)
        layout.addLayout(buttons)
        layout.addWidget(self._reset_button, 0, Qt.AlignmentFlag.AlignRight)

        store.stations_changed.connect(self._reload)
        self._reload()

    # MARK: - Наполнение

    def _reload(self) -> None:
        # Пока применяется правка, перестраивать список нельзя: он уничтожит
        # тот самый элемент, из обработчика которого мы пришли.
        if self._applying:
            return
        self._list.blockSignals(True)
        self._list.clear()
        for station in self._store.stations:
            self._add_row(station.id, station.name, station.url, visible=True)
        for station in self._store.hidden_stations:
            self._add_row(station.id, station.name, station.url, visible=False)
        self._list.blockSignals(False)
        self._update_buttons()

    def _add_row(self, station_id: str, name: str, url: str, *, visible: bool) -> None:
        item = QListWidgetItem(name)
        item.setData(_STATION_ID_ROLE, station_id)
        item.setToolTip(url)
        item.setFlags(item.flags() | Qt.ItemFlag.ItemIsUserCheckable | Qt.ItemFlag.ItemIsEditable)
        item.setCheckState(Qt.CheckState.Checked if visible else Qt.CheckState.Unchecked)
        self._list.addItem(item)

    def _selected_id(self) -> str | None:
        item = self._list.currentItem()
        return item.data(_STATION_ID_ROLE) if item else None

    def _update_buttons(self) -> None:
        station_id = self._selected_id()
        station = self._store.find(station_id) if station_id else None
        self._duplicate_button.setEnabled(station is not None and not station.is_custom)
        self._remove_button.setEnabled(station is not None and station.is_custom)

    # MARK: - Действия

    def _on_item_changed(self, item: QListWidgetItem) -> None:
        station_id = item.data(_STATION_ID_ROLE)
        if station_id is None or self._applying:
            return
        # Считываем всё до изменений: после первой правки элемент будет уничтожен.
        name = item.text()
        visible = item.checkState() == Qt.CheckState.Checked

        with self._batch():
            self._store.rename(station_id, name)
            if visible:
                self._store.unhide(station_id)
            else:
                self._store.hide(station_id)

    def _on_rows_moved(self, *_args) -> None:
        """Порядок задаётся видимыми строками — их и отправляем в хранилище."""
        if self._applying:
            return
        order = [
            self._list.item(row).data(_STATION_ID_ROLE)
            for row in range(self._list.count())
            if self._list.item(row).checkState() == Qt.CheckState.Checked
        ]
        with self._batch():
            self._store.set_order(order)

    @contextmanager
    def _batch(self):
        """Применяет правки, отложив перестроение списка до выхода из обработчика."""
        self._applying = True
        try:
            yield
        finally:
            self._applying = False
        QTimer.singleShot(0, self._reload)

    def _add_station(self) -> None:
        title = self.tr("New station")
        name, ok = QInputDialog.getText(self, title, self.tr("Name:"))
        if not ok or not name.strip():
            return
        url, ok = QInputDialog.getText(
            self, title, self.tr("Stream URL:"), QLineEdit.EchoMode.Normal, "https://"
        )
        if not ok:
            return
        if self._store.add_custom(name, url) is None:
            QMessageBox.warning(self, title, self.tr("A name and a stream URL are required."))

    def _duplicate_station(self) -> None:
        station_id = self._selected_id()
        if station_id:
            self._store.duplicate(station_id)

    def _remove_station(self) -> None:
        station_id = self._selected_id()
        station = self._store.find(station_id) if station_id else None
        if station is None:
            return
        confirm = QMessageBox.question(
            self,
            self.tr("Remove"),
            self.tr("Remove the station «{name}»?").format(name=station.name),
        )
        if confirm == QMessageBox.StandardButton.Yes:
            self._store.remove_custom(station.id)

    def _import_stations(self) -> None:
        path, _ = QFileDialog.getOpenFileName(
            self,
            self.tr("Import stations"),
            "",
            self.tr("Playlists (*.m3u *.m3u8 *.pls *.json);;All files (*)"),
        )
        if not path:
            return
        stations = playlist_io.read_file(Path(path))
        if not stations:
            QMessageBox.warning(
                self, self.tr("Import"), self.tr("No stations were found in the file.")
            )
            return
        for station in stations:
            self._store.add_custom(station.name, station.url)
        QMessageBox.information(
            self,
            self.tr("Import"),
            self.tr("%n station(s) added.", "", len(stations)),
        )

    def _export_stations(self) -> None:
        path, _ = QFileDialog.getSaveFileName(
            self, self.tr("Export stations"), "stations.m3u", self.tr("Playlist (*.m3u)")
        )
        if not path:
            return
        try:
            Path(path).write_text(playlist_io.export_m3u(self._store.stations), encoding="utf-8")
        except OSError as exc:
            QMessageBox.warning(
                self,
                self.tr("Export"),
                self.tr("Could not save the file: {error}").format(error=exc),
            )

    def _reset(self) -> None:
        confirm = QMessageBox.question(
            self,
            self.tr("Reset"),
            self.tr("Return the station list to the catalog state? Your edits will be lost."),
        )
        if confirm == QMessageBox.StandardButton.Yes:
            self._store.reset()


class CatalogsTab(QWidget):
    """Источники станций и их обновление."""

    def __init__(
        self,
        settings: Settings,
        updater: catalog.CatalogUpdater,
        parent: QWidget | None = None,
    ) -> None:
        super().__init__(parent)
        self._settings = settings
        self._updater = updater

        self._list = QListWidget()
        # Ссылки длинные: сокращаем их посередине вместо горизонтальной прокрутки,
        # полный адрес остаётся в подсказке.
        self._list.setTextElideMode(Qt.TextElideMode.ElideMiddle)
        self._list.setHorizontalScrollBarPolicy(Qt.ScrollBarPolicy.ScrollBarAlwaysOff)
        self._status = QLabel()
        self._status.setWordWrap(True)

        self._add_button = QPushButton(self.tr("Add catalog..."))
        self._add_button.clicked.connect(self._add_catalog)
        self._remove_button = QPushButton(self.tr("Remove"))
        self._remove_button.clicked.connect(self._remove_catalog)
        self._update_button = QPushButton(self.tr("Update now"))
        self._update_button.clicked.connect(self._update_now)

        buttons = QHBoxLayout()
        buttons.addWidget(self._add_button)
        buttons.addWidget(self._remove_button)
        buttons.addStretch()
        buttons.addWidget(self._update_button)

        layout = QVBoxLayout(self)
        layout.addWidget(self._list, 1)
        layout.addLayout(buttons)
        layout.addWidget(self._status)

        updater.finished.connect(self._on_update_finished)
        self._list.itemSelectionChanged.connect(self._update_buttons)
        self._reload()

    def _reload(self) -> None:
        self._list.clear()
        for url in catalog.DEFAULT_CATALOG_URLS:
            item = QListWidgetItem(url)
            item.setToolTip(f"{url}\n\n" + self.tr("Built-in catalog — cannot be removed"))
            item.setFlags(Qt.ItemFlag.ItemIsEnabled)
            self._list.addItem(item)
        for url in self._settings.extra_catalog_urls:
            item = QListWidgetItem(url)
            item.setToolTip(url)
            self._list.addItem(item)
        self._update_status()
        self._update_buttons()

    def _update_buttons(self) -> None:
        item = self._list.currentItem()
        is_extra = item is not None and item.text() in self._settings.extra_catalog_urls
        self._remove_button.setEnabled(is_extra)
        self._update_button.setEnabled(not self._updater.is_running)

    def _update_status(self) -> None:
        if self._updater.is_running:
            self._status.setText(self.tr("Updating..."))
            return
        elapsed = catalog.seconds_since_update()
        if elapsed is None:
            self._status.setText(self.tr("The catalogs have never been updated."))
        else:
            self._status.setText(
                self.tr("Last update: {ago} ago.").format(ago=_humanize(elapsed))
            )

    def _add_catalog(self) -> None:
        title = self.tr("New catalog")
        url, ok = QInputDialog.getText(
            self, title, self.tr("Link to a JSON catalog:"), QLineEdit.EchoMode.Normal, "https://"
        )
        url = url.strip()
        if not ok or not url:
            return
        if not url.startswith(("http://", "https://")):
            QMessageBox.warning(
                self, title, self.tr("The link must start with http:// or https://")
            )
            return
        if url in self._settings.extra_catalog_urls or url in catalog.DEFAULT_CATALOG_URLS:
            return
        self._settings.extra_catalog_urls.append(url)
        settings_module.save(self._settings)
        self._reload()

    def _remove_catalog(self) -> None:
        item = self._list.currentItem()
        if item is None or item.text() not in self._settings.extra_catalog_urls:
            return
        self._settings.extra_catalog_urls.remove(item.text())
        settings_module.save(self._settings)
        self._reload()

    def _update_now(self) -> None:
        self._updater.update_now()
        self._update_status()
        self._update_buttons()

    def _on_update_finished(self, _catalogs: list, changed: bool) -> None:
        self._update_status()
        self._update_buttons()
        if not changed:
            self._status.setText(self._status.text() + " " + self.tr("No changes."))


def _humanize(seconds: float) -> str:
    """Прошедшее время словами. Формы числа расставляет Qt по правилам языка.

    Литералы стоят прямо в вызовах `translate()`: строки собираются статически,
    и обёртку вокруг вызова lupdate уже не разберёт.
    """
    minutes = int(seconds // 60)
    if minutes < 1:
        return QCoreApplication.translate("CatalogsTab", "less than a minute")
    if minutes < 60:
        return QCoreApplication.translate("CatalogsTab", "%n minute(s)", None, minutes)
    hours = minutes // 60
    if hours < 24:
        return QCoreApplication.translate("CatalogsTab", "%n hour(s)", None, hours)
    return QCoreApplication.translate("CatalogsTab", "%n day(s)", None, hours // 24)
