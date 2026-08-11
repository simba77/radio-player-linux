"""Хранилище настроек: правка, сохранение и уведомление за одну операцию."""

from __future__ import annotations

import pytest

from radio_player import settings as settings_module
from radio_player.settings_store import SettingsStore

pytestmark = pytest.mark.usefixtures("qt_app")

CATALOG = "https://example.org/catalog.json"
OTHER = "https://example.net/catalog.json"


@pytest.fixture
def store():
    return SettingsStore()


def count_signals(signal) -> list:
    received: list = []
    signal.connect(lambda: received.append(True))
    return received


def test_defaults_are_read_from_disk(store):
    assert store.close_to_tray is True
    assert store.extra_catalog_urls == ()


def test_change_is_saved_immediately(store):
    store.set_close_to_tray(False)
    assert settings_module.load().close_to_tray is False


def test_change_notifies(store):
    changes = count_signals(store.changed)
    store.set_update_interval_hours(6)
    assert len(changes) == 1


def test_writing_the_same_value_changes_nothing(store):
    """Виджеты шлют сигнал и когда значение не менялось — файл трогать не за что."""
    store.set_language("ru")
    changes = count_signals(store.changed)
    store.set_language("ru")
    assert changes == []


def test_interval_is_normalised_like_on_disk(store):
    store.set_update_interval_hours(0)
    assert store.update_interval_hours == 1
    assert settings_module.load().update_interval_hours == 1


def test_added_catalog_is_saved_and_announced(store):
    urls = count_signals(store.catalog_urls_changed)
    assert store.add_catalog_url(CATALOG) is True

    assert store.extra_catalog_urls == (CATALOG,)
    assert settings_module.load().extra_catalog_urls == [CATALOG]
    assert len(urls) == 1


def test_duplicate_catalog_is_rejected(store):
    store.add_catalog_url(CATALOG)
    urls = count_signals(store.catalog_urls_changed)

    assert store.add_catalog_url(CATALOG) is False
    assert urls == []


def test_removed_catalog_is_saved_and_announced(store):
    store.add_catalog_url(CATALOG)
    store.add_catalog_url(OTHER)
    urls = count_signals(store.catalog_urls_changed)

    assert store.remove_catalog_url(CATALOG) is True
    assert store.extra_catalog_urls == (OTHER,)
    assert len(urls) == 1


def test_removing_an_unknown_catalog_changes_nothing(store):
    urls = count_signals(store.catalog_urls_changed)
    assert store.remove_catalog_url(CATALOG) is False
    assert urls == []


def test_catalog_list_cannot_be_edited_behind_the_store(store):
    """Читатель получает копию: иначе правка списка прошла бы мимо сохранения."""
    store.add_catalog_url(CATALOG)
    urls = store.extra_catalog_urls
    assert isinstance(urls, tuple)
    assert store.extra_catalog_urls == (CATALOG,)


def test_settings_survive_a_restart(store):
    store.set_close_to_tray(False)
    store.set_language("en")
    store.add_catalog_url(CATALOG)

    restarted = SettingsStore()
    assert restarted.close_to_tray is False
    assert restarted.language == "en"
    assert restarted.extra_catalog_urls == (CATALOG,)
