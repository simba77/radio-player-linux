"""Список источников каталогов.

Сеть здесь не трогаем — проверяем только то, из-за чего добавленный в настройках
каталог не работал до перезапуска: какие адреса обновление считает своими.
"""

from __future__ import annotations

import pytest

from radio_player.core import catalog

pytestmark = pytest.mark.usefixtures("qt_app")

OTHER = "https://example.org/catalog.json"
THIRD = "https://example.net/catalog.json"


def test_bundled_urls_are_used_without_extras():
    updater = catalog.CatalogUpdater()
    assert updater.urls == catalog.DEFAULT_CATALOG_URLS


def test_extra_urls_go_after_bundled_ones():
    updater = catalog.CatalogUpdater([OTHER])
    assert updater.urls == (*catalog.DEFAULT_CATALOG_URLS, OTHER)


def test_added_catalog_is_picked_up_without_a_restart():
    updater = catalog.CatalogUpdater()
    updater.set_extra_urls([OTHER])
    assert OTHER in updater.urls


def test_removed_catalog_is_no_longer_fetched():
    updater = catalog.CatalogUpdater([OTHER, THIRD])
    updater.set_extra_urls([THIRD])
    assert OTHER not in updater.urls
    assert THIRD in updater.urls


def test_extra_urls_are_copied_not_aliased():
    """Список из настроек продолжает жить своей жизнью — правки в нём
    не должны менять состав обновления в обход `set_extra_urls`."""
    urls = [OTHER]
    updater = catalog.CatalogUpdater(urls)
    urls.append(THIRD)
    assert THIRD not in updater.urls
