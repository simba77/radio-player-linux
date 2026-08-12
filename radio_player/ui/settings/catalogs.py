"""Вкладка «Каталоги»: источники станций и их обновление."""

from __future__ import annotations

from PySide6.QtCore import Qt
from PySide6.QtWidgets import (
    QHBoxLayout,
    QInputDialog,
    QLabel,
    QLineEdit,
    QListWidget,
    QListWidgetItem,
    QMessageBox,
    QPushButton,
    QVBoxLayout,
    QWidget,
)

from ...core import catalog
from ...core.models import Catalog
from ...core.settings_store import SettingsStore


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
