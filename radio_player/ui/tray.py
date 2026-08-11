"""Иконка в системном лотке.

На KDE Wayland QSystemTrayIcon уходит через StatusNotifierItem. В GNOME без расширения
AppIndicator лотка нет вообще — приложение должно оставаться пригодным и без него,
поэтому трей здесь только дополняет окно, но ничем не владеет.
"""

from __future__ import annotations

from collections.abc import Callable

from PySide6.QtGui import QIcon
from PySide6.QtWidgets import QMenu, QSystemTrayIcon

from .. import APP_NAME
from ..player import PlayerManager

#: Иконки из системной темы. Свои symbolic-иконки появятся в M3.
_ICON_PLAYING = "media-playback-start"
_ICON_STOPPED = "media-playback-stop"


class TrayIcon(QSystemTrayIcon):
    def __init__(
        self,
        player: PlayerManager,
        on_toggle_window: Callable[[], None],
        on_settings: Callable[[], None],
        on_quit: Callable[[], None],
        parent=None,
    ) -> None:
        super().__init__(parent)
        self._player = player
        self._on_toggle_window = on_toggle_window

        self.setToolTip(APP_NAME)

        menu = QMenu()
        self._play_action = menu.addAction(self.tr("Play"), player.toggle)
        menu.addAction(self.tr("Next station"), player.play_next)
        menu.addAction(self.tr("Previous station"), player.play_previous)
        menu.addSeparator()
        menu.addAction(self.tr("Show window"), on_toggle_window)
        menu.addAction(self.tr("Settings..."), on_settings)
        menu.addSeparator()
        menu.addAction(self.tr("Quit"), on_quit)
        self.setContextMenu(menu)

        self.activated.connect(self._on_activated)
        player.state_changed.connect(self._refresh)
        player.track_changed.connect(lambda _track: self._refresh())
        player.blink_toggled.connect(self._on_blink)
        self._refresh()

    def _on_activated(self, reason: QSystemTrayIcon.ActivationReason) -> None:
        if reason == QSystemTrayIcon.ActivationReason.Trigger:
            self._on_toggle_window()

    def _refresh(self) -> None:
        playing = self._player.is_playing and not self._player.is_reconnecting
        self._set_icon(playing)
        self._play_action.setText(self.tr("Stop") if self._player.is_playing else self.tr("Play"))
        self.setToolTip(self._tooltip())

    def _on_blink(self, on: bool) -> None:
        """Во время переподключения иконка мигает — как в macOS-версии."""
        self._set_icon(on)

    def _set_icon(self, active: bool) -> None:
        self.setIcon(QIcon.fromTheme(_ICON_PLAYING if active else _ICON_STOPPED))

    def _tooltip(self) -> str:
        player = self._player
        if player.is_reconnecting:
            return self.tr("{app} — reconnecting...").format(app=APP_NAME)
        if player.current_track is not None:
            return player.current_track.display
        if player.current_station is not None and player.is_playing:
            return player.current_station.name
        return APP_NAME
