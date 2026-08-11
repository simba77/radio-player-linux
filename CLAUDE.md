# CLAUDE.md

## Development approach

Carried over from the macOS version of the project:

- **States, not just the happy path.** Every feature covers errors, loading and edge cases.
  Adding playback means adding `is_loading` and `error_message`; adding a list means adding
  an empty state.
- **State consistency.** If `is_playing == True`, something must actually be playing. Observed
  mpv state beats optimistic flags.
- **Resource lifecycle.** Every `observe_property`, `QTimer` and D-Bus object is torn down
  explicitly.
- **Fix bugs found along the way.** A bug noticed while working on a task gets fixed and
  mentioned in the summary.

## Language

Code comments and docstrings are in Russian; talk to the user in Russian too. Commit messages,
this file and `README.md` are in English.

User-facing strings are English literals inside `tr()`, translated through Qt Linguist —
`translations/*.ts`, compiled to `radio_player/resources/i18n/*.qm`, which are committed so
installing needs no Qt tooling. After touching any interface string run `make i18n-update`
(needs `make deps-i18n`) and `make i18n-release`; a test fails if the catalog drifts from the code.
Outside widgets use `QCoreApplication.translate("Context", "literal")` — lupdate extracts
statically and cannot see through a wrapper function. Console output is a log, not interface,
and stays in Russian untranslated.

## Commits

Conventional Commits: `feat(power): inhibit system sleep during playback`.

## Environment

The app runs on the system Python: PySide6 and python-mpv come from distro packages. Installing
packages globally is refused (PEP 668), so pytest and ruff live in a `.venv` created with
`--system-site-packages` — without that flag the system PySide6 and mpv are invisible inside it.
Build targets: `make help`.

Development happens on KDE neon (Plasma 6, Wayland). Also verify on X11 and GNOME. On GNOME
`QSystemTrayIcon` is unavailable without the AppIndicator extension, so the app must stay fully
usable with no tray.

## Traps

**libmpv crashes under a non-C numeric locale**, and Qt sets the system one. Call
`locale.setlocale(locale.LC_NUMERIC, "C")` before creating `mpv.MPV()`.

**Check D-Bus signatures with `busctl --user monitor`** — `dbus-monitor` does not show them.
Qt marshals an empty Python list as an array of variants (`av`) rather than strings (`as`), which
makes `PropertiesChanged` go out as `sa{sv}av`: properties still read fine and `dbus-monitor` shows
the signal, but plasmashell and kded silently ignore it — the player never appears in the
"Media Player" applet and never receives media keys.

**PySide6 does not infer D-Bus numeric types either.** A Python number is sent as `int32`, so
calling a method with signature `u` fails with `UnknownMethod`. The type can only be forced through
the interface metaobject: `QMetaObject.invokeMethod(interface, "UnInhibit", Q_ARG("uint", cookie))`.
And `asyncCall` takes only the method name — pass arguments via `asyncCallWithArgumentList`.

**System sleep must be inhibited explicitly** (`power.py`): neither libmpv nor an active MPRIS
player holds the system awake, and idle auto-suspend cuts the stream off.

## Decisions not visible from the code

- Data flow: `__main__` → `StationStore` → `PlayerManager` (owns the store) → UI via signals.
  Qt signals replace `@Published` from the macOS version.
- **Default stations are never hardcoded** — they come from JSON catalogs and are merged with the
  user's local overlay; a catalog update never overwrites the user's edits.
- **All paths to user files live in `paths.py`** (XDG). No other module builds paths.
