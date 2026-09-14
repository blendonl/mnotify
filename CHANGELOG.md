# Changelog

All notable changes to mnotify are documented here. This project adheres to
[Semantic Versioning](https://semver.org/) (pre-1.0: minor = features + fixes).
Sections after 0.1.0 are generated from commit messages; see *Releases* in the
README.

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
