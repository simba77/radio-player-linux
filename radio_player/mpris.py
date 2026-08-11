"""MPRIS2 — управление плеером извне: медиаклавиши, виджет «Медиаплеер» в Plasma.

Аналог `MPRemoteCommandCenter` + `MPNowPlayingInfoCenter` из macOS-версии.
Спецификация: https://specifications.freedesktop.org/mpris-spec/latest/

Живое радио не перематывается и не имеет длительности, поэтому `CanSeek` = false,
`Position` всегда 0, а `mpris:length` не публикуется вовсе.
"""

from __future__ import annotations

from collections.abc import Callable
from pathlib import Path

from PySide6.QtCore import ClassInfo, Property, QMetaType, QObject, Slot
from PySide6.QtDBus import (
    QDBusAbstractAdaptor,
    QDBusArgument,
    QDBusConnection,
    QDBusMessage,
    QDBusObjectPath,
)

from . import APP_NAME, APP_RDNS_ID
from .player import PlayerManager

#: Спецификация просит, чтобы имя на шине совпадало с именем `.desktop`-файла.
BUS_NAME = f"org.mpris.MediaPlayer2.{APP_RDNS_ID}"
OBJECT_PATH = "/org/mpris/MediaPlayer2"

_ROOT_INTERFACE = "org.mpris.MediaPlayer2"
_PLAYER_INTERFACE = "org.mpris.MediaPlayer2.Player"
_PROPERTIES_INTERFACE = "org.freedesktop.DBus.Properties"

#: Путь трека обязан быть валидным objectpath; конкретный трек нас не интересует.
_TRACK_ID = "/org/mpris/MediaPlayer2/CurrentTrack"


@ClassInfo({"D-Bus Interface": _ROOT_INTERFACE})
class _RootAdaptor(QDBusAbstractAdaptor):
    """Интерфейс `org.mpris.MediaPlayer2`: сведения о приложении."""

    def __init__(self, parent: QObject, on_raise: Callable[[], None], on_quit: Callable[[], None]) -> None:
        super().__init__(parent)
        self._on_raise = on_raise
        self._on_quit = on_quit

    @Slot()
    def Raise(self) -> None:  # noqa: N802 — имя из спецификации D-Bus
        self._on_raise()

    @Slot()
    def Quit(self) -> None:  # noqa: N802
        self._on_quit()

    @Property(bool)
    def CanQuit(self) -> bool:  # noqa: N802
        return True

    @Property(bool)
    def CanRaise(self) -> bool:  # noqa: N802
        return True

    @Property(bool)
    def HasTrackList(self) -> bool:  # noqa: N802
        return False

    @Property(str)
    def Identity(self) -> str:  # noqa: N802
        return APP_NAME

    @Property(str)
    def DesktopEntry(self) -> str:  # noqa: N802
        """Имя `.desktop`-файла без расширения: по нему оболочка находит
        иконку и название приложения для апплета «Медиаплеер»."""
        return APP_RDNS_ID

    @Property("QStringList")
    def SupportedUriSchemes(self) -> list[str]:  # noqa: N802
        return ["http", "https"]

    @Property("QStringList")
    def SupportedMimeTypes(self) -> list[str]:  # noqa: N802
        return ["audio/mpeg", "audio/aac", "application/vnd.apple.mpegurl"]


@ClassInfo({"D-Bus Interface": _PLAYER_INTERFACE})
class _PlayerAdaptor(QDBusAbstractAdaptor):
    """Интерфейс `org.mpris.MediaPlayer2.Player`: воспроизведение и метаданные."""

    def __init__(self, parent: QObject, player: PlayerManager) -> None:
        super().__init__(parent)
        self._player = player

    # MARK: - Методы

    @Slot()
    def Play(self) -> None:  # noqa: N802
        if not self._player.is_playing:
            self._player.toggle()

    @Slot()
    def Pause(self) -> None:  # noqa: N802
        # Живой поток не ставится на паузу — останавливаем.
        self._player.stop()

    @Slot()
    def PlayPause(self) -> None:  # noqa: N802
        self._player.toggle()

    @Slot()
    def Stop(self) -> None:  # noqa: N802
        self._player.stop()

    @Slot()
    def Next(self) -> None:  # noqa: N802
        self._player.play_next()

    @Slot()
    def Previous(self) -> None:  # noqa: N802
        self._player.play_previous()

    @Slot(float)
    def Seek(self, _offset: float) -> None:  # noqa: N802
        pass  # Живое радио не перематывается.

    @Slot(QDBusObjectPath, float)
    def SetPosition(self, _track: QDBusObjectPath, _position: float) -> None:  # noqa: N802
        pass

    @Slot(str)
    def OpenUri(self, _uri: str) -> None:  # noqa: N802
        pass

    # MARK: - Свойства

    @Property(str)
    def PlaybackStatus(self) -> str:  # noqa: N802
        return "Playing" if self._player.is_playing else "Stopped"

    @Property("QVariantMap")
    def Metadata(self) -> dict:  # noqa: N802
        return _metadata(self._player)

    def _get_volume(self) -> float:
        return self._player.volume / 100

    def _set_volume(self, value: float) -> None:
        self._player.set_volume(round(max(0.0, min(1.0, value)) * 100))

    Volume = Property(float, _get_volume, _set_volume)

    @Property("qlonglong")
    def Position(self) -> int:  # noqa: N802
        return 0

    @Property(float)
    def Rate(self) -> float:  # noqa: N802
        return 1.0

    @Property(float)
    def MinimumRate(self) -> float:  # noqa: N802
        return 1.0

    @Property(float)
    def MaximumRate(self) -> float:  # noqa: N802
        return 1.0

    @Property(bool)
    def CanGoNext(self) -> bool:  # noqa: N802
        return len(self._player.stations) > 1

    @Property(bool)
    def CanGoPrevious(self) -> bool:  # noqa: N802
        return len(self._player.stations) > 1

    @Property(bool)
    def CanPlay(self) -> bool:  # noqa: N802
        return bool(self._player.stations)

    @Property(bool)
    def CanPause(self) -> bool:  # noqa: N802
        return True

    @Property(bool)
    def CanSeek(self) -> bool:  # noqa: N802
        return False

    @Property(bool)
    def CanControl(self) -> bool:  # noqa: N802
        return True


