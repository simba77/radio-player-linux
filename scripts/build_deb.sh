#!/bin/sh
# Сборка .deb-пакета. Результат — dist/radio-player_<версия>_all.deb
set -eu

source_root="$(cd "$(dirname "$0")/.." && pwd)"
version="$(sed -n 's/^APP_VERSION = "\(.*\)"$/\1/p' "$source_root/radio_player/__init__.py")"
[ -n "$version" ] || { echo "Не удалось определить версию" >&2; exit 1; }

build_root="$source_root/build/deb"
dist_dir="$source_root/dist"
package="radio-player_${version}_all.deb"

# Changelog ведётся руками, поэтому его версия — второй источник правды.
# Расхождение ловим здесь, а не в баг-репорте: dpkg возьмёт версию из control,
# а пользователь прочитает changelog и не поймёт, что установил.
changelog_version="$(sed -n '1s/^radio-player (\(.*\)).*/\1/p' "$source_root/packaging/deb/changelog")"
[ "$changelog_version" = "$version" ] || {
    echo "Версия в changelog ($changelog_version) не совпадает с APP_VERSION ($version)" >&2
    exit 1
}

rm -rf "$build_root"
mkdir -p "$build_root/DEBIAN" "$dist_dir"

PREFIX=/usr DESTDIR="$build_root" sh "$source_root/scripts/install.sh" >/dev/null

# Debian Policy §12.5 и §12.7: без copyright и changelog пакет не примет
# ни один репозиторий, а lintian ругается ошибкой, а не предупреждением.
docdir="$build_root/usr/share/doc/radio-player"
mkdir -p "$docdir"
cp "$source_root/packaging/deb/copyright" "$docdir/copyright"
# -n: без метки времени, иначе пакет не воспроизводим побайтово.
gzip -9n -c "$source_root/packaging/deb/changelog" > "$docdir/changelog.Debian.gz"

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
 libmpv2,
 qt6-svg,
 qt6-translations-l10n
Maintainer: simba77 <itenterprise105@gmail.com>
Homepage: https://github.com/simba77/radio-player-linux
Description: Плеер интернет-радио
 Минималистичный плеер интернет-радио: иконка в системном лотке и обычное окно.
 Поддерживает HLS и прямые HTTP-потоки, показывает текущий трек с обложкой,
 интегрируется с системой через MPRIS2.
EOF

# md5sums позволяет dpkg и debsums заметить порчу установленных файлов.
(cd "$build_root" && find . -type f ! -path './DEBIAN/*' -printf '%P\0' \
    | sort -z | xargs -0 md5sum > DEBIAN/md5sums)

dpkg-deb --root-owner-group --build "$build_root" "$dist_dir/$package" >/dev/null
echo "$dist_dir/$package"
