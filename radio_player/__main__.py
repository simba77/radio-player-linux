"""Точка входа приложения."""

from __future__ import annotations

import argparse
import logging
import signal
import sys

from PySide6.QtCore import QTimer
from PySide6.QtWidgets import QApplication, QSystemTrayIcon

from . import APP_ID, APP_NAME, APP_RDNS_ID, APP_VERSION, i18n
from .core import catalog, paths
from .core.settings_store import SettingsStore
from .core.station_store import StationStore
from .desktop.mpris import MprisServer
from .desktop.network import install_network_monitor
from .desktop.power import SleepInhibitor
from .desktop.single_instance import InstanceServer, notify_running_instance
from .playback.player import PlayerManager
from .ui.main_window import MainWindow
from .ui.tray import TrayIcon

#: Задержка перед фоновым обновлением каталогов, чтобы не тормозить запуск.
_CATALOG_UPDATE_DELAY_MS = 2000

_log = logging.getLogger(__name__)


def main(argv: list[str] | None = None) -> int:
    argv = sys.argv if argv is None else argv
    args, qt_argv = parse_args(argv)
    _setup_logging(args.log_level)

    if notify_running_instance():
        _log.info("%s уже запущен — показали окно существующего экземпляра.", APP_NAME)
        return 0

    paths.ensure_dirs()

    settings = SettingsStore()
    # Язык выбирается до QApplication: свой переводчик Qt ставит при создании
    # приложения, и позже его уже не сменить.
    i18n.prepare(settings.language)

    app = QApplication(qt_argv)
    app.setApplicationName(APP_NAME)
    app.setApplicationVersion(APP_VERSION)
    # Имя `.desktop`-файла: по нему Wayland связывает окно с иконкой приложения.
    app.setDesktopFileName(APP_RDNS_ID)
    # Окно прячется в трей — Qt не должен завершать приложение при закрытии последнего окна.
    app.setQuitOnLastWindowClosed(False)

    # Переводчики ставятся до первого виджета: строки берутся при создании.
    i18n.install(app, settings.language)

    # Обновление создаётся первым: оно владеет списком источников, а стартовый
    # набор каталогов читается из кэша ровно по этому списку.
    updater = catalog.CatalogUpdater(settings.extra_catalog_urls, parent=app)
    store = StationStore(catalog.load_initial(updater.urls))
    player = PlayerManager(store)
    install_network_monitor(player, app)

    updater.finished.connect(lambda catalogs, changed: store.set_catalogs(catalogs) if changed else None)
    wire_catalog_sources(settings, updater, store)

    window = MainWindow(player, store, settings, updater)

    has_tray = QSystemTrayIcon.isSystemTrayAvailable()
    if has_tray:
        tray = TrayIcon(
            player,
            on_toggle_window=window.toggle_visibility,
            on_settings=window.open_settings,
            on_quit=app.quit,
        )
        tray.show()
    else:
        # Без лотка (например, GNOME без AppIndicator) прятать окно некуда.
        _log.warning("Системный лоток недоступен — работаем только окном.")
        app.setQuitOnLastWindowClosed(True)
        window.allow_close()

    server = InstanceServer(on_show_requested=window.show_and_raise)
    app.aboutToQuit.connect(server.close)
    app.aboutToQuit.connect(player.shutdown)

    inhibitor = SleepInhibitor(player, parent=app)
    app.aboutToQuit.connect(inhibitor.release)

    mpris = MprisServer(player, on_raise=window.show_and_raise, on_quit=app.quit, parent=app)
    if mpris.register():
        app.aboutToQuit.connect(mpris.unregister)
    else:
        # Без MPRIS остаются окно и трей — медиаклавиши просто не работают.
        _log.warning("MPRIS недоступен: медиаклавиши и виджет плеера работать не будут.")

    _schedule_catalog_update(updater, settings)
    app.aboutToQuit.connect(updater.cancel)

    window.show()

    # Даём Python-обработчику Ctrl+C шанс сработать: Qt-цикл иначе не отдаёт управление.
    signal.signal(signal.SIGINT, lambda *_: app.quit())
    wakeup = QTimer(app)
    wakeup.start(300)
    wakeup.timeout.connect(lambda: None)

    return app.exec()


def parse_args(argv: list[str]) -> tuple[argparse.Namespace, list[str]]:
    """Разбирает свои аргументы, остальные оставляет Qt.

    У Qt свой набор ключей (`-platform`, `-style`), и перечислять их у себя
    нельзя — они меняются от версии к версии. Поэтому неизвестные аргументы
    не ошибка, а то, что уходит в `QApplication`.
    """
    parser = argparse.ArgumentParser(
        prog=APP_ID,
        description="Плеер интернет-радио: трей, MPRIS, HLS и прямые потоки.",
    )
    parser.add_argument("--version", action="version", version=f"{APP_ID} {APP_VERSION}")
    parser.add_argument(
        "--log-level",
        default="info",
        choices=("debug", "info", "warning", "error"),
        help="сколько писать в stderr (по умолчанию info)",
    )
    args, unknown = parser.parse_known_args(argv[1:])
    return args, [argv[0], *unknown]


def _setup_logging(level: str) -> None:
    # В stderr, а не в stdout: при запуске из `.desktop` вывод забирает journald,
    # и диагностика не должна смешиваться с обычным выводом программы.
    logging.basicConfig(
        stream=sys.stderr,
        level=getattr(logging, level.upper()),
        format="%(levelname)s: %(message)s",
    )


def wire_catalog_sources(
    settings: SettingsStore, updater: catalog.CatalogUpdater, store: StationStore
) -> None:
    """Связывает список каталогов с теми, кто на него опирается.

    Правится он в настройках, а нужен и обновлению, и списку станций — причём
    сразу: добавленный каталог должен заработать, а удалённый перестать
    поставлять станции, не дожидаясь перезапуска.
    """

    def apply() -> None:
        updater.set_extra_urls(settings.extra_catalog_urls)
        store.set_catalogs(catalog.load_initial(updater.urls))

    settings.catalog_urls_changed.connect(apply)


def _schedule_catalog_update(updater: catalog.CatalogUpdater, settings: SettingsStore) -> None:
    """Обновляет каталоги в фоне, если с прошлого раза прошёл заданный интервал."""
    elapsed = catalog.seconds_since_update()
    if elapsed is None or elapsed >= settings.update_interval_seconds:
        QTimer.singleShot(_CATALOG_UPDATE_DELAY_MS, updater.update_now)


if __name__ == "__main__":
    sys.exit(main())
