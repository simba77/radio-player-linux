# Architecture

What the code cannot say about itself: the data model, why metadata needs two providers, and
which invariants the playback state machine keeps. Everything here describes the current
behaviour — when they disagree, the code is right and this file is a bug.

## Layers

```
src/app/       entry point, arguments, Unix signals  (radio-player, radio_app)
src/ui/        windows, tray, settings tabs, i18n    (radio_ui: Qt Widgets)
src/desktop/   MPRIS, sleep inhibitor, autostart,    (radio_desktop: Qt DBus, XDG)
               network reachability, single instance
src/playback/  libmpv, playback states, metadata     (radio_playback: Qt Gui, libmpv)
src/core/      stations, catalogs, settings, disk    (radio_core: Qt Core and Network only)
```

Each layer is a static library with its own header directory, and dependencies point one way —
`app → ui → desktop → playback → core`. A layer sees only the headers and symbols of what it links,
so a forbidden dependency does not compile instead of waiting for a review. `src/app/main.cpp`
is the composition root: it builds the objects, connects them and owns their lifetime; nothing
else wires two subsystems together.

Data flows `main()` → `StationStore` / `SettingsStore` → `PlayerManager` → UI via Qt signals,
which replace `@Published` from the macOS version.

## Data model: catalogs plus a local overlay

Default stations are never hardcoded. They come from **catalogs** — JSON files published over
HTTPS — which are merged with each other and then with the user's own edits. A catalog owns a
station's URL; the user owns its visibility, name and position. Because those live in different
files, a catalog update can never conflict with an edit.

### Catalog format

```jsonc
{
  "schema": 1,
  "id": "record",                    // unique catalog id
  "name": "Radio Record",
  "revision": 3,                     // grows with every change
  "stations": [
    {
      "id": "record-main",           // stable slug, NOT a UUID — the merge key across revisions
      "name": "Radio Record — Main",
      "url": "https://hls-01-radiorecord.hostingradio.ru/record/playlist.m3u8"
    }
  ]
}
```

A station's `id` never changes: renaming changes `name`, moving a stream changes `url`, and a
station dropped from the catalog disappears for the user — unless they duplicated it (below).
Unknown fields are ignored, so a catalog may carry more than this version understands.

A catalog is rejected whole when `schema` is missing or newer than supported; individual stations
are dropped when they lack a name, carry an unusable URL scheme, or repeat an id. One typo must
not cost the user their station list.

### Overlay format (`overlay.json`)

```jsonc
{
  "schema": 1,
  "order": ["record-main", "local:9f3c…", "record-brks"],  // display order, hidden ones included
  "hidden": ["record-dc"],                                  // catalog stations the user hid
  "overrides": {
    "record-main": { "name": "Рекорд (основной)" }          // rename, URL untouched
  },
  "custom": [                                               // stations of the user's own
    { "id": "local:9f3c…", "name": "Моё радио", "url": "https://…" }
  ]
}
```

### Merge algorithm

1. Catalog stations, in the order the sources are configured; on an id collision the earlier
   catalog wins.
2. The user's `custom` stations, appended after them.
3. Anything in `hidden` is removed.
4. `overrides` are applied (only `name` today).
5. The result is sorted by `order`; stations absent from it — new ones from a catalog — go to the
   end, keeping the catalog's own order.

The URL of a catalog station cannot be edited. "Duplicate" instead creates a custom station with
the same stream and hides the original, so the user gets an editable copy while the catalog keeps
updating underneath.

Hidden stations stay in `order`, anchored after the visible station they follow — otherwise
un-hiding one would send it to the end of the list.

## Files on disk

```
~/.config/radio-player/
  settings.json      close_to_tray, update_interval_hours, extra_catalog_urls, language
  overlay.json       the user's edits, as above
  state.json         volume, last station, time of the last catalog check
~/.cache/radio-player/
  catalogs/<hash>.json   last successfully downloaded copy, keyed by source URL
  catalogs/etags.json    ETag per source URL
  artwork/               cover art, named by the hash of its URL
```

Every path is built in `src/core/paths.cpp`; `state.json` is written with
`storage::updateState()`, which re-reads before writing — it has several independent writers, and
a full rewrite would drop whichever key someone else had just set.

Catalog files are named by BLAKE2s-128 of the source URL, artwork by SHA-1 of its URL. Versions
1.x (Python) used BLAKE2b for catalogs, which Qt lacks: their cached catalogs are pruned and
fetched again, while their artwork is reused as is.

The catalog cache is a derivative of the configured source list, not a store of its own: it is
read strictly by that list and pruned of everything else, so removing a catalog removes its
stations.

## Catalog updates

- A bundled copy is embedded in the binary, so the application works immediately and offline.
- A background update runs ~2 s after start, and then no more often than the configured interval
  (24 h by default).
- `ETag` / `If-None-Match`; a 304 or any error leaves the cached copy in place, silently. The ETag
  is sent only while its cache file exists: a 304 carries no body, so a deleted cache would
  otherwise never come back.
