"""Метаданные HLS-потоков hostingradio (Radio Record) из тега `#EXT-X-DATERANGE`.

ffmpeg этот тег пропускает, поэтому libmpv для таких потоков не отдаёт ничего —
манифест приходится опрашивать самим. Подробности в PLAN.md §2.1.

    #EXT-X-DATERANGE:ID="6a7a2f97…",CLASS="ru.hostingradio.meta",START-DATE="…",END-ON-NEXT=YES,
                     X-ARTIST="BLOND_ISH",X-TITLE="$MONEY",X-METADATA-URI="metadata/6a7a2f97….json"
"""

from __future__ import annotations

import json
import re
from urllib.parse import urljoin

from PySide6.QtCore import QObject, QTimer, QUrl
from PySide6.QtNetwork import QNetworkAccessManager, QNetworkReply, QNetworkRequest

from ..models import Station, TrackInfo
from . import MetadataProvider

#: Как часто опрашивать манифест. Сегменты по ~6 с, чаще смысла нет.
POLL_INTERVAL_MS = 5000
#: После стольких подряд неудачных опросов перезапрашиваем мастер-плейлист:
#: параметр hlssid в ссылке на variant протухает.
_FAILURES_BEFORE_REFRESH = 3
#: Размер обложки. 300×300 хватает и для окна, и для MPRIS.
_COVER_KEYS = ("coverImageUrl300", "coverImageUrl600", "coverImageUrl100")

_DATERANGE_RE = re.compile(r"^#EXT-X-DATERANGE:(.+)$", re.MULTILINE)
_ATTR_RE = re.compile(r'([A-Za-z0-9-]+)="([^"]*)"')
_STREAM_INF_RE = re.compile(r"^#EXT-X-STREAM-INF:.*$\r?\n^(?!#)(\S+)\s*$", re.MULTILINE)


class HlsDaterangeProvider(MetadataProvider):
    def __init__(self, parent: QObject | None = None) -> None:
        super().__init__(parent)
        self._network = QNetworkAccessManager(self)
        self._timer = QTimer(self)
        self._timer.setInterval(POLL_INTERVAL_MS)
        self._timer.timeout.connect(self._poll_variant)

        self._master_url: str | None = None
        self._variant_url: str | None = None
        self._last_range_id: str | None = None
        self._failures = 0
        self._replies: set[QNetworkReply] = set()

    # MARK: - Жизненный цикл

    def start(self, station: Station) -> None:
        self.stop()
        self._master_url = station.url
        self._fetch_master()

    def stop(self) -> None:
        self._timer.stop()
        for reply in list(self._replies):
            reply.abort()
        self._replies.clear()

        self._master_url = None
        self._variant_url = None
        self._failures = 0
        if self._last_range_id is not None:
            self._last_range_id = None
            self.track_cleared.emit()

    # MARK: - Запросы

    def _get(self, url: str, handler) -> None:
        request = QNetworkRequest(QUrl(url))
        request.setAttribute(
            QNetworkRequest.Attribute.CacheLoadControlAttribute,
            QNetworkRequest.CacheLoadControl.AlwaysNetwork,
        )
        request.setTransferTimeout(10_000)
        reply = self._network.get(request)
        self._replies.add(reply)

        def on_finished() -> None:
            self._replies.discard(reply)
            reply.deleteLater()
            if reply.error() == QNetworkReply.NetworkError.OperationCanceledError:
                return
            body = bytes(reply.readAll()).decode("utf-8", errors="replace")
            handler(body if reply.error() == QNetworkReply.NetworkError.NoError else None)

        reply.finished.connect(on_finished)

    def _fetch_master(self) -> None:
        if self._master_url is None:
            return
        self._get(self._master_url, self._handle_master)

    def _handle_master(self, body: str | None) -> None:
        if self._master_url is None:
            return
        if body is None:
            # Сеть или сервер подвели — не мешаем воспроизведению, пробуем позже.
            self._timer.start()
            return

        match = _STREAM_INF_RE.search(body)
        # Ссылка на variant обязательно с параметром hlssid: без него DATERANGE не отдаётся.
        # Если STREAM-INF нет, значит нам сразу дали media playlist.
        self._variant_url = urljoin(self._master_url, match.group(1)) if match else self._master_url
        self._failures = 0
        self._poll_variant()
        self._timer.start()

    def _poll_variant(self) -> None:
        if self._variant_url is None:
            return
        self._get(self._variant_url, self._handle_variant)

    def _handle_variant(self, body: str | None) -> None:
        if self._variant_url is None:
            return
        if body is None:
            self._failures += 1
            if self._failures >= _FAILURES_BEFORE_REFRESH:
                self._failures = 0
                self._timer.stop()
                self._fetch_master()
            return

        self._failures = 0
        attrs = _parse_daterange(body)
        if attrs is None:
            # Станция не из hostingradio либо метаданных сейчас нет — это нормально.
            return

        range_id = attrs.get("ID")
        if not range_id or range_id == self._last_range_id:
            return
        self._last_range_id = range_id

        title = (attrs.get("X-TITLE") or "").strip()
        if not title:
            return
        artist = (attrs.get("X-ARTIST") or "").strip() or None
        # Название отдаём сразу, обложку — когда (и если) догрузится.
        self.track_changed.emit(TrackInfo(title=title, artist=artist))

        metadata_uri = attrs.get("X-METADATA-URI")
        if metadata_uri:
            self._fetch_artwork(urljoin(self._variant_url, metadata_uri), range_id, title, artist)

    def _fetch_artwork(self, url: str, range_id: str, title: str, artist: str | None) -> None:
        def handle(body: str | None) -> None:
            if body is None or range_id != self._last_range_id:
                return  # Трек уже сменился — обложка неактуальна.
            cover = _extract_cover(body)
            if cover:
                self.track_changed.emit(TrackInfo(title=title, artist=artist, artwork_url=cover))

        self._get(url, handle)


def _parse_daterange(playlist: str) -> dict[str, str] | None:
    """Возвращает атрибуты первого DATERANGE с метаданными трека."""
    for match in _DATERANGE_RE.finditer(playlist):
        attrs = dict(_ATTR_RE.findall(match.group(1)))
        if "X-TITLE" in attrs or "X-ARTIST" in attrs:
            return attrs
    return None


def _extract_cover(body: str) -> str | None:
    """Достаёт URL обложки текущего трека. У части станций её нет — это штатно."""
    try:
        current = json.loads(body).get("current") or {}
    except (json.JSONDecodeError, AttributeError):
        return None
    for key in _COVER_KEYS:
        url = current.get(key)
        if url:
            return url
    return None
