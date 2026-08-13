"""Версия продублирована в пяти файлах, и разъезжаются они молча.

`APP_VERSION` — источник истины: по нему `scripts/build_deb.sh` называет пакет.
Расхождение проявляется поздно и неприятно: в `.deb` одна версия, в `--version`
другая, а магазины приложений показывают третью из AppStream.
"""

from __future__ import annotations

import re
import tomllib
import xml.etree.ElementTree as ET
from pathlib import Path

from radio_player import APP_VERSION

ROOT = Path(__file__).resolve().parent.parent


def test_pyproject_matches() -> None:
    data = tomllib.loads((ROOT / "pyproject.toml").read_text(encoding="utf-8"))
    assert data["project"]["version"] == APP_VERSION


def test_deb_changelog_matches() -> None:
    first_line = (ROOT / "packaging" / "deb" / "changelog").read_text(encoding="utf-8").splitlines()[0]
    assert re.match(rf"^radio-player \({re.escape(APP_VERSION)}\)", first_line)


def test_appstream_lists_the_release() -> None:
    path = ROOT / "packaging" / "appstream" / "io.github.simba77.RadioPlayer.metainfo.xml"
    releases = ET.parse(path).getroot().find("releases")
    assert releases is not None
    # Свежий выпуск идёт первым: AppStream показывает список сверху вниз.
    assert releases[0].get("version") == APP_VERSION


def test_changelog_has_a_section() -> None:
    text = (ROOT / "CHANGELOG.md").read_text(encoding="utf-8")
    assert f"## [{APP_VERSION}]" in text
