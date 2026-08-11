"""Сборка приложения.

`main()` целиком не запускается — ему нужны окно, трей и шина. Но связки между
частями живут именно там, и раньше ошибка в одной из них стоила бага, который
никакой модульный тест увидеть не мог: список каталогов правился в настройках,
а обновление ходило по списку, снятому при запуске.
"""

from __future__ import annotations

import json

import pytest

from radio_player import catalog
from radio_player.__main__ import wire_catalog_sources
from radio_player.settings_store import SettingsStore
from radio_player.station_store import StationStore

pytestmark = pytest.mark.usefixtures("qt_app")

EXTRA_URL = "https://example.org/extra.json"
EXTRA_CATALOG = {
    "schema": 1,
    "id": "extra",
    "name": "Дополнительный",
    "revision": 1,
    "stations": [{"id": "extra-1", "name": "Из добавленного каталога", "url": "https://example.org/s"}],
}


@pytest.fixture
def wired():
    settings = SettingsStore()
    updater = catalog.CatalogUpdater(settings.extra_catalog_urls)
    store = StationStore(catalog.load_initial(updater.urls))
    wire_catalog_sources(settings, updater, store)
    return settings, updater, store


def put_in_cache(url: str, data: dict) -> None:
    """Каталог, уже скачанный обновлением: сеть в тестах не трогаем."""
    path = catalog._cache_path(url)
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(data), encoding="utf-8")


def test_added_catalog_reaches_the_updater(wired):
    settings, updater, _ = wired
    settings.add_catalog_url(EXTRA_URL)
    assert EXTRA_URL in updater.urls


def test_added_catalog_reaches_the_station_list(wired):
    settings, _, store = wired
    put_in_cache(EXTRA_URL, EXTRA_CATALOG)

    settings.add_catalog_url(EXTRA_URL)
    assert "extra-1" in [station.id for station in store.stations]


def test_removed_catalog_leaves_the_station_list(wired):
    settings, _, store = wired
    put_in_cache(EXTRA_URL, EXTRA_CATALOG)
    settings.add_catalog_url(EXTRA_URL)

    settings.remove_catalog_url(EXTRA_URL)
    assert "extra-1" not in [station.id for station in store.stations]
