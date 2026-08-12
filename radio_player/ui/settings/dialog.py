"""Окно настроек: три вкладки и ничего своего."""

from __future__ import annotations

from PySide6.QtWidgets import QDialog, QTabWidget, QVBoxLayout, QWidget

from ...core import catalog
from ...core.settings_store import SettingsStore
from ...core.station_store import StationStore
from .catalogs import CatalogsTab
from .general import GeneralTab
from .stations import StationsTab


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
