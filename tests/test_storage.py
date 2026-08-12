"""Чтение и запись пользовательских файлов.

Главный тест здесь — про независимых писателей `state.json`. Ошибка не видна
глазами: файл остаётся валидным, приложение работает, просто чужой ключ тихо
исчезает, и каталоги начинают обновляться при каждом запуске.
"""

from __future__ import annotations

import json

from radio_player.core import storage


def test_load_state_returns_empty_dict_without_file():
    assert storage.load_state() == {}


def test_load_state_survives_broken_file():
    path = storage.state_path()
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("{ это не json", encoding="utf-8")
    assert storage.load_state() == {}


def test_update_state_creates_the_file():
    assert storage.update_state(volume=70) is True
    assert storage.load_state() == {"volume": 70}


def test_update_state_keeps_untouched_keys():
    storage.update_state(volume=70)
    storage.update_state(last_station_id="record-main")
    assert storage.load_state() == {"volume": 70, "last_station_id": "record-main"}


def test_independent_writers_do_not_clobber_each_other():
    """Плеер и обновление каталогов пишут в файл, ничего не зная друг о друге.

    Плеер раньше держал копию состояния с момента запуска и сохранял её
    целиком, стирая метку обновления каталогов, поставленную позже.
    """
    # Плеер запомнил громкость при старте.
    storage.update_state(volume=70)
    # Обновление каталогов отметилось спустя какое-то время.
    storage.update_state(catalogs_updated_at=1234567890.0)
    # Пользователь двигает слайдер громкости.
    storage.update_state(volume=71)

    assert storage.load_state() == {"volume": 71, "catalogs_updated_at": 1234567890.0}


def test_write_json_is_atomic_and_leaves_no_temporary_file(tmp_path):
    path = tmp_path / "data.json"
    assert storage.write_json(path, {"a": 1}) is True
    assert json.loads(path.read_text(encoding="utf-8")) == {"a": 1}
    assert list(tmp_path.iterdir()) == [path]


def test_write_json_reports_failure(tmp_path):
    # Внутрь файла записать нельзя — родитель не каталог.
    blocker = tmp_path / "blocker"
    blocker.write_text("", encoding="utf-8")
    assert storage.write_json(blocker / "data.json", {"a": 1}) is False
