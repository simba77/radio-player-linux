#!/bin/sh
# Удаление приложения, установленного `make install`.
#
#   PREFIX — откуда удалять (по умолчанию ~/.local)
set -eu

PREFIX="${PREFIX:-$HOME/.local}"
config_home="${XDG_CONFIG_HOME:-$HOME/.config}"

# Каталог с пакетом версий 1.x на Python — его оставила их установка.
rm -rf "$PREFIX/lib/radio-player"
rm -f "$PREFIX/bin/radio-player"
rm -rf "$PREFIX/share/doc/radio-player"
rm -f "$PREFIX/share/applications/io.github.simba77.RadioPlayer.desktop"
# Имя из версий до перехода на reverse-DNS.
rm -f "$PREFIX/share/applications/radio-player.desktop"
rm -f "$PREFIX/share/metainfo/io.github.simba77.RadioPlayer.metainfo.xml"
find "$PREFIX/share/icons/hicolor" -name 'io.github.simba77.RadioPlayer.*' -delete 2>/dev/null || true

# Файл автозапуска создаёт само приложение, и он указывает на команду, которую
# мы только что удалили. Оставить его — значит заставлять сессию при каждом
# входе запускать несуществующий бинарник. Второе имя — из версий до
# перехода на reverse-DNS.
rm -f "$config_home/autostart/io.github.simba77.RadioPlayer.desktop"
rm -f "$config_home/autostart/radio-player.desktop"

echo "Удалено из $PREFIX. Настройки в $config_home/radio-player остались."
