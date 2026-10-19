# Changelog

The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and the project uses [Semantic Versioning](https://semver.org/).

## [2.0.0] — 2026-10-19

Rewritten in C++. Settings, station edits and cover art carry over from 1.0.0 untouched.

### Changed

- The player is written in C++20 with Qt 6 Widgets and libmpv; Python, PySide6 and python-mpv
  are no longer needed at run time.
- Releases carry a `.deb` for Debian, Ubuntu and KDE neon and a `.tar.gz` for other
  distributions, built on Ubuntu 24.04; building from source uses CMake.
- System sleep is inhibited through `org.freedesktop.portal.Inhibit`, which also works in a
  Flatpak sandbox, with `org.freedesktop.PowerManagement.Inhibit` as the fallback.
- Cached catalogs are downloaded once more after the upgrade: their file names changed.
- The bundled catalog moved to `data/catalogs/record.json`; 1.0.0 keeps its last downloaded copy
  but receives no further catalog updates.

### Fixed

- Quitting during a catalog update recorded it as done, so the next start skipped the check.
- A deleted catalog cache was never restored: its ETag was still sent and the server answered 304.
- A timed-out HLS master playlist left the station without track info until a restart.
- A non-numeric volume in `state.json` stopped the app from starting.
- MPRIS `Seek` and `SetPosition` used argument types that differ from the specification.
- The first playback blocked the window while the power service was introspected.
- SIGTERM during startup ended the app without a clean shutdown.
- The autostart entry showed a generic icon instead of the application's.

### Removed

- The Python implementation; it stays available at the `v1.0.0` tag.
- `make probe`, the metadata diagnostics built on python-mpv.

## [1.0.0] — 2026-08-13

First public release. A tray radio player for Linux, ported from the macOS
[radio-player](https://github.com/simba77/radio-player) with feature parity and
a new station catalog subsystem.

### Playback

- HLS (`.m3u8`) and direct HTTP(S) streams through libmpv.
- Explicit playback state machine: idle, loading, playing, reconnecting, error —
  derived from mpv properties, never from optimistic flags.
- Endless reconnect with backoff, a stall watchdog, and an immediate retry when
  the network comes back.
- Volume with persistence, debounced so the slider does not write `state.json`
  on every pixel.

### Metadata

- Artist and title from ICY tags for direct streams.
- Artist, title and cover art for HLS through `#EXT-X-DATERANGE`, which libmpv
  does not expose; the provider stays silent on manifests that carry no such tag.
- Cover art cache with a size limit.
- Clicking the track title copies it to the clipboard.

### Stations

- Stations come from JSON catalogs merged with a local overlay, so a catalog
  update can never overwrite the user's edits.
- Hide, rename, reorder by drag-and-drop, fork a catalog station into your own,
  and add stations of your own.
- Catalog sources are configurable; catalogs refresh in the background at most
  once a day, with ETag caching and a fallback to the bundled copy.
- Import from `.m3u`, `.m3u8`, `.pls` and from the macOS version's JSON; export
  to `.m3u`.

### Desktop integration

- Tray icon reflecting the playback state, with a menu; the app stays fully
  usable without a tray (GNOME without the AppIndicator extension).
- MPRIS2: playback control, metadata, cover art and volume — media keys and the
  Plasma "Media Player" applet work.
- System sleep is inhibited while playing or reconnecting.
- Autostart on login, an application icon in hicolor, a reverse-DNS desktop
  entry and AppStream metadata.
- English and Russian interface, selectable in the settings.

### Packaging

- `make install` into `~/.local` without touching the system site-packages, and
  a `.deb` built from the same install script.

[2.0.0]: https://github.com/simba77/radio-player-linux/releases/tag/v2.0.0
[1.0.0]: https://github.com/simba77/radio-player-linux/releases/tag/v1.0.0
