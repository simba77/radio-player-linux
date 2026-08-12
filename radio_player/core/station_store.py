"""Список станций: мерж каталогов с локальным оверлеем.

Алгоритм описан в `docs/architecture.md`. Главное правило: каталог владеет URL станции,
пользователь владеет видимостью, названием и порядком. Поэтому обновление
каталога никогда не конфликтует с правками — они лежат в разных местах.
"""

from __future__ import annotations

from dataclasses import replace
from pathlib import Path

from PySide6.QtCore import QObject, Signal

from . import paths, storage
from .models import Catalog, Overlay, Station, is_playable_url


class StationStore(QObject):
    """Итоговый список станций и операции пользователя над ним."""

    stations_changed = Signal()

    def __init__(self, catalogs: list[Catalog] | None = None, parent: QObject | None = None) -> None:
        super().__init__(parent)
        self._catalogs: list[Catalog] = list(catalogs or [])
        self._overlay = Overlay.from_dict(storage.read_json(self._overlay_path(), default={}))
        self._stations: list[Station] = []
        self._rebuild()

    # MARK: - Чтение

    @property
    def stations(self) -> list[Station]:
        """Видимые станции в порядке отображения."""
        return list(self._stations)

    @property
    def hidden_stations(self) -> list[Station]:
        """Скрытые станции каталогов — нужны экрану настроек, чтобы их вернуть."""
        return [
            self._apply_override(station)
            for station in self._catalog_stations()
            if station.id in self._overlay.hidden
        ]

    def find(self, station_id: str) -> Station | None:
        for station in self._stations:
            if station.id == station_id:
                return station
        return None

    # MARK: - Каталоги

    def set_catalogs(self, catalogs: list[Catalog]) -> None:
        """Заменяет набор каталогов (после загрузки или обновления из сети)."""
        self._catalogs = list(catalogs)
        self._rebuild()

    # MARK: - Операции пользователя

    def hide(self, station_id: str) -> None:
        """Скрывает станцию каталога. Свои станции скрывать нечем — их удаляют."""
        if station_id in self._overlay.hidden:
            return
        self._overlay.hidden.add(station_id)
        self._commit()

    def unhide(self, station_id: str) -> None:
        if station_id not in self._overlay.hidden:
            return
        self._overlay.hidden.discard(station_id)
        self._commit()

    def rename(self, station_id: str, name: str) -> None:
        """Переименовывает станцию: свою — насовсем, из каталога — через override."""
        cleaned = name.strip()
        if not cleaned:
            return

        for index, station in enumerate(self._overlay.custom):
            if station.id == station_id:
                self._overlay.custom[index] = replace(station, name=cleaned)
                self._commit()
                return

        original = self._catalog_station(station_id)
        if original is None:
            return
        if original.name == cleaned:
            self._overlay.overrides.pop(station_id, None)
        else:
            self._overlay.overrides[station_id] = {"name": cleaned}
        self._commit()

    def add_custom(self, name: str, url: str) -> Station | None:
        cleaned_name, cleaned_url = name.strip(), url.strip()
        # Проверка здесь, а не только в диалоге: станции приходят ещё и импортом.
        if not cleaned_name or not is_playable_url(cleaned_url):
            return None
        station = Station(name=cleaned_name, url=cleaned_url)
        self._overlay.custom.append(station)
        self._overlay.order = [*self._current_order(), station.id]
        self._commit()
        return station

    def remove_custom(self, station_id: str) -> None:
        before = len(self._overlay.custom)
        self._overlay.custom = [s for s in self._overlay.custom if s.id != station_id]
        if len(self._overlay.custom) == before:
            return
        self._overlay.order = [i for i in self._overlay.order if i != station_id]
        self._commit()

    def duplicate(self, station_id: str) -> Station | None:
        """Создаёт свою копию станции каталога и скрывает оригинал.

        Так пользователь получает право менять URL, не ломая мерж: копия живёт
        в оверлее, а каталог продолжает обновляться сам по себе.
        """
        original = self.find(station_id)
        if original is None or original.is_custom:
            return None
        copy = Station(name=original.name, url=original.url)
        self._overlay.custom.append(copy)
        self._overlay.hidden.add(station_id)
        self._overlay.order = [copy.id if item == station_id else item for item in self._current_order()]
        self._commit()
        return copy

    def set_custom_url(self, station_id: str, url: str) -> None:
        cleaned = url.strip()
        if not is_playable_url(cleaned):
            return
        for index, station in enumerate(self._overlay.custom):
            if station.id == station_id:
                self._overlay.custom[index] = replace(station, url=cleaned)
                self._commit()
                return

    def set_order(self, visible_order: list[str]) -> None:
        """Задаёт порядок видимых станций (после перетаскивания в настройках)."""
        known = {station.id for station in self._stations}
        filtered = [station_id for station_id in visible_order if station_id in known]
        if len(filtered) != len(known):
            return  # Пришёл неполный список — не рискуем порядком.
        self._overlay.order = _merge_order(filtered, self._current_order())
        self._commit()

    def move(self, from_index: int, to_index: int) -> None:
        order = [station.id for station in self._stations]
        if not (0 <= from_index < len(order)) or not (0 <= to_index < len(order)):
            return
        order.insert(to_index, order.pop(from_index))
        # В order попадают и скрытые станции — их относительный порядок надо сохранить.
        self._overlay.order = _merge_order(order, self._current_order())
        self._commit()

    def reset(self) -> None:
        """Возвращает всё к состоянию каталогов: правки пользователя стираются."""
        self._overlay = Overlay()
        self._commit()

    # MARK: - Мерж

    def _rebuild(self) -> None:
        merged: list[Station] = []
        seen: set[str] = set()

        # 1-2. Станции каталогов; при коллизии id побеждает более ранний каталог.
        for station in self._catalog_stations():
            if station.id in seen:
                continue
            seen.add(station.id)
            merged.append(station)

        # 3. Свои станции пользователя.
        for station in self._overlay.custom:
            if station.id in seen:
                continue
            seen.add(station.id)
            merged.append(station)

        # 4-5. Скрытые убираем, переименования применяем.
        visible = [self._apply_override(s) for s in merged if s.id not in self._overlay.hidden]

        # 6. Порядок из оверлея; новые станции каталога уходят в конец.
        position = {station_id: index for index, station_id in enumerate(self._overlay.order)}
        tail = len(position)
        # sorted стабильна, поэтому станции вне order сохраняют порядок каталога.
        self._stations = sorted(visible, key=lambda station: position.get(station.id, tail))
        self.stations_changed.emit()

    def _catalog_stations(self) -> list[Station]:
        return [station for catalog in self._catalogs for station in catalog.stations]

    def _catalog_station(self, station_id: str) -> Station | None:
        for station in self._catalog_stations():
            if station.id == station_id:
                return station
        return None

    def _apply_override(self, station: Station) -> Station:
        override = self._overlay.overrides.get(station.id)
        if override and override.get("name"):
            return replace(station, name=override["name"])
        return station

    def _current_order(self) -> list[str]:
        """Текущий порядок с учётом станций, которых ещё нет в оверлее."""
        known = set(self._overlay.order)
        extra = [s.id for s in self._stations if s.id not in known]
        return [*self._overlay.order, *extra]

    # MARK: - Сохранение

    def _commit(self) -> None:
        storage.write_json(self._overlay_path(), self._overlay.to_dict())
        self._rebuild()

    @staticmethod
    def _overlay_path() -> Path:
        return paths.config_dir() / "overlay.json"


def _merge_order(visible_order: list[str], full_order: list[str]) -> list[str]:
    """Вставляет скрытые станции обратно, сохраняя их привязку к соседям.

    Пользователь двигает только видимые строки, но `order` описывает весь список.
    Каждую скрытую станцию привязываем к ближайшей видимой сверху и возвращаем
    сразу после неё — иначе возвращённая из скрытых станция уезжала бы в конец.
    """
    visible = set(visible_order)
    leading: list[str] = []
    followers: dict[str, list[str]] = {}
    anchor: str | None = None

    for station_id in full_order:
        if station_id in visible:
            anchor = station_id
        elif anchor is None:
            leading.append(station_id)
        else:
            followers.setdefault(anchor, []).append(station_id)

    result = list(leading)
    for station_id in visible_order:
        result.append(station_id)
        result.extend(followers.get(station_id, ()))
    return result
