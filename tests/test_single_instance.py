"""Протокол повторного запуска.

Второй запуск не открывает второе окно, а просит первый показать своё — и
передаёт токен активации. На Wayland без него композитор фокус не отдаст,
так что «показали окно» превратилось бы в мигание в панели задач.
"""

from __future__ import annotations

import os

import pytest

from radio_player.core import paths
from radio_player.desktop import single_instance

pytestmark = pytest.mark.usefixtures("qt_app")

# Токен активации — не секрет: композитор выдаёт его запускаемому процессу.
TOKEN = "wayland-token-abc123"  # noqa: S105


def test_request_carries_the_token():
    assert single_instance.decode_request(single_instance.encode_request(TOKEN)) == TOKEN


def test_request_without_a_token_is_still_valid():
    """Запуск из терминала токена не получает — окно всё равно надо показать."""
    assert single_instance.decode_request(single_instance.encode_request("")) == ""


def test_foreign_data_is_ignored():
    assert single_instance.decode_request(b"\x00\x01whatever\n") is None


def test_token_is_read_from_the_environment(monkeypatch):
    monkeypatch.setenv("XDG_ACTIVATION_TOKEN", TOKEN)
    assert single_instance.activation_token() == TOKEN


def test_x11_startup_id_is_accepted_too(monkeypatch):
    monkeypatch.delenv("XDG_ACTIVATION_TOKEN", raising=False)
    monkeypatch.setenv("DESKTOP_STARTUP_ID", "startup-id-1")
    assert single_instance.activation_token() == "startup-id-1"


def test_no_token_outside_a_session(monkeypatch):
    monkeypatch.delenv("XDG_ACTIVATION_TOKEN", raising=False)
    monkeypatch.delenv("DESKTOP_STARTUP_ID", raising=False)
    assert single_instance.activation_token() == ""


def test_second_launch_hands_the_token_to_the_first(monkeypatch, tmp_path):
    """Полный круг: сервер слушает, второй экземпляр стучится, токен доходит."""
    monkeypatch.setenv("XDG_RUNTIME_DIR", str(tmp_path))
    monkeypatch.setenv("XDG_ACTIVATION_TOKEN", TOKEN)

    received: list[str] = []
    server = single_instance.InstanceServer(on_show_requested=received.append)
    try:
        assert single_instance.notify_running_instance() is True
        # Данные приходят отдельным событием — даём циклу их доставить.
        from PySide6.QtCore import QCoreApplication, QDeadlineTimer

        deadline = QDeadlineTimer(1000)
        while not received and not deadline.hasExpired():
            QCoreApplication.processEvents()
        assert received == [TOKEN]
    finally:
        server.close()


def test_nothing_answers_when_no_instance_runs(monkeypatch, tmp_path):
    monkeypatch.setenv("XDG_RUNTIME_DIR", str(tmp_path))
    assert single_instance.notify_running_instance() is False


def test_socket_lives_in_the_runtime_directory(monkeypatch, tmp_path):
    """В общем /tmp сокет был бы один на всю машину: второй пользователь,
    запустив приложение, поднимал бы окно первому."""
    monkeypatch.setenv("XDG_RUNTIME_DIR", str(tmp_path))
    assert paths.ipc_socket().parent == tmp_path


def test_socket_falls_back_per_user(monkeypatch):
    monkeypatch.delenv("XDG_RUNTIME_DIR", raising=False)
    assert str(os.getuid()) in str(paths.ipc_socket())
