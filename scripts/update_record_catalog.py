#!/usr/bin/env python3
"""Обновляет data/catalogs/record.json по списку станций с radiorecord.ru.

Источник — JSON-API сайта, которым пользуется его собственный плеер. Запускается
вручную (`make catalog`), результат идёт в репозиторий: приложение скачивает
каталог из ветки main, так что после пуша изменения получат все пользователи.

id станции — имя потока в пути HLS-адреса (`record-deep`). По id приложение
узнаёт станцию между ревизиями, к нему привязаны правки пользователя, поэтому
правило менять нельзя. Основной канал сложился раньше правила: `record-main`.

Порядок уже известных станций сохраняется, новые дописываются в конец в порядке
сайта, исчезнувшие с сайта (сезонные, например) уходят из каталога. Ревизия
растёт, только если список действительно изменился.
"""

import gzip
import json
import re
import sys
import urllib.request
from pathlib import Path
from urllib.parse import urlparse

API_URL = "https://www.radiorecord.ru/api/stations/"
CATALOG = Path(__file__).resolve().parent.parent / "data" / "catalogs" / "record.json"
NAME_PREFIX = "Radio Record — "
MAIN_SLUG = "record"
MAIN_ID = "record-main"
TIMEOUT_S = 30


def fetch_stations():
    request = urllib.request.Request(API_URL, headers={"Accept-Encoding": "gzip"})
    with urllib.request.urlopen(request, timeout=TIMEOUT_S) as response:
        body = response.read()
    # Сайт сжимает ответ, даже когда об этом не просят.
    if body[:2] == b"\x1f\x8b":
        body = gzip.decompress(body)
    return json.loads(body)["result"]["stations"]


def to_entry(station):
    title = station["title"].strip()
    url = (station.get("stream_hls") or "").strip()
    parsed = urlparse(url)
    slug = parsed.path.split("/")[1] if parsed.path.count("/") >= 2 else ""
    # Без HLS нет ни id по правилу, ни метаданных — решать, что делать, человеку.
    if parsed.scheme != "https" or not re.fullmatch(r"[a-z0-9-]+", slug):
        sys.exit(f"У станции «{title}» неожиданный HLS-адрес: {url!r}. Каталог не тронут.")
    if slug == MAIN_SLUG:
        return {"id": MAIN_ID, "name": NAME_PREFIX + "Main", "url": url}
    # «Record Gold» → «Radio Record — Gold»: имя радио и так стоит в начале.
    title = re.sub(r"^(Record|Рекорд)\s+", "", title)
    return {"id": slug, "name": NAME_PREFIX + title, "url": url}


def main():
    catalog = json.loads(CATALOG.read_text(encoding="utf-8"))
    current = {station["id"]: station for station in catalog["stations"]}

    site = {}
    # sorted() устойчив: станции с одинаковым sort остаются в порядке API.
    for station in sorted(fetch_stations(), key=lambda station: station["sort"]):
        entry = to_entry(station)
        if entry["id"] in site:
            sys.exit(f"id {entry['id']} встречается дважды. Каталог не тронут.")
        site[entry["id"]] = entry

    kept = [site[station_id] for station_id in current if station_id in site]
    added = [entry for station_id, entry in site.items() if station_id not in current]
    removed = [station for station_id, station in current.items() if station_id not in site]
    # Каталог уходит всем пользователям: сбой сайта не должен стереть им станции.
    if len(kept) < len(current) / 2:
        sys.exit(
            f"С сайта пропало {len(removed)} станций из {len(current)} — похоже на сбой. Каталог не тронут."
        )

    for entry in kept:
        before = current[entry["id"]]
        if before["name"] != entry["name"]:
            print(f"~ {before['name']} → {entry['name']}")
        if before["url"] != entry["url"]:
            print(f"~ {entry['name']}: {before['url']} → {entry['url']}")
    for entry in added:
        print(f"+ {entry['name']}")
    for station in removed:
        print(f"- {station['name']}")

    stations = kept + added
    if stations == catalog["stations"]:
        print("Каталог не изменился.")
        return
    catalog["revision"] += 1
    catalog["stations"] = stations
    CATALOG.write_text(json.dumps(catalog, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(f"Станций: {len(stations)}, ревизия {catalog['revision']}.")


if __name__ == "__main__":
    main()
