"""Импорт и экспорт списков станций (PLAN.md §3.5).

Поддерживаются три формата на входе: JSON macOS-версии, `.m3u`/`.m3u8` и `.pls`.
Все импортированные станции становятся своими (`local:`), даже если в исходнике
были идентификаторы: чужие id не должны притворяться станциями каталога.
"""

from __future__ import annotations

import json
import re
from pathlib import Path

from .models import Station

#: `#EXTINF:<секунды>,<название>` — название может содержать запятые.
_EXTINF_RE = re.compile(r"^#EXTINF:[^,]*,(.*)$", re.IGNORECASE)
#: Строки `.pls`: `File1=...`, `Title1=...`.
_PLS_ENTRY_RE = re.compile(r"^(File|Title)(\d+)\s*=\s*(.*)$", re.IGNORECASE)


def parse(text: str, *, filename: str = "") -> list[Station]:
    """Разбирает список станций, определяя формат по содержимому и имени файла."""
    stripped = text.lstrip()
    # `.pls` тоже начинается с квадратной скобки, поэтому проверяем его первым.
    if stripped.lower().startswith("[playlist]") or filename.lower().endswith(".pls"):
        return parse_pls(text)
    if stripped.startswith(("[", "{")):
        return parse_json(text)
    return parse_m3u(text)


def parse_json(text: str) -> list[Station]:
    """JSON macOS-версии: массив `{id, name, urlString}`."""
    try:
        data = json.loads(text)
    except json.JSONDecodeError:
        return []
    if isinstance(data, dict):
        data = data.get("stations") or []
    if not isinstance(data, list):
        return []

    stations = []
    for raw in data:
        station = Station.from_dict(raw)
        if station is not None:
            # Идентификаторы из чужого файла не переносим — станция становится своей.
            stations.append(Station(name=station.name, url=station.url))
    return stations


def parse_m3u(text: str) -> list[Station]:
    stations: list[Station] = []
    pending_name: str | None = None

    for line in text.splitlines():
        line = line.strip()
        if not line:
            continue
        if line.startswith("#"):
            match = _EXTINF_RE.match(line)
            if match:
                pending_name = match.group(1).strip() or None
            continue
        if not _looks_like_stream(line):
            continue
        stations.append(Station(name=pending_name or _name_from_url(line), url=line))
        pending_name = None

    return stations


def parse_pls(text: str) -> list[Station]:
    urls: dict[int, str] = {}
    titles: dict[int, str] = {}

    for line in text.splitlines():
        match = _PLS_ENTRY_RE.match(line.strip())
        if match is None:
            continue
        key, index, value = match.group(1).lower(), int(match.group(2)), match.group(3).strip()
        if key == "file":
            urls[index] = value
        else:
            titles[index] = value

    stations = []
    for index in sorted(urls):
        url = urls[index]
        if not _looks_like_stream(url):
            continue
        stations.append(Station(name=titles.get(index) or _name_from_url(url), url=url))
    return stations


def export_m3u(stations: list[Station]) -> str:
    lines = ["#EXTM3U"]
    for station in stations:
        lines.append(f"#EXTINF:-1,{station.name}")
        lines.append(station.url)
    return "\n".join(lines) + "\n"


def read_file(path: Path) -> list[Station]:
    try:
        text = path.read_text(encoding="utf-8", errors="replace")
    except OSError:
        return []
    return parse(text, filename=path.name)


def _looks_like_stream(value: str) -> bool:
    return value.startswith(("http://", "https://"))


def _name_from_url(url: str) -> str:
    """Запасное имя, когда в файле его не было."""
    host = url.split("//", 1)[-1].split("/", 1)[0]
    return host or url
