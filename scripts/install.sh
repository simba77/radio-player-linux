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

echo "Установлено в $PREFIX"
case ":$PATH:" in
    *":$PREFIX/bin:"*) ;;
    *) echo "Внимание: $PREFIX/bin отсутствует в PATH." ;;
esac
