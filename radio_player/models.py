"""Модели данных: станция, трек, каталог станций и локальный оверлей.

Схема каталогов и правила мержа описаны в PLAN.md §3.
"""

from __future__ import annotations

import uuid
from dataclasses import dataclass, field
from typing import Any

#: Версия схемы, которую понимает это приложение. Каталог с большей версией игнорируется.
SCHEMA_VERSION = 1
#: Префикс идентификаторов станций, созданных пользователем.
LOCAL_ID_PREFIX = "local:"


@dataclass(frozen=True, slots=True)
class Station:
    """Радиостанция. `id` — стабильный ключ: slug из каталога или `local:<uuid>`."""

    name: str
    url: str
    id: str = field(default_factory=lambda: f"{LOCAL_ID_PREFIX}{uuid.uuid4()}")

    @property
    def is_hls(self) -> bool:
        return self.url.split("?", 1)[0].endswith(".m3u8")

    @property
    def is_custom(self) -> bool:
        """Создана пользователем, а не пришла из каталога."""
        return self.id.startswith(LOCAL_ID_PREFIX)

    def to_dict(self) -> dict[str, str]:
        return {"id": self.id, "name": self.name, "url": self.url}

    @classmethod
    def from_dict(cls, data: Any) -> Station | None:
        """Разбирает станцию, отбрасывая мусор. Неизвестные поля игнорируются."""
        if not isinstance(data, dict):
            return None
        name = str(data.get("name") or "").strip()
        # `urlString` — имя поля в macOS-версии, поддерживаем для импорта.
        url = str(data.get("url") or data.get("urlString") or "").strip()
        if not name or not url:
            return None
        station_id = str(data.get("id") or "").strip()
        if not station_id:
            return cls(name=name, url=url)
        return cls(name=name, url=url, id=station_id)


@dataclass(frozen=True, slots=True)
class TrackInfo:
    """Текущий трек, как его отдал провайдер метаданных."""

    title: str
    artist: str | None = None
    artwork_url: str | None = None

    @property
    def display(self) -> str:
        """`Исполнитель — Трек`, как в macOS-версии (`formattedTrack`)."""
        return f"{self.artist} — {self.title}" if self.artist else self.title


@dataclass(frozen=True, slots=True)
class Catalog:
    """Подборка станций, публикуемая в репозитории (PLAN.md §3.1)."""

    id: str
    name: str
    revision: int
    stations: tuple[Station, ...]

    @classmethod
    def from_dict(cls, data: Any) -> Catalog | None:
        """Возвращает None, если каталог битый или его схема новее поддерживаемой.

        Каталог приходит из сети, поэтому доверять ему нельзя: одна опечатка
        в JSON не должна лишать пользователя всего списка станций.
        """
        if not isinstance(data, dict):
            return None
        schema = data.get("schema")
        if not isinstance(schema, int) or schema > SCHEMA_VERSION:
            return None
        catalog_id = str(data.get("id") or "").strip()
        if not catalog_id:
            return None

        stations: list[Station] = []
        seen: set[str] = set()
        for raw in data.get("stations") or []:
            station = Station.from_dict(raw)
            # Станция каталога обязана иметь стабильный id — без него мерж развалится.
            if station is None or station.is_custom or station.id in seen:
                continue
            seen.add(station.id)
            stations.append(station)
        if not stations:
            return None

        revision = data.get("revision")
        return cls(
            id=catalog_id,
            name=str(data.get("name") or catalog_id),
            revision=revision if isinstance(revision, int) else 0,
            stations=tuple(stations),
        )


@dataclass
class Overlay:
    """Правки пользователя поверх каталогов (PLAN.md §3.2).

    Хранится отдельно от каталогов, поэтому их обновление не затирает
    переименования, скрытия и порядок.
    """

    order: list[str] = field(default_factory=list)
    hidden: set[str] = field(default_factory=set)
    overrides: dict[str, dict[str, str]] = field(default_factory=dict)
    custom: list[Station] = field(default_factory=list)

    def to_dict(self) -> dict[str, Any]:
        return {
            "schema": SCHEMA_VERSION,
            "order": list(self.order),
            "hidden": sorted(self.hidden),
            "overrides": self.overrides,
            "custom": [station.to_dict() for station in self.custom],
        }

    @classmethod
    def from_dict(cls, data: Any) -> Overlay:
        """Битый оверлей — это потеря пользовательских правок, но не повод падать."""
        if not isinstance(data, dict):
            return cls()

        custom: list[Station] = []
        for raw in data.get("custom") or []:
            station = Station.from_dict(raw)
            if station is not None:
                custom.append(station)

        overrides: dict[str, dict[str, str]] = {}
        for key, value in (data.get("overrides") or {}).items():
            if isinstance(value, dict) and isinstance(value.get("name"), str):
                overrides[str(key)] = {"name": value["name"]}

        return cls(
            order=[str(item) for item in data.get("order") or []],
            hidden={str(item) for item in data.get("hidden") or []},
            overrides=overrides,
            custom=custom,
        )


def split_icy_title(raw: str) -> TrackInfo | None:
    """Разбирает `icy-title` вида `ARTIST - Title`.

    Разделитель ` - ` не гарантирован стандартом, поэтому при его отсутствии
    всю строку считаем названием трека.
    """
    cleaned = raw.strip()
    if not cleaned:
        return None
    artist, separator, title = cleaned.partition(" - ")
    if separator and artist.strip() and title.strip():
        return TrackInfo(title=title.strip(), artist=artist.strip())
    return TrackInfo(title=cleaned)
