"""Аргументы командной строки.

Ключи Qt (`-platform`, `-style`) разбирает сама `QApplication`, и перечислять
их у себя нельзя — они меняются от версии к версии. Поэтому проверяется ровно
одно: свои аргументы разобраны, чужие дошли до Qt в целости.
"""

from __future__ import annotations

import pytest

from radio_player import APP_ID, APP_VERSION
from radio_player.__main__ import parse_args


def test_defaults():
    args, qt_argv = parse_args(["radio-player"])
    assert args.log_level == "info"
    assert qt_argv == ["radio-player"]


def test_log_level_is_read():
    args, _ = parse_args(["radio-player", "--log-level", "debug"])
    assert args.log_level == "debug"


def test_unknown_log_level_is_rejected():
    with pytest.raises(SystemExit):
        parse_args(["radio-player", "--log-level", "громко"])


def test_version_is_printed_and_exits(capsys):
    """Пакетчики и авторы баг-репортов ожидают `--version` по умолчанию."""
    with pytest.raises(SystemExit) as exit_info:
        parse_args(["radio-player", "--version"])
    assert exit_info.value.code == 0
    assert capsys.readouterr().out.strip() == f"{APP_ID} {APP_VERSION}"


def test_qt_arguments_are_passed_through():
    args, qt_argv = parse_args(["radio-player", "--log-level", "error", "-platform", "offscreen"])
    assert args.log_level == "error"
    # Имя программы Qt ожидает первым: по нему она подписывает окна и ошибки.
    assert qt_argv == ["radio-player", "-platform", "offscreen"]
