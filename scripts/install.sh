#!/bin/sh
# Установка приложения. Используется и для `make install` (в ~/.local),
# и при сборке .deb (PREFIX=/usr DESTDIR=<каталог сборки>).
#
#   PREFIX  — куда приложение будет установлено (по умолчанию ~/.local)
#   DESTDIR — временный корень для упаковки; в путях внутри файлов не участвует
set -eu

PREFIX="${PREFIX:-$HOME/.local}"
DESTDIR="${DESTDIR:-}"

source_root="$(cd "$(dirname "$0")/.." && pwd)"
libdir="$DESTDIR$PREFIX/lib/radio-player"
bindir="$DESTDIR$PREFIX/bin"
appdir="$DESTDIR$PREFIX/share/applications"
icondir="$DESTDIR$PREFIX/share/icons/hicolor"
metainfodir="$DESTDIR$PREFIX/share/metainfo"

mkdir -p "$libdir" "$bindir" "$appdir"

# Пакет целиком: приложение запускается как `python3 -m radio_player`.
rm -rf "$libdir/radio_player"
cp -r "$source_root/radio_player" "$libdir/"
find "$libdir" -name __pycache__ -type d -prune -exec rm -rf {} +

# Обёртка знает, где лежит пакет, — установка не трогает системный site-packages
# и потому не конфликтует с PEP 668.
cat > "$bindir/radio-player" <<EOF
#!/bin/sh
PYTHONPATH="$PREFIX/lib/radio-player\${PYTHONPATH:+:\$PYTHONPATH}"
export PYTHONPATH
exec python3 -m radio_player "\$@"
EOF
chmod 755 "$bindir/radio-player"

# Ярлык из версий до перехода на reverse-DNS: иначе в меню останутся два.
rm -f "$appdir/radio-player.desktop"
cp "$source_root/radio_player/resources/io.github.simba77.RadioPlayer.desktop" "$appdir/"

# Иконки темы hicolor: без них оболочка покажет приложение безымянным квадратом,
# а имя `audio-radio` есть только в Breeze.
mkdir -p "$icondir"
cp -r "$source_root/radio_player/resources/icons/hicolor/." "$icondir/"

# AppStream: по нему приложение видно в Discover и GNOME Software.
mkdir -p "$metainfodir"
cp "$source_root/packaging/appstream/io.github.simba77.RadioPlayer.metainfo.xml" "$metainfodir/"

# Кэши оболочки: без обновления ярлык и иконка появятся только после перелогина.
# Команд может не быть — установка от этого не должна падать.
command -v update-desktop-database >/dev/null 2>&1 && [ -z "$DESTDIR" ] &&
    update-desktop-database "$appdir" 2>/dev/null || true
command -v gtk-update-icon-cache >/dev/null 2>&1 && [ -z "$DESTDIR" ] &&
    gtk-update-icon-cache -q -t -f "$icondir" 2>/dev/null || true

echo "Установлено в $PREFIX"
case ":$PATH:" in
    *":$PREFIX/bin:"*) ;;
    *) echo "Внимание: $PREFIX/bin отсутствует в PATH." ;;
esac
