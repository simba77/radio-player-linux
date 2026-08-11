"""Настройки приложения как единый источник правды.

Раньше каждый виджет правил общий объект `Settings` и сам звал `save()`, а те,
кто на настройку опирался, об изменении не узнавали: список каталогов
поправился в одном окне, а обновление продолжало ходить по списку, снятому при
запуске. Здесь правка и сохранение — одна операция, а об изменении сообщает
сигнал, поэтому забыть уведомить потребителя нечем.

Симметрично `StationStore`: тот владеет списком станций, этот — настройками.
"""

from __future__ import annotations

from PySide6.QtCore import QObject, Signal

from . import settings as settings_module
from .settings import Settings


class SettingsStore(QObject):
    #: Изменилась любая настройка.
    changed = Signal()
    #: Изменился список пользовательских каталогов.
    catalog_urls_changed = Signal()

    def __init__(self, settings: Settings | None = None, parent: QObject | None = None) -> None:
        super().__init__(parent)
        self._settings = settings if settings is not None else settings_module.load()

    # MARK: - Чтение

    @property
    def close_to_tray(self) -> bool:
        return self._settings.close_to_tray

    @property
    def update_interval_hours(self) -> int:
        return self._settings.update_interval_hours

    @property
    def update_interval_seconds(self) -> int:
        return self._settings.update_interval_seconds

    @property
    def language(self) -> str:
        return self._settings.language

    @property
    def extra_catalog_urls(self) -> tuple[str, ...]:
        """Копия: список настроек нельзя править в обход хранилища."""
        return tuple(self._settings.extra_catalog_urls)

    # MARK: - Запись

    def set_close_to_tray(self, value: bool) -> None:
        self._update(close_to_tray=bool(value))

    def set_update_interval_hours(self, hours: int) -> None:
        self._update(update_interval_hours=int(hours))

    def set_language(self, language: str) -> None:
        self._update(language=language)

    def add_catalog_url(self, url: str) -> bool:
        """Добавляет каталог. False — если такой уже есть."""
        cleaned = url.strip()
        if not cleaned or cleaned in self._settings.extra_catalog_urls:
            return False
        self._settings.extra_catalog_urls.append(cleaned)
        self._commit()
        self.catalog_urls_changed.emit()
        return True

    def remove_catalog_url(self, url: str) -> bool:
        if url not in self._settings.extra_catalog_urls:
            return False
        self._settings.extra_catalog_urls.remove(url)
        self._commit()
        self.catalog_urls_changed.emit()
        return True

    # MARK: - Сохранение

    def _update(self, **values) -> None:
        # Настройки приходят из виджетов, а те шлют сигнал и когда значение
        # не изменилось: без этой проверки каждый показ окна писал бы файл.
        if all(getattr(self._settings, name) == value for name, value in values.items()):
            return
        for name, value in values.items():
            setattr(self._settings, name, value)
        self._commit()
        self.changed.emit()

    def _commit(self) -> None:
        # Значения нормализуются тем же кодом, что и при чтении с диска:
        # интервал обновления, например, ограничен разумными рамками.
        self._settings = Settings.from_dict(self._settings.to_dict())
        settings_module.save(self._settings)
