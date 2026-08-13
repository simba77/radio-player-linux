# Manual testing across desktops

Automated tests run headless (`QT_QPA_PLATFORM=offscreen`) and cannot see a tray, a session bus
owned by a real shell, or a compositor. Everything below needs a live session, and each row must
be repeated per environment before a release.

Environments: **Plasma 6 Wayland** (primary), **Plasma 6 X11**, **GNOME** (Wayland; the tray is
expected to be missing without the AppIndicator extension).

Run the installed build, not `make run` — autostart, the desktop entry and icon lookup all depend
on files being where `make install` puts them:

```sh
make install && radio-player --log-level=debug
```

## 1. Window and icon

| # | Step | Expected |
|---|---|---|
| 1.1 | Launch the app | Window appears, station list populated |
| 1.2 | Look at the task switcher and the panel | The app's own icon, not a generic square — on Wayland this only works when `setDesktopFileName` matches the installed `io.github.simba77.RadioPlayer.desktop` |
| 1.3 | Find the app in the application menu | Listed under Audio/Multimedia, correct name and icon |
| 1.4 | Close the window | The app keeps running (tray) or quits, per the setting in "General" |
| 1.5 | Start a second copy from the terminal | No second window; the existing one is raised and focused. On Wayland raising needs the xdg-activation token — if focus is only "requested" (taskbar entry blinks), note it |
| 1.6 | `pkill -TERM -f 'python3 -m radio_player'` (the wrapper `exec`s Python, so the process is not named `radio-player`) | Exits cleanly, state saved, no inhibitor left behind (see 5.3) |

## 2. Tray

Plasma only; on GNOME confirm the *absence* is handled (2.4).

| # | Step | Expected |
|---|---|---|
| 2.1 | Start playback | Tray icon switches to the playing state |
| 2.2 | Break the network (turn off Wi-Fi) during playback | Icon blinks at ~600 ms while reconnecting; restore the network → playback resumes on its own |
| 2.3 | Tray menu: play/pause, next, previous, quit | All work; the menu reflects the current station |
| 2.3a | Minimize the window to the task bar, focus another app, click the tray icon | The window comes back and takes focus — it must not hide, and must not flash and vanish. On Wayland this depends on the activation token plasmashell sends before `Activate`; `--log-level=debug` prints `token=` on every tray click |
| 2.4 | GNOME without AppIndicator | No tray, and the app says so in the log; the window and MPRIS remain fully usable, closing the window does not strand the app invisibly |

## 3. MPRIS and media keys

| # | Step | Expected |
|---|---|---|
| 3.1 | `busctl --user list \| grep mpris` | `org.mpris.MediaPlayer2.io.github.simba77.RadioPlayer` present |
| 3.2 | Plasma: the "Media Player" applet in the panel | Shows the station, track and cover art; buttons control playback |
| 3.3 | GNOME: the media widget in the top-bar menu | Same |
| 3.4 | Media keys Play/Pause, Next, Previous | Control the app while it is the active player |
| 3.5 | `busctl --user monitor org.mpris.MediaPlayer2.io.github.simba77.RadioPlayer` while switching stations | `PropertiesChanged` with signature `sa{sv}as` — **not** `av`; `dbus-monitor` does not show signatures, so use `busctl` |
| 3.6 | Change the volume in the app | The applet's volume follows |

## 4. Playback and metadata

| # | Step | Expected |
|---|---|---|
| 4.1 | An HLS station (Radio Record) | Artist, title and cover art appear within ~5 s and change with the track |
| 4.2 | A direct Icecast stream | Artist and title from ICY |
| 4.3 | A station whose HLS manifest has no `#EXT-X-DATERANGE` | Plays fine, no track info, polling gives up instead of hammering the manifest |
| 4.4 | An unreachable URL | Error state, reconnect with backoff, no crash |
| 4.5 | Click the track title | Copied to the clipboard, "Copied" confirmation. Verify the clipboard survives (Wayland clipboard is owned by the app — paste into another window **before** quitting) |

## 5. System integration

| # | Step | Expected |
|---|---|---|
| 5.1 | Enable "Start on login", relog | The app starts; `~/.config/autostart/io.github.simba77.RadioPlayer.desktop` exists and its `Exec` works from any working directory |
| 5.2 | Disable it | The file is gone |
| 5.3 | During playback: `busctl --user call org.kde.Solid.PowerManagement.PolicyAgent /org/kde/Solid/PowerManagement/PolicyAgent org.kde.Solid.PowerManagement.PolicyAgent ListInhibitions` (Plasma) or `gnome-session-inhibit --list` (GNOME) | The app holds an inhibition while playing or reconnecting, and releases it on stop and on quit (the list updates a few seconds late) |
| 5.4 | Set the idle sleep timeout to its minimum and let the stream play | The machine does not suspend |
| 5.5 | Switch the system theme light ↔ dark | Icons and the muted text stay readable in both |

## 6. Packaging

| # | Step | Expected |
|---|---|---|
| 6.1 | `sudo dpkg -i dist/radio-player_*.deb` on a clean machine | Installs, pulls its dependencies, launches from the application menu |
| 6.2 | `make uninstall` | Binary, desktop entry, icons, metainfo and the autostart entry are all gone |

## Reporting

Record the environment (`$XDG_SESSION_TYPE`, `$XDG_CURRENT_DESKTOP`, distro, Qt version) with any
failure, plus the `--log-level=debug` output around it.
