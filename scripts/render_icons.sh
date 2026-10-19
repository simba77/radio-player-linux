#!/bin/sh
# Растеризация иконки приложения в размеры hicolor.
#
# SVG — источник, PNG коммитятся вместе с ним: не в каждом окружении приложение
# получает отрисовщик SVG, а иконка нужна и там. Запускается вручную после правки
# `.svg` (`make icons`), потому что результат идёт в репозиторий. Нужен
# rsvg-convert (пакет librsvg2-bin).
set -eu

source_root="$(cd "$(dirname "$0")/.." && pwd)"
icons="$source_root/data/icons/hicolor"
name="io.github.simba77.RadioPlayer"
svg="$icons/scalable/apps/$name.svg"

command -v rsvg-convert >/dev/null 2>&1 || {
    echo "Нужен rsvg-convert: sudo apt install librsvg2-bin" >&2
    exit 1
}

# Размеры, которые ожидают оболочки: панель, список окон, лаунчер, настройки.
for size in 16 24 32 48 64 128 256; do
    target="$icons/${size}x${size}/apps"
    mkdir -p "$target"
    rsvg-convert --width "$size" --height "$size" --output "$target/$name.png" "$svg"
    echo "$target/$name.png"
done
