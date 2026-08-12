"""Вкладка «Основные»: автозапуск, поведение окна, обновление, язык."""

from __future__ import annotations

from PySide6.QtWidgets import (
    QCheckBox,
    QComboBox,
    QFormLayout,
    QLabel,
    QMessageBox,
    QSpinBox,
    QVBoxLayout,
    QWidget,
)

from ... import i18n
from ...core.settings import MAX_UPDATE_INTERVAL_HOURS, MIN_UPDATE_INTERVAL_HOURS
from ...core.settings_store import SettingsStore
from ...desktop import autostart

#: Названия языков не переводятся: каждый пишется на себе самом.
_LANGUAGE_NAMES = {"en": "English", "ru": "Русский"}


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
