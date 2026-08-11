#!/usr/bin/env python3
"""Диагностика метаданных потока через libmpv.

Отвечает на вопрос из PLAN.md §7.1: какие поля отдаёт mpv для HLS-потоков Radio Record
и есть ли среди них URL обложки (на macOS он приходил во фрейме WXXX).

    python3 scripts/probe_metadata.py [URL] [--seconds 60] [--audio]

По умолчанию звук не выводится (ao=null) — нужны только метаданные.
"""

from __future__ import annotations

import argparse
import json
import sys
import time

DEFAULT_URL = "https://hls-01-radiorecord.hostingradio.ru/record/playlist.m3u8"


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("url", nargs="?", default=DEFAULT_URL)
    parser.add_argument("--seconds", type=int, default=60, help="сколько слушать поток")
    parser.add_argument("--audio", action="store_true", help="выводить звук в колонки")
    args = parser.parse_args()

    try:
        import mpv
    except ImportError:
        print("Нет модуля mpv. Установите: sudo apt install libmpv2 python3-mpv", file=sys.stderr)
        return 1

    player = mpv.MPV(video=False, ytdl=False)
    if not args.audio:
        player["ao"] = "null"

    started = time.monotonic()
    seen: set[str] = set()

    def stamp() -> str:
        return f"[{time.monotonic() - started:6.1f}s]"

    @player.property_observer("metadata")
    def on_metadata(_name, value):
        if not value:
            return
        blob = json.dumps(value, ensure_ascii=False, sort_keys=True)
        if blob in seen:
            return
        seen.add(blob)
        print(f"\n{stamp()} metadata:")
        for key in sorted(value):
            print(f"    {key!r}: {value[key]!r}")

    @player.property_observer("core-idle")
    def on_core_idle(_name, value):
        print(f"{stamp()} core-idle = {value}")

    @player.property_observer("paused-for-cache")
    def on_cache(_name, value):
        print(f"{stamp()} paused-for-cache = {value}")

    print(f"Слушаем {args.url} в течение {args.seconds} с...\n")
    player.play(args.url)

    try:
        player.wait_for_property("core-idle", lambda v: v is False, timeout=20)
        print(f"{stamp()} воспроизведение началось")
    except Exception as exc:  # noqa: BLE001 — диагностический скрипт, важен сам факт
        print(f"{stamp()} не дождались старта: {exc!r}")

    try:
        time.sleep(args.seconds)
    except KeyboardInterrupt:
        pass
    finally:
        player.terminate()

    print(f"\nВсего различных наборов метаданных: {len(seen)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
