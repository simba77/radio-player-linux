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

### From a release

Every [release](https://github.com/simba77/radio-player-linux/releases) carries a `.deb` for
Debian, Ubuntu and KDE neon, built on Ubuntu 24.04, and a `.tar.gz` with the same files for other
distributions:

```bash
sudo apt install ./radio-player_*_amd64.deb
```

### From source

```bash
make deps      # build dependencies, needs sudo (Ubuntu 24.04 / KDE neon package names)
make install   # into ~/.local (use PREFIX=... for another location)
make deb       # or a .deb and a .tar.gz in dist/
```

Make sure `~/.local/bin` is in your `PATH`. To remove it, run `make uninstall` — it also turns
autostart off, since the entry would point at a command that no longer exists. Settings in
`~/.config/radio-player` are kept.

### Dependencies

Qt 6.4+ (Core, Gui, Widgets, Network, DBus) and libmpv 0.36+ at run time; building also needs
CMake 3.22+, a C++20 compiler, the Qt Linguist tools and, for the tests, Catch2 3 and dbus-daemon.
The SVG icon engine (`libqt6svg6`) is needed for themed icons; `qt6-wayland` gives a native
Wayland window and `qt6-translations-l10n` translates Qt's own dialogs.

## Running from source

```bash
make run
```

Diagnostics go to stderr, so a desktop launch lands in the journal. To see more,
run `radio-player --log-level debug`; `--version` prints the version.

## Development

```bash
make devenv  # .venv with the pinned dev tools: clang-format, gcovr, shellcheck
make test    # build and run the tests
make check   # formatting, scripts, tests under ASan/UBSan and the coverage threshold — what CI runs
make format  # apply clang-format
```

The tests need no display, sound or session bus: Qt runs on the offscreen platform, network code
talks to a local HTTP server, and the D-Bus tests start a private `dbus-daemon`.

Optionally `pip install pre-commit && pre-commit install` runs the same format checks before each
commit.

### Translations

Interface strings are English literals in the source, translated with Qt Linguist. After adding
or changing a string:

```bash
make i18n-update   # pull new strings into translations/*.ts
linguist translations/radio_player_ru.ts
```

The `.qm` files are compiled during the build and embedded in the binary; a test fails when the
catalog drifts from the code.

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

Tray, media keys and sleep inhibition cannot be covered by headless tests; the per-desktop
checklist for them is [docs/manual-testing.md](docs/manual-testing.md).

## Architecture

Data model, the two metadata providers and the playback state machine are described in
[docs/architecture.md](docs/architecture.md).

## Stack

C++20 · Qt 6 Widgets · libmpv · MPRIS2

Versions up to 1.0.0 were written in Python with PySide6; see the `v1.0.0` tag.

## License

[MIT](LICENSE). The libraries the player builds on keep their own licenses: Qt (LGPL-3) and
libmpv (LGPL-2.1+ or GPL-2+, depending on how it was built) are used as system libraries and are
not redistributed with this project.
