# mnotify

A **tray and notification host for Windows shells that replace Explorer.** It
takes the place of Explorer's tray window, so apps that report something through
a tray balloon get an on-screen notification again. It also gives you a menu of
their tray icons, so an app that hid itself in the tray can still be reached.

```
                                   ┌─────────────────────────────────┐
                                   │ ▌ Windows PowerShell            │
                                   │ ▌ Backup complete               │
                                   │ ▌ 42 files copied to D:\backup  │
                                   └─────────────────────────────────┘
```

It is a natural fit for [mshell](https://github.com/blendonl/mshell), a tiling WM
that replaces `explorer.exe`, and works under any other replacement shell. It
needs no window manager and depends on nothing in one.

## What it can and cannot catch

Windows has two ways for an app to notify you, and a shell without Explorer
loses both:

- **Tray balloons** (`Shell_NotifyIcon` with `NIF_INFO`). Apps send these to
  the window of class `Shell_TrayWnd`, which Explorer owns. With no Explorer
  there is no such window, and the balloon goes nowhere. **mnotify is that
  window**, and shows each balloon it receives.
- **Toast notifications** (WinRT / Windows App SDK). These are dropped by Windows
  itself before any program can see them. Without Explorer's shell running,
  every app's toast notifier reports `DisabledForUser` and nothing is stored,
  so there is nothing for a third-party program to read or intercept. **mnotify
  cannot show these, and neither can anything else** short of running Explorer.

In practice:

| App | Notifies through | Shown under a shell without Explorer |
|---|---|---|
| Older Win32 and WinForms tray utilities, backup tools, updaters | Balloons | Yes, by mnotify |
| Discord, Chrome, Edge, Outlook | Toasts | No |
| Teams (new) | Toasts, or its own popups with "Teams built-in" notifications | Only with the built-in style |
| Telegram Desktop, Steam | Their own popups | Yes, without mnotify |

Discord's message notifications go through toasts only, so they cannot be shown.
Turning on Discord's *taskbar flashing* makes its window ask for attention
instead, which a window manager can pick up (mshell: `mshell.appearance.urgency(true)`).

## Usage

```
mnotify                         host the tray (nothing happens if it already is)
mnotify --send <title> [text]   show a notification
        --kind info|warn|error    its accent colour
mnotify --tray                  open a menu of tray icons at the cursor
mnotify --dismiss               close every notification on screen
mnotify --quit                  stop the running instance

Read when the host starts:
--corner <where>                bottom-right|top-right|bottom-left|top-left
--timeout <ms>                  how long a notification stays up (6000)
--log-level <lvl>               error|warn|info|debug|trace

mnotify --version               print the version and exit
```

Start it once per session. It stays resident, asks running apps to re-register
their tray icons (the same `TaskbarCreated` broadcast Explorer sends), and from
then on:

- **A balloon** becomes a notification in the chosen corner, naming the app that
  sent it. Left-click it to do what clicking the balloon would have done (the
  app gets `NIN_BALLOONUSERCLICK`), right-click to dismiss it. Hovering keeps it
  up. An app that updates or removes its balloon updates or removes the
  notification.
- **`mnotify --tray`** opens a menu listing every visible tray icon by its
  tooltip. Each has *Click*, *Double-click* and *Right-click*, which send the
  icon exactly what the mouse would have. Right-click is usually the app's own
  tray menu; click or double-click usually brings its window back.
- **`mnotify --send`** raises a notification from a script, with no app behind it.

If Explorer is running, mnotify refuses to start: there is already a tray. If
Explorer starts while mnotify is running (a panic key, say), mnotify sees
Explorer announce its tray and exits, so the two never compete.

Everything is logged to `%LOCALAPPDATA%\mnotify\mnotify.log`; `--log-level debug`
records every icon an app adds, changes and removes.

### With mshell

mshell does not know about mnotify and needs no configuration for it. In
`init.lua`:

```lua
mshell.exec.startup("mnotify.exe")

mshell.keys.bind({"LWin"}, "n",
    function() mshell.exec("mnotify.exe", "--tray") end, { desc = "tray icons" })
```

Its windows are tool windows, which mshell never tiles.

## Build

Cross-compiled from Linux with **mingw-w64**. There are no dependencies to fetch:

```sh
sudo apt install gcc-mingw-w64-x86-64
make            # -> mnotify.exe
make test       # host-side unit tests (the tray protocol)
make dist       # -> dist/mnotify-<version>-win64.zip
```

## Releases

Every pull request merged into `main` is a release. While the PR is open, CI
works out the next version from its
[Conventional Commits](https://www.conventionalcommits.org/) and pushes a
`chore(release): vX.Y.Z` commit to the branch that sets `VERSION` in the
Makefile and writes the matching section of `CHANGELOG.md`. When the PR merges,
CI tags that version and publishes the zip with that section as its notes.

Before 1.0, `feat`, `fix` and breaking changes (`feat!:`, or a
`BREAKING CHANGE:` footer) bump the minor version. From 1.0, breaking changes
bump the major, `feat` the minor and `fix` the patch. Anything else bumps the
patch.

Pull after the bot commits before pushing to the branch again, or run
`make bump` and commit the result yourself so there is nothing for it to add.
Commits pushed straight to `main` are not released on their own; they go out
with the next merged PR.

## Install

In PowerShell:

```powershell
irm https://raw.githubusercontent.com/blendonl/mnotify/main/install.ps1 | iex
```

That downloads the latest release into `%LOCALAPPDATA%\Programs\mnotify` and
adds the folder to your user `PATH`. Run it again to upgrade; a copy running
from that folder is stopped for the upgrade and started again. To pin a
version, or install somewhere else:

```powershell
& ([scriptblock]::Create((irm https://raw.githubusercontent.com/blendonl/mnotify/main/install.ps1))) -Version 0.1.0 -InstallDir C:\Tools\mnotify
```

## License

MIT. See [LICENSE](LICENSE).
