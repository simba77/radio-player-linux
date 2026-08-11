#!/bin/sh
# Удаление приложения, установленного scripts/install.sh.
#
#   PREFIX — откуда удалять (по умолчанию ~/.local)
set -eu

PREFIX="${PREFIX:-$HOME/.local}"
config_home="${XDG_CONFIG_HOME:-$HOME/.config}"

rm -rf "$PREFIX/lib/radio-player"
rm -f "$PREFIX/bin/radio-player"
rm -f "$PREFIX/share/applications/io.github.simba77.RadioPlayer.desktop"
# Имя из версий до перехода на reverse-DNS.
rm -f "$PREFIX/share/applications/radio-player.desktop"

# Файл автозапуска создаёт само приложение, и он указывает на команду, которую
# мы только что удалили. Оставить его — значит заставлять сессию при каждом
# входе запускать несуществующий бинарник. Второе имя — из версий до
# перехода на reverse-DNS.
rm -f "$config_home/autostart/io.github.simba77.RadioPlayer.desktop"
rm -f "$config_home/autostart/radio-player.desktop"

echo "Удалено из $PREFIX. Настройки в $config_home/radio-player остались."
