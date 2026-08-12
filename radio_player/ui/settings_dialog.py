"""Окно настроек: основные параметры, станции и каталоги."""

from __future__ import annotations

from collections.abc import Iterator
from contextlib import contextmanager
from pathlib import Path

from PySide6.QtCore import Qt, QTimer
from PySide6.QtGui import QPalette
from PySide6.QtWidgets import (
    QAbstractItemView,
    QCheckBox,
    QComboBox,
    QDialog,
    QDialogButtonBox,
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

from .. import i18n
from ..core import catalog, playlist_io
from ..core.models import Catalog
from ..core.settings import MAX_UPDATE_INTERVAL_HOURS, MIN_UPDATE_INTERVAL_HOURS
from ..core.settings_store import SettingsStore
from ..core.station_store import StationStore
from ..desktop import autostart

#: Роль Qt для хранения id станции в строке списка.
_STATION_ID_ROLE = int(Qt.ItemDataRole.UserRole)

#: Названия языков не переводятся: каждый пишется на себе самом.
_LANGUAGE_NAMES = {"en": "English", "ru": "Русский"}


class SettingsDialog(QDialog):
    def __init__(
        self,
        store: StationStore,
        settings: SettingsStore,
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
    def __init__(self, settings: SettingsStore, parent: QWidget | None = None) -> None:
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
        QMessageBox.warning(self, self.tr("Start at login"), self.tr("Could not change the startup setting."))

    def _on_close_to_tray_toggled(self, enabled: bool) -> None:
        self._settings.set_close_to_tray(enabled)

    def _on_interval_changed(self, hours: int) -> None:
        self._settings.set_update_interval_hours(hours)

    def _on_language_changed(self) -> None:
        self._settings.set_language(self._language.currentData())
        # Перерисовать уже собранный интерфейс нечем — предупреждаем честно.
        self._language_hint.show()


class StationDialog(QDialog):
    """Название и адрес потока в одном окне.

    Раньше станция заводилась двумя `QInputDialog` подряд: ответив на первый,
    пользователь уже не мог вернуться и поправить название, а отмена на втором
    шаге теряла введённое. Здесь оба поля видны сразу, «ОК» неактивна, пока
    введено не всё, а причина написана прямо под полями.
    """

    def __init__(self, parent: QWidget | None = None, *, name: str = "", url: str = "") -> None:
        super().__init__(parent)
        self.setWindowTitle(self.tr("New station"))
        self.setMinimumWidth(380)

        self._name = QLineEdit(name)
        self._name.setPlaceholderText(self.tr("Station name"))
        self._url = QLineEdit(url)
        self._url.setPlaceholderText("https://example.org/stream.m3u8")

        self._hint = QLabel()
        self._hint.setWordWrap(True)
        # Подсказку приглушаем прозрачностью, а не фиксированным цветом: так она
        # читается и в светлой, и в тёмной теме.
        palette = self._hint.palette()
        muted = palette.color(QPalette.ColorRole.WindowText)
        muted.setAlphaF(0.65)
        palette.setColor(QPalette.ColorRole.WindowText, muted)
        self._hint.setPalette(palette)

        self._buttons = QDialogButtonBox(
            QDialogButtonBox.StandardButton.Ok | QDialogButtonBox.StandardButton.Cancel
        )
        self._buttons.accepted.connect(self.accept)
        self._buttons.rejected.connect(self.reject)

        form = QFormLayout()
        form.addRow(self.tr("Name:"), self._name)
        form.addRow(self.tr("Stream URL:"), self._url)

        layout = QVBoxLayout(self)
        layout.addLayout(form)
        layout.addWidget(self._hint)
        layout.addWidget(self._buttons)

        self._name.textChanged.connect(self._validate)
        self._url.textChanged.connect(self._validate)
        self._validate()

    def values(self) -> tuple[str, str]:
        return self._name.text().strip(), self._url.text().strip()

    def _validate(self) -> None:
        name, url = self.values()
        problem = ""
        if url and not url.startswith(("http://", "https://")):
            problem = self.tr("The URL must start with http:// or https://")

        # Красный поверх приглушённой палитры — тот же цвет, что у ошибки плеера.
        self._hint.setStyleSheet("color: #e05252;" if problem else "")
        self._hint.setText(problem or self.tr("A direct stream URL or an HLS playlist (.m3u8)."))

        ok = self._buttons.button(QDialogButtonBox.StandardButton.Ok)
        ok.setEnabled(bool(name) and bool(url) and not problem)


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
            self.tr("Uncheck a station to hide it. Rename with a double click, reorder by dragging.")
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

    def _on_rows_moved(self, *_args: object) -> None:
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
    def _batch(self) -> Iterator[None]:
        """Применяет правки, отложив перестроение списка до выхода из обработчика."""
        self._applying = True
        try:
            yield
        finally:
            self._applying = False
        QTimer.singleShot(0, self._reload)

    def _add_station(self) -> None:
        dialog = StationDialog(self)
        if dialog.exec() != QDialog.DialogCode.Accepted:
            return
        # Пустых значений диалог не выпускает — проверять их здесь уже не за чем.
        self._store.add_custom(*dialog.values())

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
            QMessageBox.warning(self, self.tr("Import"), self.tr("No stations were found in the file."))
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
        settings: SettingsStore,
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
            self._status.setText(self.tr("Last update: {ago} ago.").format(ago=self._humanize(elapsed)))

    def _humanize(self, seconds: float) -> str:
        """Прошедшее время словами. Формы числа расставляет Qt по правилам языка.

        Метод, а не функция модуля: формы множественного числа lupdate узнаёт
        только по `self.tr(source, disambiguation, n)` — у
        `QCoreApplication.translate()` с тем же `n` он их не видит и молча
        сводит сообщение к одной форме.
        """
        minutes = int(seconds // 60)
        if minutes < 1:
            return self.tr("less than a minute")
        if minutes < 60:
            return self.tr("%n minute(s)", "", minutes)
        hours = minutes // 60
        if hours < 24:
            return self.tr("%n hour(s)", "", hours)
        return self.tr("%n day(s)", "", hours // 24)

    def _add_catalog(self) -> None:
        title = self.tr("New catalog")
        url, ok = QInputDialog.getText(
            self, title, self.tr("Link to a JSON catalog:"), QLineEdit.EchoMode.Normal, "https://"
        )
        url = url.strip()
        if not ok or not url:
            return
        if not url.lower().startswith("https://"):
            QMessageBox.warning(self, title, self.tr("The link must start with https://"))
            return
        if url in catalog.DEFAULT_CATALOG_URLS or not self._settings.add_catalog_url(url):
            return
        self._reload()
        # Добавленный каталог нужен пользователю сейчас, а не после перезапуска.
        self._update_now()

    def _remove_catalog(self) -> None:
        item = self._list.currentItem()
        if item is not None and self._settings.remove_catalog_url(item.text()):
            self._reload()

    def _update_now(self) -> None:
        self._updater.update_now()
        self._update_status()
        self._update_buttons()

    def _on_update_finished(self, _catalogs: list[Catalog], changed: bool) -> None:
        self._update_status()
        self._update_buttons()
        if not changed:
            self._status.setText(self._status.text() + " " + self.tr("No changes."))
