# Changelog

All notable changes to mnotify are documented here. This project adheres to
[Semantic Versioning](https://semver.org/) (pre-1.0: minor = features + fixes).
Sections after 0.1.0 are generated from commit messages; see *Releases* in the
README.

## 0.4.0

### Added

- Redesign --history as a searchable panel with app icons (462f385)

  mnotify --history opened a plain menu at the pointer: 40 rows of
  "App - Title: body", no icons and no way to find an older notification.
  It now opens a panel in the notification corner, in the same palette as
  the popups.

  - Type to search. Every word has to appear in the app, title or text,
    ignoring case and accents. The header reads "N of M" while filtering,
    and Ctrl+Backspace deletes a word.
  - Each card shows the app's icon from its Start menu entry or
    executable, loaded one at a time once the panel is up and cached per
    app; apps without one get a coloured initial. The time reads
    "Just now", "12 min ago" or the clock time, and the text wraps to two
    lines ending in an ellipsis.
  - Notifications are grouped under Today, Yesterday and weekday headings,
    and up to 200 are listed instead of 40.
  - Up/Down, PgUp/PgDn, Ctrl+Home/End, hovering and the wheel move through
    them; Enter or a click opens one as the menu did. Esc clears the
    search, then closes the panel; clicking elsewhere closes it, and
    running mnotify --history again toggles it. --dismiss and --quit
    close it.
  - Cards and the search box have antialiased rounded corners, which GDI's
    RoundRect cannot draw. Scrolling, line breaking, search words and
    corner coverage live in history_list.c with host-side tests.

- Make the history panel follow the config (a4e9652)

  The panel had its own copy of the default colours and always opened in
  one of four corners. It now takes the same settings as the popups when
  it opens:

  - position.corner (including top-center and bottom-center),
    position.monitor and position.margin decide where it opens.
  - theme.bg, fg, dim, border and info colour it; the panel background,
    raised surfaces and muted text are shades between bg and fg, so light
    themes stay readable.
  - theme.font sets the typeface, theme.corners rounds or squares the
    window, cards, search box and keycaps, and theme.border_none drops
    the window border.

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
