"""Тесты мержа каталогов с оверлеем — PLAN.md §3.3."""

from __future__ import annotations

import pytest

from radio_player.models import Catalog, Station
from radio_player.station_store import StationStore


def catalog(*pairs: tuple[str, str], catalog_id: str = "test", revision: int = 1) -> Catalog:
    stations = tuple(Station(id=sid, name=name, url=f"https://example.org/{sid}.m3u8") for sid, name in pairs)
    return Catalog(id=catalog_id, name=catalog_id, revision=revision, stations=stations)


BASE = catalog(("a", "Первая"), ("b", "Вторая"), ("c", "Третья"))


@pytest.fixture
def store():
    return StationStore([BASE])


def names(store: StationStore) -> list[str]:
    return [station.name for station in store.stations]


def ids(store: StationStore) -> list[str]:
    return [station.id for station in store.stations]


# MARK: - Базовый мерж


def test_stations_come_from_catalog_in_order(store):
    assert ids(store) == ["a", "b", "c"]


def test_earlier_catalog_wins_on_id_collision():
    other = catalog(("b", "Дубль"), catalog_id="other")
    store = StationStore([BASE, other])
    assert [s.name for s in store.stations if s.id == "b"] == ["Вторая"]


def test_custom_stations_go_after_catalog(store):
    store.add_custom("Моё радио", "https://example.org/mine.mp3")
    assert names(store)[-1] == "Моё радио"


# MARK: - Правки пользователя


def test_hidden_station_disappears(store):
    store.hide("b")
    assert ids(store) == ["a", "c"]


def test_unhide_restores_station(store):
    store.hide("b")
    store.unhide("b")
    assert ids(store) == ["a", "b", "c"]


def test_rename_catalog_station_keeps_url(store):
    store.rename("a", "Переименованная")
    station = store.find("a")
    assert station.name == "Переименованная"
    assert station.url == "https://example.org/a.m3u8"


def test_rename_back_to_original_drops_override(store):
    store.rename("a", "Переименованная")
    store.rename("a", "Первая")
    assert store._overlay.overrides == {}


def test_rename_custom_station_is_permanent(store):
    created = store.add_custom("Моё", "https://example.org/mine.mp3")
    store.rename(created.id, "Моё радио")
    assert store.find(created.id).name == "Моё радио"
    assert store._overlay.overrides == {}


def test_remove_custom_station(store):
    created = store.add_custom("Моё", "https://example.org/mine.mp3")
    store.remove_custom(created.id)
    assert created.id not in ids(store)


def test_duplicate_replaces_catalog_station_in_place(store):
    copy = store.duplicate("b")
    assert copy is not None
    assert copy.url == "https://example.org/b.m3u8"
    # Копия встаёт на место оригинала, оригинал скрывается.
    assert ids(store) == ["a", copy.id, "c"]
    assert "b" in store._overlay.hidden


def test_duplicate_allows_editing_url(store):
    copy = store.duplicate("b")
    store.set_custom_url(copy.id, "https://example.org/custom.mp3")
    assert store.find(copy.id).url == "https://example.org/custom.mp3"


def test_catalog_station_url_cannot_be_edited(store):
    store.set_custom_url("a", "https://example.org/hack.mp3")
    assert store.find("a").url == "https://example.org/a.m3u8"


# MARK: - Порядок


def test_move_changes_order(store):
    store.move(2, 0)
    assert ids(store) == ["c", "a", "b"]


def test_new_catalog_stations_append_to_end(store):
    store.move(2, 0)  # зафиксировали свой порядок
    store.set_catalogs([catalog(("a", "Первая"), ("b", "Вторая"), ("c", "Третья"), ("d", "Новая"))])
    assert ids(store) == ["c", "a", "b", "d"]


def test_set_order_applies_new_sequence(store):
    store.set_order(["c", "b", "a"])
    assert ids(store) == ["c", "b", "a"]


def test_set_order_ignores_incomplete_list(store):
    """Неполный список — признак рассинхрона UI, порядок трогать нельзя."""
    store.set_order(["c"])
    assert ids(store) == ["a", "b", "c"]


def test_set_order_keeps_hidden_stations(store):
    store.hide("b")
    store.set_order(["c", "a"])
    store.unhide("b")
    assert ids(store) == ["c", "a", "b"]


def test_hidden_station_returns_to_its_place(store):
    """Скрытая станция не должна уезжать в конец после возврата."""
    store.hide("b")
    store.move(1, 0)  # было [a, c] → стало [c, a]
    store.unhide("b")
    assert ids(store) == ["c", "a", "b"]


# MARK: - Обновление каталогов


def test_catalog_update_changes_url_but_keeps_rename(store):
    store.rename("a", "Моё название")
    updated = Catalog(
        id="test",
        name="test",
        revision=2,
        stations=(Station(id="a", name="Первая", url="https://example.org/new-a.m3u8"),),
    )
    store.set_catalogs([updated])
    station = store.find("a")
    assert station.name == "Моё название"
    assert station.url == "https://example.org/new-a.m3u8"


def test_station_removed_from_catalog_disappears(store):
    store.set_catalogs([catalog(("a", "Первая"))])
    assert ids(store) == ["a"]


def test_custom_stations_survive_catalog_removal(store):
    created = store.add_custom("Моё", "https://example.org/mine.mp3")
    store.set_catalogs([])
    assert ids(store) == [created.id]


# MARK: - Сохранение и сброс


def test_overlay_survives_restart(store):
    store.rename("a", "Моё название")
    store.hide("c")
    store.add_custom("Моё радио", "https://example.org/mine.mp3")

    restarted = StationStore([BASE])
    assert [s.name for s in restarted.stations] == ["Моё название", "Вторая", "Моё радио"]


def test_reset_discards_user_changes(store):
    store.rename("a", "Моё название")
    store.hide("b")
    store.add_custom("Моё радио", "https://example.org/mine.mp3")
    store.reset()
    assert names(store) == ["Первая", "Вторая", "Третья"]


def test_empty_catalog_list_gives_empty_store():
    assert StationStore([]).stations == []
