"""Окно настроек: основные параметры, станции и каталоги."""

from __future__ import annotations

from contextlib import contextmanager
from pathlib import Path

from PySide6.QtCore import Qt, QTimer
from PySide6.QtWidgets import (
    QAbstractItemView,
    QCheckBox,
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

from .. import autostart, catalog, playlist_io
from .. import settings as settings_module
from ..settings import MAX_UPDATE_INTERVAL_HOURS, MIN_UPDATE_INTERVAL_HOURS, Settings
from ..station_store import StationStore

#: Роль Qt для хранения id станции в строке списка.
_STATION_ID_ROLE = int(Qt.ItemDataRole.UserRole)


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

        self.setWindowTitle("Настройки")
        self.resize(460, 460)

        tabs = QTabWidget()
        tabs.addTab(GeneralTab(settings), "Основные")
        tabs.addTab(StationsTab(store), "Станции")
        tabs.addTab(CatalogsTab(settings, updater), "Каталоги")

        layout = QVBoxLayout(self)
        layout.addWidget(tabs)


class GeneralTab(QWidget):
    def __init__(self, settings: Settings, parent: QWidget | None = None) -> None:
        super().__init__(parent)
        self._settings = settings

        self._autostart = QCheckBox("Запускать при входе в систему")
        self._autostart.setChecked(autostart.is_enabled())
        self._autostart.toggled.connect(self._on_autostart_toggled)

        self._close_to_tray = QCheckBox("Закрытие окна сворачивает в трей")
        self._close_to_tray.setChecked(settings.close_to_tray)
        self._close_to_tray.toggled.connect(self._on_close_to_tray_toggled)

        self._interval = QSpinBox()
        self._interval.setRange(MIN_UPDATE_INTERVAL_HOURS, MAX_UPDATE_INTERVAL_HOURS)
        self._interval.setValue(settings.update_interval_hours)
        self._interval.setSuffix(" ч")
        self._interval.valueChanged.connect(self._on_interval_changed)

        form = QFormLayout()
        form.addRow(self._autostart)
        form.addRow(self._close_to_tray)
        form.addRow("Обновлять каталоги каждые", self._interval)

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
        QMessageBox.warning(self, "Автозапуск", "Не удалось изменить настройку автозапуска.")

    def _on_close_to_tray_toggled(self, enabled: bool) -> None:
        self._settings.close_to_tray = enabled
        settings_module.save(self._settings)

    def _on_interval_changed(self, hours: int) -> None:
        self._settings.update_interval_hours = hours
        settings_module.save(self._settings)


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

        self._add_button = QPushButton("Добавить...")
        self._add_button.clicked.connect(self._add_station)
        self._duplicate_button = QPushButton("Сделать копию")
        self._duplicate_button.setToolTip(
            "Создаёт вашу копию станции каталога — у копии можно менять адрес потока"
        )
        self._duplicate_button.clicked.connect(self._duplicate_station)
        self._remove_button = QPushButton("Удалить")
        self._remove_button.clicked.connect(self._remove_station)
        self._import_button = QPushButton("Импорт...")
        self._import_button.clicked.connect(self._import_stations)
        self._export_button = QPushButton("Экспорт...")
        self._export_button.clicked.connect(self._export_stations)
        self._reset_button = QPushButton("Сбросить")
        self._reset_button.setToolTip("Вернуть список к состоянию каталогов")
        self._reset_button.clicked.connect(self._reset)

        buttons = QHBoxLayout()
        buttons.addWidget(self._add_button)
        buttons.addWidget(self._duplicate_button)
        buttons.addWidget(self._remove_button)
        buttons.addStretch()
        buttons.addWidget(self._import_button)
        buttons.addWidget(self._export_button)

        hint = QLabel(
            "Снимите галочку, чтобы скрыть станцию. Название можно изменить двойным щелчком, "
            "порядок — перетаскиванием."
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
        name, ok = QInputDialog.getText(self, "Новая станция", "Название:")
        if not ok or not name.strip():
            return
        url, ok = QInputDialog.getText(self, "Новая станция", "Адрес потока:", QLineEdit.EchoMode.Normal, "https://")
        if not ok:
            return
        if self._store.add_custom(name, url) is None:
            QMessageBox.warning(self, "Новая станция", "Нужны название и адрес потока.")

    def _duplicate_station(self) -> None:
        station_id = self._selected_id()
        if station_id:
            self._store.duplicate(station_id)

    def _remove_station(self) -> None:
        station_id = self._selected_id()
        station = self._store.find(station_id) if station_id else None
        if station is None:
            return
        confirm = QMessageBox.question(self, "Удаление", f"Удалить станцию «{station.name}»?")
        if confirm == QMessageBox.StandardButton.Yes:
            self._store.remove_custom(station.id)

    def _import_stations(self) -> None:
        path, _ = QFileDialog.getOpenFileName(
            self, "Импорт станций", "", "Плейлисты (*.m3u *.m3u8 *.pls *.json);;Все файлы (*)"
        )
        if not path:
            return
        stations = playlist_io.read_file(Path(path))
        if not stations:
            QMessageBox.warning(self, "Импорт", "В файле не нашлось станций.")
            return
        for station in stations:
            self._store.add_custom(station.name, station.url)
        QMessageBox.information(self, "Импорт", f"Добавлено станций: {len(stations)}.")

    def _export_stations(self) -> None:
        path, _ = QFileDialog.getSaveFileName(self, "Экспорт станций", "stations.m3u", "Плейлист (*.m3u)")
        if not path:
            return
        try:
            Path(path).write_text(playlist_io.export_m3u(self._store.stations), encoding="utf-8")
        except OSError as exc:
            QMessageBox.warning(self, "Экспорт", f"Не удалось сохранить файл: {exc}")

    def _reset(self) -> None:
        confirm = QMessageBox.question(
            self, "Сброс", "Вернуть список станций к состоянию каталогов? Ваши правки будут потеряны."
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

        self._add_button = QPushButton("Добавить каталог...")
        self._add_button.clicked.connect(self._add_catalog)
        self._remove_button = QPushButton("Удалить")
        self._remove_button.clicked.connect(self._remove_catalog)
        self._update_button = QPushButton("Обновить сейчас")
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
            item.setToolTip(f"{url}\n\nВстроенный каталог — удалить нельзя")
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
            self._status.setText("Обновление...")
            return
        elapsed = catalog.seconds_since_update()
        if elapsed is None:
            self._status.setText("Каталоги ещё не обновлялись.")
        else:
            self._status.setText(f"Последнее обновление: {_humanize(elapsed)} назад.")

    def _add_catalog(self) -> None:
        url, ok = QInputDialog.getText(
            self, "Новый каталог", "Ссылка на JSON-каталог:", QLineEdit.EchoMode.Normal, "https://"
        )
        url = url.strip()
        if not ok or not url:
            return
        if not url.startswith(("http://", "https://")):
            QMessageBox.warning(self, "Новый каталог", "Ссылка должна начинаться с http:// или https://")
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
            self._status.setText(self._status.text() + " Изменений нет.")


def _humanize(seconds: float) -> str:
    minutes = int(seconds // 60)
    if minutes < 1:
        return "меньше минуты"
    if minutes < 60:
        return f"{minutes} мин"
    hours = minutes // 60
    if hours < 24:
        return f"{hours} ч"
    return f"{hours // 24} дн"
