# Radio Player (Linux)

[![CI](https://github.com/simba77/radio-player-linux/actions/workflows/ci.yml/badge.svg)](https://github.com/simba77/radio-player-linux/actions/workflows/ci.yml)

A minimal internet radio player: a system tray icon plus a regular window.
Plays HLS and direct HTTP(S) streams, shows the current track with cover art and survives
network drops on its own.

The interface speaks English and Russian, following the system locale unless you pick a language
in the settings.

## Features

- **Playback** of HLS (`.m3u8`) and direct HTTP(S) streams via libmpv
- **Current track** — artist, title and cover art in the window; click the title to copy it
- **Automatic reconnect** when the network drops, with tray indication
- **Station catalogs** — the list updates from a repository without overwriting your edits
- **Your own stations** — added by hand or imported from `.m3u`, `.m3u8`, `.pls` and JSON
- **Desktop integration** — MPRIS2: media keys and the "Media Player" applet in Plasma
- **The machine stays awake** while on air (the screen still blanks and locks as usual)
- **Autostart** on login
- **English and Russian** interface, switchable in the settings
- Shortcuts: `P` — play/stop, `Q` — quit

## Installation

### From .deb

```bash
make deb
sudo apt install ./dist/radio-player_0.1.0_all.deb
```

### Into your home directory

```bash
make deps      # system dependencies, needs sudo
make install   # into ~/.local (use PREFIX=... for another location)
```

Make sure `~/.local/bin` is in your `PATH`. To remove it, run `make uninstall` — it also turns
autostart off, since the entry would point at a command that no longer exists. Settings in
`~/.config/radio-player` are kept.

### Dependencies

Python 3.12+, PySide6 (Qt 6), libmpv 0.36+. On Ubuntu/KDE neon a single command installs them:

```bash
sudo apt install libmpv2 python3-mpv \
    python3-pyside6.qtcore python3-pyside6.qtgui python3-pyside6.qtwidgets \
    python3-pyside6.qtnetwork python3-pyside6.qtdbus
```

## Running from source

```bash
make run
```

Diagnostics go to stderr, so a desktop launch lands in the journal. To see more,
run `radio-player --log-level debug`; `--version` prints the version.

## Development

```bash
make devenv  # .venv with the pinned dev tools (system packages stay visible)
make check   # lint, types and tests — what CI runs
make format  # apply ruff format and the fixes ruff can make itself
make probe   # inspect metadata of a live stream
```

Optionally `pip install pre-commit && pre-commit install` runs the same lint and format checks
before each commit.

### Translations

Interface strings are English literals in the source, translated with Qt Linguist. After adding
or changing a string:

```bash
make deps-i18n     # once: pyside6-lupdate, which unlike the Qt one understands Python
make i18n-update   # pull new strings into translations/*.ts
linguist translations/radio_player_ru.ts
make i18n-release  # compile to radio_player/resources/i18n/*.qm
```

The compiled `.qm` files are committed, so installing the app needs no Qt tooling.

## Station catalogs

The station list is assembled from **catalogs** — JSON files the app downloads over HTTPS and
merges with your edits. Your renames, hidden stations and ordering are stored separately
(`~/.config/radio-player/overlay.json`), so a catalog update never overwrites them.

Catalog format:

```json
{
  "schema": 1,
  "id": "record",
  "name": "Radio Record",
  "revision": 1,
  "stations": [
    {
      "id": "record-main",
      "name": "Radio Record — Main",
      "url": "https://hls-01-radiorecord.hostingradio.ru/record/playlist.m3u8"
    }
  ]
}
```

A station's `id` is its stable key: the app recognises the station across updates by it, so it must
never change, while `name` and `url` may. Add your own catalog under **Настройки → Каталоги**
(Settings → Catalogs) by pointing it at such a file.

The URL of a catalog station cannot be edited directly. To change it, use «Сделать копию»
(Duplicate): you get your own station with the same stream while the original is hidden. That way
your edits never conflict with catalog updates.

## Files

| Path | Contents |
|---|---|
| `~/.config/radio-player/settings.json` | application settings |
| `~/.config/radio-player/overlay.json` | your edits to the station list |
| `~/.config/radio-player/state.json` | volume, last station |
| `~/.cache/radio-player/` | downloaded catalogs and cover art |

## Environments

Developed and verified on KDE neon (Plasma 6, Wayland). On GNOME the system tray is unavailable
without the AppIndicator extension — there the app runs as a window and through MPRIS, with no
panel icon.

## Architecture

Data model, the two metadata providers and the playback state machine are described in
[docs/architecture.md](docs/architecture.md).

## Stack

Python · PySide6 (Qt 6) · libmpv · MPRIS2

## License

[MIT](LICENSE). The libraries the player builds on keep their own licenses: PySide6 (LGPL-3),
python-mpv (GPL-2+ or LGPL-2.1+) and libmpv (LGPL-2.1+) are used as system packages and are
not redistributed with this project.
