"""Radio Player — плеер интернет-радио для Linux."""

#: Короткое имя: команда `radio-player`, каталоги в ~/.config и ~/.cache.
#: Спецификация XDG не требует для них reverse-DNS, а переименование каталога
#: стоило бы миграции пользовательских файлов без всякой пользы.
APP_ID = "radio-player"
#: Идентификатор приложения в системе: имя `.desktop`-файла, иконки, AppStream,
#: имя на шине D-Bus. Здесь reverse-DNS обязателен — на нём держится связь окна
#: с иконкой в Wayland и приём приложения в Flathub.
APP_RDNS_ID = "io.github.simba77.RadioPlayer"
APP_NAME = "Radio Player"
#: Единственный источник версии: pyproject, changelog пакета и AppStream
#: сверяются с ним тестом `tests/test_version.py`.
APP_VERSION = "1.0.0"

__all__ = ["APP_ID", "APP_NAME", "APP_RDNS_ID", "APP_VERSION"]
