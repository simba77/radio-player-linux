"""Загрузка каталогов станций: встроенная копия → кэш на диске → сеть.

Приложение обязано работать сразу и без сети, поэтому встроенная копия каталога
всегда есть в пакете, а сетевое обновление лишь уточняет её. Ошибка загрузки
никогда не приводит к пустому списку станций.
"""

from __future__ import annotations

import contextlib
import hashlib
import json
import time
from collections.abc import Sequence
from importlib import resources
from pathlib import Path

from PySide6.QtCore import QObject, QUrl, Signal
from PySide6.QtNetwork import QNetworkAccessManager, QNetworkReply, QNetworkRequest

from . import paths, storage
from .models import Catalog

#: Каталоги, встроенные в пакет. Порядок важен: при коллизии id побеждает первый.
BUNDLED_CATALOGS = ("record.json",)

#: Откуда обновлять каталоги. Пути указывают на файлы в репозитории проекта.
#: TODO: подтвердить имя репозитория, когда он появится на GitHub.
DEFAULT_CATALOG_URLS = (
    "https://raw.githubusercontent.com/simba77/radio-player-linux/main/"
    "radio_player/resources/catalogs/record.json",
)

#: Не тянуть каталоги чаще, чем раз в сутки.
UPDATE_INTERVAL_SECONDS = 24 * 60 * 60

#: Служебный файл в каталожном кэше — не каталог.
_ETAGS_FILE = "etags.json"


def load_bundled() -> list[Catalog]:
    """Каталоги из пакета. Если и они битые — это баг сборки, но не падение."""
    catalogs = []
    for name in BUNDLED_CATALOGS:
        try:
            raw = resources.files("radio_player.resources.catalogs").joinpath(name).read_text("utf-8")
            catalog = Catalog.from_dict(json.loads(raw))
        except (OSError, json.JSONDecodeError, ModuleNotFoundError):
            continue
        if catalog is not None:
            catalogs.append(catalog)
    return catalogs


def load_cached(urls: Sequence[str]) -> list[Catalog]:
    """Ранее скачанные каталоги — только для перечисленных источников.

    Читаем строго по списку, а не всё, что лежит в кэше: иначе каталог,
    удалённый пользователем из настроек, продолжал бы поставлять станции
    из своего файла, и убрать их было бы нечем. Порядок — как в `urls`,
    от него зависит, кто побеждает при совпадении id станций.
    """
    catalogs = []
    for url in urls:
        catalog = Catalog.from_dict(storage.read_json(_cache_path(url)))
        if catalog is not None:
            catalogs.append(catalog)
    return catalogs


def load_initial(urls: Sequence[str] = DEFAULT_CATALOG_URLS) -> list[Catalog]:
    """Каталоги для старта: кэш, если он есть, иначе встроенные копии.

    Кэш заведомо не старше встроенной копии — он появляется только после
    успешной загрузки из сети.
    """
    prune_cache(urls)
    cached = load_cached(urls)
    if not cached:
        return load_bundled()

    bundled = {catalog.id: catalog for catalog in load_bundled()}
    merged = []
    for catalog in cached:
        fresher = bundled.pop(catalog.id, None)
        # Встроенная копия могла обогнать кэш после обновления приложения.
        merged.append(fresher if fresher and fresher.revision > catalog.revision else catalog)
    merged.extend(bundled.values())
    return merged


