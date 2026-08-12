"""Ограничение кэша обложек.

Радио меняет трек раз в несколько минут, то есть сотни обложек за сутки эфира.
Раньше их никто не удалял, и каталог рос всё время, пока приложением
пользуются.
"""

from __future__ import annotations

import os
from pathlib import Path

import pytest

from radio_player.playback.artwork import prune_cache


@pytest.fixture
def cache(tmp_path):
    directory = tmp_path / "artwork"
    directory.mkdir()
    return directory


def add(directory: Path, name: str, size: int, age_seconds: int) -> Path:
    path = directory / name
    path.write_bytes(b"x" * size)
    # Возраст задаём временем изменения — по нему и выбираются жертвы.
    stamp = 1_700_000_000 - age_seconds
    os.utime(path, (stamp, stamp))
    return path


def test_cache_within_the_limit_is_untouched(cache):
    add(cache, "a.jpg", 100, age_seconds=1000)
    assert prune_cache(cache, max_bytes=1000) == 0
    assert (cache / "a.jpg").exists()


def test_oldest_files_go_first(cache):
    old = add(cache, "old.jpg", 400, age_seconds=9000)
    middle = add(cache, "middle.jpg", 400, age_seconds=5000)
    fresh = add(cache, "fresh.jpg", 400, age_seconds=10)

    assert prune_cache(cache, max_bytes=800) == 1
    assert old.exists() is False
    assert middle.exists() and fresh.exists()


def test_pruning_stops_at_the_limit(cache):
    for index in range(10):
        add(cache, f"{index}.jpg", 100, age_seconds=1000 - index)

    prune_cache(cache, max_bytes=500)
    total = sum(entry.stat().st_size for entry in cache.iterdir())
    assert total <= 500
    # Свежие обложки нужны прямо сейчас: их показывает окно и отдаёт MPRIS.
    assert (cache / "9.jpg").exists()


def test_missing_directory_is_not_an_error(tmp_path):
    assert prune_cache(tmp_path / "never-created", max_bytes=10) == 0
