"""Пользовательские настройки приложения (`settings.json`).

Автозапуск сюда не входит: его состояние определяется наличием `.desktop`-файла,
и дублировать его в настройках значило бы завести второй источник правды.
"""

from __future__ import annotations

from dataclasses import dataclass, field
from pathlib import Path
from typing import Any

from . import paths, storage
from .models import SCHEMA_VERSION

#: Языки интерфейса. `en` — исходные строки в коде, перевод для него не нужен.
LANGUAGES = ("en", "ru")
#: Значение настройки «как в системе».
AUTO = "auto"

#: Границы интервала обновления каталогов, часы.
MIN_UPDATE_INTERVAL_HOURS = 1
MAX_UPDATE_INTERVAL_HOURS = 24 * 30


@dataclass
class Settings:
    #: Закрытие окна прячет его в трей вместо выхода из приложения.
    close_to_tray: bool = True
    #: Как часто обновлять каталоги станций.
    update_interval_hours: int = 24
    #: Дополнительные каталоги, добавленные пользователем поверх встроенных.
    extra_catalog_urls: list[str] = field(default_factory=list)
    #: Язык интерфейса: `auto` (как в системе) или код из `i18n.LANGUAGES`.
    language: str = AUTO

    def to_dict(self) -> dict[str, Any]:
        return {
            "schema": SCHEMA_VERSION,
            "close_to_tray": self.close_to_tray,
            "update_interval_hours": self.update_interval_hours,
            "extra_catalog_urls": list(self.extra_catalog_urls),
            "language": self.language,
        }

    @classmethod
    def from_dict(cls, data: Any) -> Settings:
        if not isinstance(data, dict):
            return cls()

        interval = data.get("update_interval_hours")
        if not isinstance(interval, int):
            interval = cls.update_interval_hours
        interval = max(MIN_UPDATE_INTERVAL_HOURS, min(MAX_UPDATE_INTERVAL_HOURS, interval))

        # Только HTTPS: каталог задаёт, что и откуда играть, и подменить его
        # по дороге — значит подменить весь список станций пользователя.
        urls = [
            str(url).strip()
            for url in data.get("extra_catalog_urls") or []
            if str(url).strip().lower().startswith("https://")
        ]

        language = data.get("language")
        if language not in (AUTO, *LANGUAGES):
            language = cls.language

        return cls(
            close_to_tray=bool(data.get("close_to_tray", True)),
            update_interval_hours=interval,
            extra_catalog_urls=urls,
            language=language,
        )

    @property
    def update_interval_seconds(self) -> int:
        return self.update_interval_hours * 3600


def load() -> Settings:
    return Settings.from_dict(storage.read_json(_path(), default={}))


def save(settings: Settings) -> bool:
    return storage.write_json(_path(), settings.to_dict())


def _path() -> Path:
    return paths.settings_file()
