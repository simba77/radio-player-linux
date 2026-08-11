#!/bin/sh
# Сборка .deb-пакета. Результат — dist/radio-player_<версия>_all.deb
set -eu

source_root="$(cd "$(dirname "$0")/.." && pwd)"
version="$(sed -n 's/^APP_VERSION = "\(.*\)"$/\1/p' "$source_root/radio_player/__init__.py")"
[ -n "$version" ] || { echo "Не удалось определить версию" >&2; exit 1; }

build_root="$source_root/build/deb"
dist_dir="$source_root/dist"
package="radio-player_${version}_all.deb"

rm -rf "$build_root"
mkdir -p "$build_root/DEBIAN" "$dist_dir"

PREFIX=/usr DESTDIR="$build_root" sh "$source_root/scripts/install.sh" >/dev/null

# Приложение — чистый Python, поэтому архитектура all. Зависимости ставим
# из репозиториев: PySide6 и python-mpv собирать самим незачем.
cat > "$build_root/DEBIAN/control" <<EOF
Package: radio-player
Version: $version
Section: sound
Priority: optional
Architecture: all
Depends: python3 (>= 3.12),
 python3-pyside6.qtcore,
 python3-pyside6.qtgui,
 python3-pyside6.qtwidgets,
 python3-pyside6.qtnetwork,
 python3-pyside6.qtdbus,
 python3-mpv,
 libmpv2
Maintainer: simba77 <itenterprise105@gmail.com>
Description: Плеер интернет-радио
 Минималистичный плеер интернет-радио: иконка в системном лотке и обычное окно.
 Поддерживает HLS и прямые HTTP-потоки, показывает текущий трек с обложкой,
 интегрируется с системой через MPRIS2.
EOF

dpkg-deb --root-owner-group --build "$build_root" "$dist_dir/$package" >/dev/null
echo "$dist_dir/$package"
