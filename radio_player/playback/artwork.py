"""Загрузка и кэширование обложек.

Файловый кэш нужен не только ради экономии запросов: MPRIS отдаёт обложку
через `mpris:artUrl`, то есть путём к локальному файлу.
"""

from __future__ import annotations

import hashlib
from pathlib import Path

from PySide6.QtCore import QObject, QUrl, Signal
from PySide6.QtGui import QImage
from PySide6.QtNetwork import QNetworkAccessManager, QNetworkReply, QNetworkRequest

from ..core import paths

#: Обложки крупнее не нужны: 300×300 — максимум, что просит UI.
_MAX_BYTES = 2 * 1024 * 1024


class ArtworkLoader(QObject):
    """Отдаёт обложку по URL: сначала из кэша на диске, иначе из сети."""

    #: url, путь к файлу, изображение
    artwork_ready = Signal(str, str, QImage)

    def __init__(self, parent: QObject | None = None) -> None:
        super().__init__(parent)
        self._network = QNetworkAccessManager(self)
        self._dir = paths.cache_dir() / "artwork"
        self._pending: dict[str, QNetworkReply] = {}

    def load(self, url: str) -> None:
        if not url or url in self._pending:
            return

        cached = self._cache_path(url)
        if cached.exists():
            image = QImage(str(cached))
            if not image.isNull():
                self.artwork_ready.emit(url, str(cached), image)
                return
            cached.unlink(missing_ok=True)

        request = QNetworkRequest(QUrl(url))
        request.setTransferTimeout(15_000)
        reply = self._network.get(request)
        self._pending[url] = reply
        reply.finished.connect(lambda: self._handle_reply(url, reply))

    def cancel_all(self) -> None:
        for reply in list(self._pending.values()):
            reply.abort()
        self._pending.clear()

    def _handle_reply(self, url: str, reply: QNetworkReply) -> None:
        self._pending.pop(url, None)
        reply.deleteLater()
        if reply.error() != QNetworkReply.NetworkError.NoError:
            return

        data = reply.readAll().data()
        if not data or len(data) > _MAX_BYTES:
            return
        image = QImage()
        if not image.loadFromData(data):
            return

        path = self._cache_path(url)
        try:
            path.parent.mkdir(parents=True, exist_ok=True)
            # Пишем через временный файл: недокачанная обложка не должна попасть в кэш.
            tmp = path.with_suffix(path.suffix + ".part")
            tmp.write_bytes(data)
            tmp.replace(path)
        except OSError:
            # Кэш не критичен — отдаём картинку из памяти.
            self.artwork_ready.emit(url, "", image)
            return

        self.artwork_ready.emit(url, str(path), image)

    def _cache_path(self, url: str) -> Path:
        # Хеш здесь — способ превратить адрес в имя файла, а не защита:
        # подделывать его некому, коллизия стоит одной перерисованной обложки.
        digest = hashlib.sha1(url.encode(), usedforsecurity=False).hexdigest()
        suffix = ".webp" if url.split("?", 1)[0].endswith(".webp") else ".jpg"
        return self._dir / f"{digest}{suffix}"
