"""Чтение и запись пользовательских JSON-файлов.

Все пути берутся из `paths`. В M2 сюда добавятся схемы и миграции для
`settings.json` и `overlay.json`; пока хватает атомарного чтения/записи.
"""

from __future__ import annotations

import contextlib
import json
import os
from pathlib import Path
from typing import Any

from . import paths


def read_json(path: Path, default: Any = None) -> Any:
    """Возвращает содержимое файла или `default`, если файла нет или он повреждён."""
    try:
        with path.open(encoding="utf-8") as handle:
            return json.load(handle)
    except (OSError, json.JSONDecodeError):
        return default


def write_json(path: Path, data: Any) -> bool:
    """Атомарно записывает JSON. Возвращает False, если записать не удалось."""
    tmp = path.with_suffix(path.suffix + ".tmp")
    try:
        path.parent.mkdir(parents=True, exist_ok=True)
        with tmp.open("w", encoding="utf-8") as handle:
            json.dump(data, handle, ensure_ascii=False, indent=2)
            handle.flush()
            os.fsync(handle.fileno())
        tmp.replace(path)
        return True
    except OSError:
        # Уборка сама может не пройти (каталога нет, прав нет) — но обещание
        # функции «вернуть False, а не бросить» важнее удалённого хвоста.
        with contextlib.suppress(OSError):
            tmp.unlink(missing_ok=True)
        return False


def state_path() -> Path:
    return paths.config_dir() / "state.json"


def load_state() -> dict[str, Any]:
    state = read_json(state_path(), default={})
    return state if isinstance(state, dict) else {}


def update_state(**values: Any) -> bool:
    """Меняет только переданные ключи, не трогая остальные.

    У `state.json` несколько независимых писателей: плеер хранит громкость
    и последнюю станцию, обновление каталогов — время последней проверки.
    Записывать файл целиком нельзя: писатель сохранил бы свою копию, снятую
    при запуске, и затёр бы чужие ключи, изменившиеся с тех пор. Поэтому
    состояние перечитывается перед каждой записью, а функции «сохранить всё»
    здесь нет намеренно.
    """
    state = load_state()
    state.update(values)
    return write_json(state_path(), state)