class MprisServer(QObject):
    """Регистрирует плеер на шине сеанса и рассылает изменения свойств."""

    def __init__(
        self,
        player: PlayerManager,
        on_raise: Callable[[], None],
        on_quit: Callable[[], None],
        parent: QObject | None = None,
    ) -> None:
        super().__init__(parent)
        self._player = player
        self._connection = QDBusConnection.sessionBus()
        self._registered = False

        # Адаптеры должны жить столько же, сколько объект: без ссылок их соберёт GC.
        self._root_adaptor = _RootAdaptor(self, on_raise, on_quit)
        self._player_adaptor = _PlayerAdaptor(self, player)

        player.state_changed.connect(self._emit_playback_status)
        player.track_changed.connect(lambda _track: self._emit_metadata())
        player.station_changed.connect(lambda _station: self._emit_metadata())
        player.artwork_changed.connect(lambda _image: self._emit_metadata())

    def register(self) -> bool:
        """Занимает имя на шине. False — если шины нет или имя уже занято."""
        if not self._connection.isConnected():
            return False
        if not self._connection.registerObject(
            OBJECT_PATH, self, QDBusConnection.RegisterOption.ExportAdaptors
        ):
            return False
        if not self._connection.registerService(BUS_NAME):
            self._connection.unregisterObject(OBJECT_PATH)
            return False
        self._registered = True
        return True

    def unregister(self) -> None:
        if not self._registered:
            return
        self._connection.unregisterService(BUS_NAME)
        self._connection.unregisterObject(OBJECT_PATH)
        self._registered = False

    # MARK: - Уведомления

    def _emit_playback_status(self) -> None:
        self._emit_properties({
            "PlaybackStatus": self._player_adaptor.PlaybackStatus,
            "CanGoNext": self._player_adaptor.CanGoNext,
            "CanGoPrevious": self._player_adaptor.CanGoPrevious,
            "CanPlay": self._player_adaptor.CanPlay,
        })

    def _emit_metadata(self) -> None:
        self._emit_properties({"Metadata": _metadata(self._player)})

    def _emit_properties(self, changed: dict) -> None:
        if not self._registered:
            return
        message = QDBusMessage.createSignal(OBJECT_PATH, _PROPERTIES_INTERFACE, "PropertiesChanged")
        message.setArguments([_PLAYER_INTERFACE, changed, _empty_string_list()])
        self._connection.send(message)


def _empty_string_list() -> QDBusArgument:
    """Пустой массив строк (`as`) для поля invalidated_properties.

    Обычный пустой список Python Qt отправляет как массив вариантов, и сигнатура
    сигнала становится `sa{sv}av` вместо `sa{sv}as`. Такой сигнал корректные
    Qt-клиенты — plasmashell и kded — молча игнорируют: для Plasma плеер тогда
    никогда не «начинает играть», не попадает в апплет «Медиаплеер»
    и не получает медиаклавиши.
    """
    argument = QDBusArgument()
    argument.beginArray(QMetaType(QMetaType.Type.QString.value))
    argument.endArray()
    return argument


def _metadata(player: PlayerManager) -> dict:
    """Метаданные текущего трека в формате MPRIS."""
    metadata: dict = {"mpris:trackid": QDBusObjectPath(_TRACK_ID)}

    station = player.current_station
    track = player.current_track
    if track is not None:
        metadata["xesam:title"] = track.title
        metadata["xesam:artist"] = [track.artist] if track.artist else []
        if station is not None:
            metadata["xesam:album"] = station.name
    elif station is not None:
        metadata["xesam:title"] = station.name

    if player.artwork_path:
        metadata["mpris:artUrl"] = Path(player.artwork_path).as_uri()
    return metadata
