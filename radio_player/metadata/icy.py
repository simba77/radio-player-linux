"""ICY-метаданные прямых Icecast/Shoutcast-потоков.

libmpv отдаёт их в свойстве `metadata` ключом `icy-title`:

    {'icy-title': 'TIESTO/BIA/21 SAVAGE - Both (Record Mix)', 'icy-url': ..., 'icy-pub': '1'}
"""

from __future__ import annotations

import contextlib

from PySide6.QtCore import QObject, Signal, Slot

from ..models import Station, split_icy_title
from . import MetadataProvider


class IcyProvider(MetadataProvider):
    """Читает `metadata` из libmpv.

    Коллбеки mpv приходят из его собственного потока, поэтому они лишь испускают
    `_raw_metadata`: Qt доставит сигнал в поток объекта очередью, и вся обработка
    пойдёт уже в основном потоке.
    """

    _raw_metadata = Signal(object)

    def __init__(self, mpv_player, parent: QObject | None = None) -> None:
        super().__init__(parent)
        self._mpv = mpv_player
        self._observer = None
        self._last_raw: str | None = None
        self._raw_metadata.connect(self._handle_metadata)

    def start(self, station: Station) -> None:  # noqa: ARG002 — источник тот же, что и поток
        self.stop()
        self._last_raw = None

        def observer(_name, value):
            self._raw_metadata.emit(value)

        self._observer = observer
        self._mpv.observe_property("metadata", observer)

    def stop(self) -> None:
        if self._observer is not None:
            # mpv мог уже завершиться — тогда наблюдатель снят вместе с ним.
            with contextlib.suppress(OSError, ValueError):
                self._mpv.unobserve_property("metadata", self._observer)
            self._observer = None
        if self._last_raw is not None:
            self._last_raw = None
            self.track_cleared.emit()

    @Slot(object)
    def _handle_metadata(self, value) -> None:
        if self._observer is None or not isinstance(value, dict):
            return
        raw = value.get("icy-title") or value.get("title")
        if not raw or raw == self._last_raw:
            return
        track = split_icy_title(raw)
        if track is None:
            return
        self._last_raw = raw
        self.track_changed.emit(track)
