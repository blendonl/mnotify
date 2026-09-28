# Changelog

All notable changes to mnotify are documented here. This project adheres to
[Semantic Versioning](https://semver.org/) (pre-1.0: minor = features + fixes).
Sections after 0.1.0 are generated from commit messages; see *Releases* in the
README.

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
