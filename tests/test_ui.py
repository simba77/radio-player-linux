"""Интерфейс: сборка окна и реакции на состояние плеера.

Виджеты почти тысяча строк, и до сих пор их не проверял никто: ошибка в
подключении сигнала или в имени метода обнаруживалась только запуском. Здесь
интерфейс собирается целиком под offscreen-платформой, без дисплея и без звука.

pytest-qt не нужен: клики нам не требуются, а сигналы дёргаются напрямую.
"""

from __future__ import annotations

import pytest
from doubles import FakeMpv
from PySide6.QtCore import Qt
from PySide6.QtWidgets import QCheckBox, QListWidget, QTabWidget

from radio_player.core import catalog
from radio_player.core.models import Catalog, Station, TrackInfo
from radio_player.core.settings_store import SettingsStore
from radio_player.core.station_store import StationStore
from radio_player.playback.player import PlayerManager
from radio_player.ui import icons
from radio_player.ui.main_window import MainWindow
from radio_player.ui.settings import SettingsDialog
from radio_player.ui.tray import TrayIcon

pytestmark = pytest.mark.usefixtures("qt_app")

FIRST = Station(id="first", name="Первая", url="https://example.org/first")
SECOND = Station(id="second", name="Вторая", url="https://example.org/second.m3u8")
CATALOG = Catalog(id="test", name="Тест", revision=1, stations=(FIRST, SECOND))


@pytest.fixture
def app_parts():
    """Приложение, собранное как в `__main__`, но без сети, звука и шины."""
    store = StationStore([CATALOG])
    settings = SettingsStore()
    mpv = FakeMpv()
    player = PlayerManager(store, mpv_factory=lambda: mpv)
    updater = catalog.CatalogUpdater(settings.extra_catalog_urls)
    window = MainWindow(player, store, settings, updater)
    yield window, player, mpv, store, settings
    window.close()
    player.shutdown()


# MARK: - Главное окно


def test_window_lists_the_stations(app_parts):
    window, *_ = app_parts
    titles = [window._list.item(row).text() for row in range(window._list.count())]
    assert titles == ["Первая", "Вторая"]


def test_empty_list_shows_a_placeholder():
    store = StationStore([])
    settings = SettingsStore()
    player = PlayerManager(store, mpv_factory=FakeMpv)
    window = MainWindow(player, store, settings, catalog.CatalogUpdater())
    try:
        assert window._list.count() == 1
        assert window._list.item(0).text() == "The station list is empty"
    finally:
        window.close()
        player.shutdown()


def test_clicking_a_station_starts_it(app_parts):
    window, player, mpv, *_ = app_parts
    window._on_item_clicked(window._list.item(1))

    assert player.current_station == SECOND
    assert mpv.played == [SECOND.url]


def test_clicking_the_playing_station_stops_it(app_parts):
    window, player, mpv, *_ = app_parts
    window._on_item_clicked(window._list.item(0))
    mpv.emit("core-idle", False)

    window._on_item_clicked(window._list.item(0))
    assert player.is_active is False


def test_new_stations_reach_the_window(app_parts):
    window, _player, _mpv, store, _settings = app_parts
    store.add_custom("Своя", "https://example.org/mine")

    titles = [window._list.item(row).text() for row in range(window._list.count())]
    assert "Своя" in titles


# MARK: - «Сейчас играет»


def test_now_playing_shows_the_station_then_the_track(app_parts):
    window, player, mpv, *_ = app_parts
    now_playing = window._now_playing

    player.play(FIRST)
    mpv.emit("core-idle", False)
    assert now_playing._title.text() == "Первая"

    player._on_track_changed(TrackInfo(title="Трек", artist="Кто-то"))
    assert now_playing._title.text() == "Кто-то — Трек"
    assert now_playing._subtitle.text() == "Первая"


def test_now_playing_reports_loading_and_reconnecting(app_parts):
    window, player, mpv, *_ = app_parts
    now_playing = window._now_playing

    player.play(FIRST)
    assert now_playing._title.text() == "Loading..."

    mpv.emit("core-idle", False)
    mpv.end_file(4)
    assert now_playing._title.text() == "Reconnecting..."


def test_play_button_follows_the_player(app_parts):
    window, player, *_ = app_parts
    button = window._now_playing._play_button

    assert button.toolTip() == "Play"
    player.play(FIRST)
    # Кнопка показывает «стоп» уже во время загрузки: пользователь ждёт звука.
    assert button.toolTip() == "Stop"

    player.stop()
    assert button.toolTip() == "Play"


def test_error_is_shown_and_cleared(app_parts):
    window, player, mpv, *_ = app_parts
    error_label = window._now_playing._error

    mpv.fail_on_play = True
    player.play(FIRST)
    # isVisible() ложно для всего дерева, пока окно не показано, — спрашиваем
    # про сам виджет.
    assert error_label.isHidden() is False
    assert error_label.text()

    mpv.fail_on_play = False
    player.play(FIRST)
    assert error_label.text() == ""


def test_volume_slider_follows_external_changes(app_parts):
    window, player, *_ = app_parts
    slider = window._now_playing._volume

    player.set_volume(37)
    assert slider.value() == 37


# MARK: - Настройки


def test_settings_dialog_builds_all_tabs(app_parts):
    window, *_ = app_parts
    window.open_settings()
    dialog = window._settings_dialog
    assert isinstance(dialog, SettingsDialog)

    tabs = dialog.findChild(QTabWidget)
    assert [tabs.tabText(index) for index in range(tabs.count())] == [
        "General",
        "Stations",
        "Catalogs",
    ]


def test_settings_window_is_reused(app_parts):
    window, *_ = app_parts
    window.open_settings()
    first = window._settings_dialog

    window.open_settings()
    assert window._settings_dialog is first


def test_toggling_a_checkbox_reaches_the_settings(app_parts):
    """Полный путь: щелчок по флажку — хранилище — файл на диске."""
    window, _player, _mpv, _store, settings = app_parts
    window.open_settings()

    checkbox = next(
        box
        for box in window._settings_dialog.findChildren(QCheckBox)
        if box.text() == "Closing the window hides it in the tray"
    )
    assert checkbox.isChecked() is True

    checkbox.setChecked(False)
    assert settings.close_to_tray is False
    assert SettingsStore().close_to_tray is False


def test_stations_tab_lists_visible_and_hidden(app_parts):
    window, _player, _mpv, store, _settings = app_parts
    store.hide(SECOND.id)
    window.open_settings()

    lists = window._settings_dialog.findChildren(QListWidget)
    stations = next(widget for widget in lists if widget.count() == 2)
    states = {
        stations.item(row).text(): stations.item(row).checkState() == Qt.CheckState.Checked
        for row in range(stations.count())
    }
    assert states == {"Первая": True, "Вторая": False}


# MARK: - Трей


def test_tray_reflects_playback(app_parts):
    _window, player, mpv, *_ = app_parts
    tray = TrayIcon(
        player,
        on_toggle_window=lambda: None,
        on_settings=lambda: None,
        on_quit=lambda: None,
    )
    try:
        assert tray._play_action.text() == "Play"
        player.play(FIRST)
        mpv.emit("core-idle", False)
        assert tray._play_action.text() == "Stop"
        assert tray.toolTip() == "Первая"
    finally:
        tray.deleteLater()


# MARK: - Иконки


def test_application_icon_is_found():
    """Иконка приложения ставится в hicolor, а из исходников берётся из пакета."""
    assert icons.get(icons.RADIO).isNull() is False


def test_missing_theme_icon_falls_back_to_a_standard_one():
    assert icons.get("совершенно-несуществующая-иконка").isNull() is True