class CatalogUpdater(QObject):
    """Фоновое обновление каталогов по HTTP с валидацией ETag.

    Список источников — встроенные каталоги плюс добавленные пользователем.
    Дополнительные держим отдельно и меняем через `set_extra_urls`: список
    правится в настройках уже после запуска, а копия, снятая при создании,
    об этих правках не узнала бы.
    """

    #: Загрузка завершилась: список каталогов и признак, что что-то изменилось.
    finished = Signal(list, bool)

    def __init__(self, extra_urls: Sequence[str] = (), parent: QObject | None = None) -> None:
        super().__init__(parent)
        self._extra_urls = tuple(extra_urls)
        self._network = QNetworkAccessManager(self)
        self._replies: set[QNetworkReply] = set()
        self._pending = 0
        self._changed = False

    @property
    def urls(self) -> tuple[str, ...]:
        """Все источники: встроенные, затем пользовательские."""
        return (*DEFAULT_CATALOG_URLS, *self._extra_urls)

    def set_extra_urls(self, urls: Sequence[str]) -> None:
        """Заменяет список пользовательских каталогов.

        Уже отправленные запросы не отменяет: текущий цикл дойдёт до конца
        со старым списком, а новый начнётся с обновлённым.
        """
        self._extra_urls = tuple(urls)

    @property
    def is_running(self) -> bool:
        return self._pending > 0

    def update_now(self) -> None:
        urls = self.urls
        if self.is_running or not urls:
            return
        self._pending = len(urls)
        self._changed = False
        for url in urls:
            self._fetch(url)

    def cancel(self) -> None:
        for reply in list(self._replies):
            reply.abort()
        self._replies.clear()
        self._pending = 0

    def _fetch(self, url: str) -> None:
        request = QNetworkRequest(QUrl(url))
        request.setTransferTimeout(20_000)
        etag = _etags().get(url)
        if etag:
            request.setRawHeader(b"If-None-Match", etag.encode())
        reply = self._network.get(request)
        self._replies.add(reply)
        reply.finished.connect(lambda: self._handle_reply(url, reply))

    def _handle_reply(self, url: str, reply: QNetworkReply) -> None:
        self._replies.discard(reply)
        reply.deleteLater()
        try:
            if reply.error() == QNetworkReply.NetworkError.NoError:
                self._store(url, reply)
        finally:
            self._pending -= 1
            if self._pending <= 0:
                self._pending = 0
                self._finish()

    def _store(self, url: str, reply: QNetworkReply) -> None:
        status = reply.attribute(QNetworkRequest.Attribute.HttpStatusCodeAttribute)
        if status == 304:
            return  # Каталог не менялся.

        try:
            data = json.loads(bytes(reply.readAll().data()).decode("utf-8"))
        except (UnicodeDecodeError, json.JSONDecodeError):
            return
        catalog = Catalog.from_dict(data)
        if catalog is None:
            # Битый или слишком новый каталог: остаёмся на прежней копии.
            return

        if not storage.write_json(_cache_path(url), data):
            return
        self._changed = True

        etag = bytes(reply.rawHeader(b"ETag").data()).decode(errors="ignore")
        etags = _etags()
        if etag:
            etags[url] = etag
        else:
            etags.pop(url, None)
        storage.write_json(_etags_path(), etags)

    def _finish(self) -> None:
        storage.update_state(catalogs_updated_at=_now())
        self.finished.emit(load_initial(self.urls), self._changed)


def seconds_since_update() -> float | None:
    """Сколько прошло с последнего успешного обновления, None — если не обновляли."""
    updated_at = storage.load_state().get("catalogs_updated_at")
    if not isinstance(updated_at, int | float):
        return None
    return max(0.0, _now() - updated_at)


def prune_cache(urls: Sequence[str]) -> None:
    """Удаляет из кэша файлы источников, которых больше нет в списке.

    Кэш — производная от списка каталогов, а не самостоятельное хранилище:
    осиротевший файл иначе лежал бы вечно, занимая место и сбивая с толку.
    """
    keep = {_cache_path(url).name for url in urls}
    keep.add(_ETAGS_FILE)
    for path in _cache_dir().glob("*.json"):
        if path.name not in keep:
            with contextlib.suppress(OSError):
                path.unlink()

    etags = _etags()
    stale = set(etags) - set(urls)
    if stale:
        for url in stale:
            del etags[url]
        storage.write_json(_etags_path(), etags)


def _now() -> float:
    return time.time()


def _cache_dir() -> Path:
    return paths.cache_dir() / "catalogs"


def _cache_path(url: str) -> Path:
    """Файл кэша именуется по адресу источника, а не по id каталога:
    имя должно однозначно отвечать, из какого источника взят файл."""
    digest = hashlib.blake2b(url.encode(), digest_size=16).hexdigest()
    return _cache_dir() / f"{digest}.json"


def _etags_path() -> Path:
    return _cache_dir() / _ETAGS_FILE


def _etags() -> dict[str, str]:
    data = storage.read_json(_etags_path(), default={})
    return data if isinstance(data, dict) else {}
