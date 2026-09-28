# Changelog

All notable changes to mnotify are documented here. This project adheres to
[Semantic Versioning](https://semver.org/) (pre-1.0: minor = features + fixes).
Sections after 0.1.0 are generated from commit messages; see *Releases* in the
README.

## 0.3.0

### Added

- Configure mnotify with a Lua config (c3128b7)

  mnotify reads %APPDATA%\mnotify\init.lua, falling back to config\init.lua
  next to the exe, the way mshell finds its own. Every setting that was
  hard-coded is now in it, and saving the file reloads it.

  - behavior: timeout and long_timeout in ms or "forever", pause on hover
    and its grace, max_visible (up to 10), holding toasts while busy, and
    catching up on missed toasts.
  - position: the four corners plus top-center and bottom-center, the
    monitor under the cursor or the primary one, margin and spacing.
  - theme: every colour, opacity, corner style, border, font and sizes,
    width, padding, line spacing, body lines and the accent bar.
  - animation: slide, fade or none to open and close, duration, easing;
    notifications glide into place when the stack changes.
  - mnotify.on("notify", fn) edits or drops each notification. A failing
    hook shows it unchanged; endless loops in the hook or the config are
    cut off.
  - A config with an error is rejected whole: the previous one stays and an
    error notification says why. mnotify --check reports it without the
    running instance, and mnotify --reload reloads on demand.
  - --corner, --timeout (now also "forever") and --log-level still work and
    override the config.
  - The installer puts the default config, .luarc.json and LuaLS types in
    %APPDATA%\mnotify, never overwriting an existing init.lua.

### Other

- **build:** Vendor Lua 5.4.7 (b82b9bd)

  The same PUC Lua sources mshell vendors, so both read their config with
  the same interpreter. THIRD-PARTY-NOTICES.md carries Lua's MIT license.

## 0.2.0

### Added

- **tray:** Answer the user notification state query (606548a)

  SHQueryUserNotificationState sends 0x4EF to Shell_TrayWnd and returns the
  reply, defaulting to QUNS_BUSY. mnotify let DefWindowProc answer 0, so
  Discord and other apps that check the state dropped every notification.
  Answer like Explorer: not present during the screen saver, presentation
  mode and exclusive fullscreen when active, accepts notifications otherwise.

- Show Windows toast notifications without Explorer (280c516)

  Windows keeps accepting toasts when Explorer is not the shell: it stores
  every one in the per-user notification database, and only the drawing is
  missing. mnotify now watches that store and shows new toasts.

  - Poll the store's data_version every 500 ms through the system's
    winsqlite3.dll, read-only, and parse each new toast's XML for its title,
    body, attribution and launch arguments. Toasts stored before mnotify
    starts are not replayed.
  - Name the app from its Start menu entry, and respect the per-app banner
    setting and the notification state mnotify already reports to apps.
  - Left-click does what Windows would: open a protocol link, or hand the
    click to the app's toast activator, found in its AppUserModelId
    registration or Start menu shortcut. Apps without one are brought
    forward through their window or tray icon; packaged apps are started.
  - Warn at startup when ToastEnabled = 0 has turned toasts off for the
    user, since Windows then drops them before storing them.

- Catch up on missed toasts and list them with --history (28d5cf9)

  Explorer shows the toasts it never displayed when it starts; mnotify now
  does the same. It remembers the last toast it handled (HKCU\Software\mnotify
  LastToastId) and on startup shows the ones that arrived since: the newest
  four and a note saying how many more. The first run catches up on
  everything Windows is holding.

  - Toasts held back during a screensaver, presentation mode or exclusive
    fullscreen are summed up in one note when the user is back.
  - mnotify --history opens a menu of the stored notifications, newest
    first with their arrival time; choosing one acts like clicking it.
    Clicking a summary note opens the same menu.
  - Closing the tray or history menu without choosing gives the keyboard
    back to the window that had it.

## 0.1.0

### Added

- Host the tray on a shell without Explorer. mnotify registers the
  `Shell_TrayWnd` window that `Shell_NotifyIcon` talks to, broadcasts
  `TaskbarCreated` so running apps re-register their icons, and keeps track of
  every icon they add, change and remove.
- Show tray balloons as notifications, naming the app that sent them. Clicking
  one sends the app `NIN_BALLOONUSERCLICK`, right-clicking dismisses it, and the
  app hears `NIN_BALLOONSHOW` and `NIN_BALLOONTIMEOUT` as it would from Explorer.
- `--tray` opens a menu of tray icons with *Click*, *Double-click* and
  *Right-click*, so apps that hide in the tray stay reachable.
- `--send`, `--dismiss` and `--quit` for scripts and key bindings; `--corner`,
  `--timeout` and `--log-level` when starting.
- Refuse to start when Explorer's tray exists, and exit when one appears.
