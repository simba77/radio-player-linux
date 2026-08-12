"""Двойники для тестов: libmpv и источник станций.

Живут отдельно, потому что нужны и тестам плеера, и тестам интерфейса —
импортировать один тестовый модуль из другого было бы хуже.
"""

from __future__ import annotations

from radio_player.core.models import Station


class FakeMpv:
    """Двойник libmpv: запоминает вызовы и умеет отдавать события."""

    def __init__(self) -> None:
        self.properties: dict[str, object] = {}
        self.observers: dict[str, list] = {}
        self.event_handlers: dict[str, object] = {}
        self.played: list[str] = []
        self.commands: list[tuple] = []
        self.terminated = False
        self.fail_on_play = False

    # MARK: - Интерфейс, которым пользуется PlayerManager

    def __setitem__(self, name: str, value: object) -> None:
        self.properties[name] = value

    def observe_property(self, name: str, callback) -> None:
        self.observers.setdefault(name, []).append(callback)

    def unobserve_property(self, name: str, callback) -> None:
        self.observers.get(name, []).remove(callback)

    def event_callback(self, name: str):
        def register(handler):
            self.event_handlers[name] = handler
            return handler

        return register

    def play(self, url: str) -> None:
        if self.fail_on_play:
            raise OSError("поток не открылся")
        self.played.append(url)

    def command(self, *args) -> None:
        self.commands.append(args)

    def terminate(self) -> None:
        self.terminated = True

    # MARK: - Помощники теста

    def emit(self, name: str, value) -> None:
        for callback in list(self.observers.get(name, [])):
            callback(name, value)

    def end_file(self, reason: int) -> None:
        event = type("Event", (), {"data": type("Data", (), {"reason": reason})()})()
        self.event_handlers["end-file"](event)


class FakeStore:
    def __init__(self, stations: list[Station]) -> None:
        self._stations = stations

    @property
    def stations(self) -> list[Station]:
        return list(self._stations)
