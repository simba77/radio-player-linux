# CLAUDE.md

## Development approach

Carried over from the macOS version of the project:

- **States, not just the happy path.** Every feature covers errors, loading and edge cases.
  Adding playback means adding `isLoading()` and `errorMessage()`; adding a list means adding
  an empty state.
- **State consistency.** If `isPlaying()` is true, something must actually be playing. Observed
  mpv state beats optimistic flags.
- **Resource lifecycle.** Every mpv observer, `QTimer`, network reply and D-Bus object is torn
  down explicitly. Abort a `QNetworkReply` only after disconnecting from it: `abort()` reports
  completion synchronously, and the handler would treat the cancelled request as finished.
- **Fix bugs found along the way.** A bug noticed while working on a task gets fixed and
  mentioned in the summary.

## Language

Code comments are in Russian; talk to the user in Russian too. Commit messages, this file and
`README.md` are in English.

User-facing strings are English literals inside `tr()`, translated through Qt Linguist —
`translations/*.ts`, compiled by lrelease during the build and embedded in the binary. After
touching any interface string run `make i18n-update`; a test runs lupdate over the sources and
fails if the catalog drifts from the code. `radio_player_en.ts` holds only plural forms — the
source strings are already English. Outside a `QObject` use
`QCoreApplication::translate("radio::Context", "literal")`: lupdate extracts statically and cannot
see through a wrapper function. Console output is a log, not interface, and stays in Russian
untranslated.

## Commits

Conventional Commits: `feat(power): inhibit system sleep during playback`.

## Environment

C++20, Qt 6 Widgets, libmpv, CMake; build targets: `make help`, build dependencies: `make deps`.
Dev tools (clang-format, gcovr, shellcheck) live in `.venv` with pinned versions — installing
Python packages globally is refused (PEP 668).

Development happens on KDE neon (Plasma 6, Wayland) with Qt 6.11, but releases are built on
Ubuntu 24.04 with **Qt 6.4**, and nothing newer may be used. A local build does not catch it; CI
does, and so does a build in an `ubuntu:24.04` container. Also verify on X11 and GNOME. On GNOME
`QSystemTrayIcon` is unavailable without the AppIndicator extension, so the app must stay fully
usable with no tray.

## Traps

**libmpv crashes under a non-C numeric locale**, and Qt sets the system one. Call
`setlocale(LC_NUMERIC, "C")` before `mpv_create()` — `LibMpv` does.

**Check D-Bus signatures with `busctl --user monitor`** — `dbus-monitor` does not show them.
`PropertiesChanged` must carry the invalidated properties as a string array (`as`): a list of
variants makes it `sa{sv}av`, properties still read fine and `dbus-monitor` shows the signal, but
plasmashell and kded silently ignore it — the player never appears in the "Media Player" applet
and never receives media keys. D-Bus integer types follow the C++ type: a method with signature
`u` (`UnInhibit`) needs a `uint`, an `int` is rejected with `UnknownMethod`.

**The test D-Bus must not activate services.** The tests start their own `dbus-daemon` so they
never touch the user's session; it runs with a config without service directories, otherwise the
app's first call (AT-SPI, portals) would start real session services on it.

**A Wayland window does not know it was minimized.** xdg-shell has no such state, so on Plasma 6
`isVisible()` stays true and `isMinimized()` stays false even right after `showMinimized()` — the
compositor configures the state away and Qt reverts to `WindowNoState` (measured with a KWin
script watching `minimizedChanged`). Toggling the window from the tray must therefore key off
`isActiveWindow()`. Restoring it is the compositor's job: `showNormal()` does nothing, but
activation unminimizes the window, and plasmashell hands out an activation token through
`ProvideXdgActivationToken` right before it calls `Activate` on the tray icon — Qt puts it into
the environment, so a plain `activateWindow()` from the tray handler works. What must not be done
is hiding and showing the window to restore it: KWin remembers the minimized state per
application and applies it to the fresh window, which then flashes and vanishes (the same memory
makes a freshly started instance come up minimized).

**System sleep must be inhibited explicitly** (`src/desktop/power.cpp`): neither libmpv nor an
active MPRIS player holds the system awake, and idle auto-suspend cuts the stream off.

## Decisions not visible from the code

`docs/architecture.md` holds the long version: layers, catalog and overlay formats, the merge
rules, why metadata needs two providers and what the playback states guarantee. Keep it in sync —
when the code and that file disagree, the file is the bug.

- Data flow: `main()` builds `StationStore` and `SettingsStore`, hands them to `PlayerManager`
  and the UI, and connects the two — nothing else wires subsystems together. Qt signals replace
  `@Published` from the macOS version.
- **Layers are enforced, not just intended**: each layer in `src/` is a static library with its
  own header directory, linked one way — `app → ui → desktop → playback → core`. A forbidden
  dependency does not compile. The domain knows nothing about widgets, D-Bus or sound.
- **Default stations are never hardcoded** — they come from JSON catalogs (the bundled one is
  `data/catalogs/record.json`) and are merged with the user's local overlay; a catalog update
  never overwrites the user's edits.
- **All paths to user files live in `src/core/paths.cpp`** (XDG): every file the app writes has a
  function there. Modules compose names only inside a directory that module owns — the
  catalog cache names files by a hash of the source URL, and that is its business alone.
- **The app has two names on purpose.** `APP_ID` (`radio-player`) is the command and the directory
  under `~/.config`; `APP_RDNS_ID` (`io.github.simba77.RadioPlayer`) is how the system knows the
  app — `.desktop` file, icons, AppStream, D-Bus. Anything visible outside the process uses the
  reverse-DNS one: on Wayland the window finds its icon only when `setDesktopFileName` matches the
  entry exactly.
- **The catalog cache is a derivative of the source list**, keyed by URL: a catalog removed from
  the settings stops contributing stations, and its file is pruned.
- **Configuration files keep the format of versions 1.x** (Python): settings, the overlay and the
  state are read as they were written, so an upgrade is invisible.
