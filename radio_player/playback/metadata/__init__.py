"""Провайдеры метаданных текущего трека.

Почему их два — см. `docs/architecture.md`. Коротко: libmpv отдаёт ICY для прямых потоков,
но для HLS-потоков hostingradio не отдаёт ничего, и там метаданные приходится
забирать самим из тега `#EXT-X-DATERANGE` в манифесте.
"""

from __future__ import annotations

from typing import Any

from PySide6.QtCore import QObject, Signal

from ...core.models import Station, TrackInfo


class MetadataProvider(QObject):
    """Источник информации о текущем треке.

    Провайдер обязан молчать, если метаданных нет: отсутствие трека — штатная
    ситуация, а не ошибка, и оно не должно влиять на воспроизведение.
    """

    #: Сменился трек.
    track_changed = Signal(TrackInfo)
    #: Метаданных больше нет (сменилась станция, поток остановлен).
    track_cleared = Signal()

    def start(self, station: Station) -> None:
        raise NotImplementedError

    def stop(self) -> None:
        raise NotImplementedError


def make_provider(station: Station, mpv_player: Any, parent: QObject | None = None) -> MetadataProvider:
    """Выбирает провайдера по типу потока."""
    from .hls_daterange import HlsDaterangeProvider
    from .icy import IcyProvider

    if station.is_hls:
        return HlsDaterangeProvider(parent)
    return IcyProvider(mpv_player, parent)


__all__ = ["MetadataProvider", "TrackInfo", "make_provider"]
