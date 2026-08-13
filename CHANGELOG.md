# Changelog

The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and the project uses [Semantic Versioning](https://semver.org/).

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

[1.0.0]: https://github.com/simba77/radio-player-linux/releases/tag/v1.0.0