- Extra catalogs must be HTTPS: a catalog decides what and where to play, so replacing it in
  transit replaces the whole station list.

## Metadata: two providers

Verified against live streams in August 2026.

**Direct Icecast/Shoutcast streams** — libmpv reports ICY metadata itself:

```
{'icy-title': 'TIESTO/BIA/21 SAVAGE - Both (Record Mix)', 'icy-url': ..., 'icy-pub': '1'}
```

**HLS streams from hostingradio (Radio Record)** — libmpv reports *nothing*: the metadata travels
not as ID3 inside segments but as a manifest tag that ffmpeg skips
(`[ffmpeg/demuxer] hls: Skip ('#EXT-X-DATERANGE:…')`):

```
#EXT-X-DATERANGE:ID="6a7a2f97…",CLASS="ru.hostingradio.meta",START-DATE="…",END-ON-NEXT=YES,
                 X-ARTIST="BLOND_ISH",X-TITLE="$MONEY",X-METADATA-URI="metadata/6a7a2f97….json"
```

This is what AVFoundation turned into `title`/`artist` on macOS. Waiting for ffmpeg to expose it
is pointless, so the player fetches it:

1. Poll the variant playlist every ~5 s (master → first `#EXT-X-STREAM-INF`, which carries the
   `hlssid` parameter — without it DATERANGE is not served).
2. Parse `#EXT-X-DATERANGE`; a changed `ID` means a changed track. `X-ARTIST` / `X-TITLE` give
   artist and title immediately, with no extra request.
3. `X-METADATA-URI` (relative to the variant playlist) returns JSON whose `current` object holds
   `coverImageUrl100/300/600`. **Cover art may be absent** — some stations never send it, and the
   UI shows a placeholder instead.

The provider is chosen by the `.m3u8` extension, so it also gets HLS stations that have no
DATERANGE at all. After a minute of manifests without the tag it stops polling: otherwise it
would query someone else's server every five seconds for the whole broadcast, finding nothing.

Metadata never affects playback: a failed poll, a 500 from the manifest or a missing tag are all
normal, and the stream keeps playing.

## Playback states

`PlaybackState` is the single observable state — `Idle`, `Loading`, `Playing`, `Reconnecting`,
`Failed`. It is never set optimistically: `Playing` means mpv confirmed audio (`core-idle` went
false), not that the player asked for it. Two derived views matter to everyone else:

- `isPlaying()` — sound is actually flowing (tray icon, list row);
- `isActive()` — the user is waiting for sound, including loading and reconnecting (stop button,
  sleep inhibitor, MPRIS `PlaybackStatus`), so nothing flickers while a stream rebuffers.

The *intent* to play (`shouldBePlaying_`) is separate from all of them and is cleared only by an
explicit `stop()`. A network drop does not cancel it — that is what makes an endless reconnect
correct rather than a runaway loop.

Reconnect starts at 3 s and doubles up to a minute, with ±25 % jitter: a radio server goes down
for every listener at once, and without jitter they all come back in the same second. The network
returning triggers an immediate attempt regardless of the pause, and a stall watchdog (6 s of
buffering) turns a hung stream into a reconnect.

Losing the network does not touch a stream that is already sounding: the buffer (about 20 s —
FFmpeg starts a live HLS stream three segments behind the edge) plays out and covers a short
Wi-Fi drop, while restarting the stream would throw it away. The break, if it comes, arrives the
usual way — end of file or the stall watchdog. FFmpeg's HLS demuxer gives the stream up as soon
as a playlist reload fails, but mpv reports it only once the buffer runs dry, often after the
network is back; so for a minute after the network returns a break is put down to our own outage
and reconnected at once — no listener wave to spread out there.

Audio already played is not kept at all: mpv's back buffer is off (`demuxer-max-back-bytes=0`).
By default it keeps 50 MiB and borrows the unused forward space as well, which on a live stream is
nearly all of it — up to 200 MiB of audio nobody will seek back to.

## Desktop integration

Everything in `src/desktop/` must degrade silently, because none of it is guaranteed to exist:

- **MPRIS2** gives media keys and the "Media Player" applet. `PropertiesChanged` must be sent
  with an empty **string** array (`as`): a list of variants goes out as `av`, and Plasma ignores
  such a signal without a word. The tests receive the signal from a private bus and check the
  signature.
- **Sleep inhibition** is explicit: neither libmpv nor an active MPRIS player keeps the system
  awake, and idle suspend cuts the stream off. The portal `org.freedesktop.portal.Inhibit` is
  tried first with the suspend flag only — the screen may still blank and lock — and works from a
  Flatpak sandbox too; when it is missing or fails, the legacy
  `org.freedesktop.PowerManagement.Inhibit` takes over for the rest of the session.
- **The tray is optional.** On GNOME without the AppIndicator extension there is none, and the
  application stays fully usable as a window.
- **Single instance** is a `QLocalServer` socket. The second launch hands the first its
  activation token and exits: on Wayland a window cannot raise itself without one.
