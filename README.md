# mnotify

A **tray and notification host for Windows shells that replace Explorer.** It
shows the notifications Explorer would have drawn: the toasts Discord, Chrome,
WhatsApp and Outlook send, and the balloons older tray apps send. It also gives
you a menu of tray icons, so an app that hid itself in the tray can still be
reached.

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

## What it shows

Windows has two ways for an app to notify you, and a shell without Explorer
loses the screen for both:

- **Toast notifications** (WinRT / Windows App SDK). Windows still accepts and
  stores these without Explorer, in the per-user notification database
  (`%LOCALAPPDATA%\Microsoft\Windows\Notifications\wpndatabase.db`), but
  Explorer's shell is what draws them, so they never appear. **mnotify reads
  new toasts from that store** as they arrive (read-only, through Windows' own
  `winsqlite3.dll`) and shows them.
- **Tray balloons** (`Shell_NotifyIcon` with `NIF_INFO`). Apps send these to
  the window of class `Shell_TrayWnd`, which Explorer owns. With no Explorer
  there is no such window, and the balloon goes nowhere. **mnotify is that
  window**, and shows each balloon it receives.

Discord checks whether the shell will accept a notification before it sends
one, by asking `Shell_TrayWnd` (`SHQueryUserNotificationState`). mnotify
answers the way Explorer does, so Discord notifies again. The answer is "not
now" while a screensaver, presentation mode or an exclusive-fullscreen game is
running. mnotify holds back toasts at those times too, and when you are back it
tells you how many arrived.

In practice:

| App | Notifies through | Shown by mnotify |
|---|---|---|
| Discord, Chrome, Edge, WhatsApp, Outlook, Teams | Toasts | Yes |
| Older Win32 and WinForms tray utilities, backup tools, updaters | Balloons | Yes |
| Telegram Desktop, Steam | Their own popups | Not needed |

Toasts need Windows notifications to be on for your account. If
`HKCU\Software\Microsoft\Windows\CurrentVersion\PushNotifications\ToastEnabled`
is `0`, Windows drops every toast before storing it; mnotify warns about this
when it starts. Set it to `1` (or delete it), then sign out and back in.

## Usage

```
mnotify                         host the tray (nothing happens if it already is)
mnotify --send <title> [text]   show a notification
        --kind info|warn|error    its accent colour
mnotify --tray                  open a menu of tray icons at the cursor
mnotify --history               open or close the notification history; type to search
mnotify --dismiss               close every notification on screen
mnotify --reload                reload the config in the running instance
mnotify --check                 load the config, report errors and exit
mnotify --quit                  stop the running instance

Read when the host starts, overriding the config:
--corner <where>                bottom-right|top-right|bottom-left|top-left|
                                top-center|bottom-center
--timeout <ms>|forever          how long a notification stays up
--log-level <lvl>               error|warn|info|debug|trace

mnotify --version               print the version and exit
```

Everything else (how long notifications last, where they go, how they look and
move, and per-app rules) lives in a Lua config; see [Configuration](#configuration).

Start it once per session. It stays resident, asks running apps to re-register
their tray icons (the same `TaskbarCreated` broadcast Explorer sends), and from
then on:

- **A toast** becomes a notification in the chosen corner, naming the app and,
  for web notifications, the site. Left-click it to do what clicking the toast
  would have done: open its link, or hand the click to the app's registered
  toast handler (Chrome opens the page). Apps with no handler, such as Discord,
  are brought forward instead, through their tray icon if their window is
  hidden. Right-click dismisses it. Reminders and calls stay up for 25 s. Apps
  whose banners you turned off in Windows' notification settings stay quiet.
- **Toasts you missed** pop up when mnotify starts, the way Explorer shows them
  when it starts: the ones that arrived while mnotify was not running, such as
  overnight or before you signed in. It shows the newest four and a note saying
  how many more there are. Clicking the note opens the history. mnotify remembers
  the last toast it handled, so nothing is shown twice. On its first run, it
  catches up on everything Windows is holding.
- **`mnotify --history`** opens a panel in the chosen corner listing the
  notifications Windows is holding, newest first and grouped by day. Each shows
  the app's icon (or a coloured initial when the app has none), its name, when
  it arrived, the title and the first two lines of text. Windows keeps them for
  up to three days, 20 per app. Type to search: every word has to appear in the
  app, title or text, ignoring case and accents. `↑` `↓`, `PgUp` `PgDn`,
  `Ctrl+Home` `Ctrl+End` or the mouse pick one; `Enter` or a click does what
  clicking the notification would have done. `Esc` clears the search, then
  closes the panel; clicking elsewhere or running `mnotify --history` again
  closes it too.
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
mshell.keys.bind({"LWin", "LShift"}, "n",
    function() mshell.exec("mnotify.exe", "--history") end, { desc = "notification history" })
```

Its windows are tool windows, which mshell never tiles.

## Configuration

mnotify reads `%APPDATA%\mnotify\init.lua`, the way mshell reads
`%APPDATA%\mshell\init.lua`. If that file does not exist it falls back to
`config\init.lua` next to `mnotify.exe`, and with neither it uses its built-in
defaults. The installer puts the default config in `%APPDATA%\mnotify` and never
overwrites yours; when a release changes the default, it is written beside yours
as `init.lua.new`.

Saving the file reloads it: the look changes on screen without a restart. A
config with an error is rejected as a whole: mnotify keeps the previous one,
shows the error in a notification and logs it. `mnotify --check` loads it and
prints the error without touching the running instance, and `mnotify --reload`
reloads it on demand. For completion and type checking in an editor with the
Lua language server, open the `%APPDATA%\mnotify` folder; the installer puts
`.luarc.json` and `meta\mnotify.lua` there.

The default config, which sets every option to its default:

```lua
mnotify.config.auto_reload(true)
mnotify.log.level("info")

mnotify.behavior.setup {
    timeout        = 6000,
    long_timeout   = 25000,
    pause_on_hover = true,
    hover_grace    = 1500,
    max_visible    = 5,
    hold_when_busy = true,
    catch_up       = true,
}

mnotify.position.setup {
    corner  = "bottom-right",
    monitor = "cursor",
    margin  = 16,
    spacing = 10,
}

mnotify.theme.setup {
    bg     = 0x1e1e2e,
    fg     = 0xcdd6f4,
    dim    = 0xa6adc8,
    border = 0x45475a,
    info   = 0x89b4fa,
    warn   = 0xf9e2af,
    error  = 0xf38ba8,

    opacity = 255,
    corners = "round",

    font       = "Segoe UI",
    font_size  = 14,
    title_size = 15,
    app_size   = 12,

    width        = 360,
    padding      = 14,
    line_spacing = 3,
    body_lines   = 6,

    accent       = "left",
    accent_width = 3,
}

mnotify.animation.setup {
    open     = "slide",
    close    = "fade",
    duration = 200,
    easing   = "ease_out",
}

mnotify.on("notify", function(n)
    return n
end)
```

Each `setup` call only changes the settings it names, so a config can be as
short as `mnotify.behavior.setup { timeout = "forever" }`. An unknown setting
or a value out of range is an error, not silently ignored. Sizes are in pixels
at 100% scaling and grow with the monitor's scale. Colours are `0xRRGGBB`
numbers or `"#rrggbb"` strings.

| Setting | Values | Meaning |
|---|---|---|
| `behavior.timeout` | ms, or `"forever"` | How long a notification stays up. A `"forever"` one stays until you click it, right-click it or run `mnotify --dismiss`, or until `max_visible` pushes it out. |
| `behavior.long_timeout` | ms, or `"forever"` | The same for reminders and calls, and for mnotify's own notices. |
| `behavior.pause_on_hover` | boolean | Keep a notification up while the pointer is on it. |
| `behavior.hover_grace` | 0 to 60000 ms | How long it stays after the pointer leaves. |
| `behavior.max_visible` | 1 to 10 | How many are on screen at once; the oldest goes first. |
| `behavior.hold_when_busy` | boolean | Hold toasts back during a fullscreen game, presentation or screensaver, and say how many arrived afterwards. |
| `behavior.catch_up` | boolean | Show toasts that arrived while mnotify was not running. Read when mnotify starts. |
| `position.corner` | `bottom-right`, `top-right`, `bottom-left`, `top-left`, `top-center`, `bottom-center` | Where notifications stack. |
| `position.monitor` | `cursor`, `primary` | The monitor under the pointer, or the primary one. |
| `position.margin` | 0 to 1000 | Distance from the edge of the screen. |
| `position.spacing` | 0 to 500 | Gap between notifications. |
| `theme.bg`, `fg`, `dim` | colour | Background; title; app name and body text under a title. |
| `theme.border` | colour, or `"none"` | The window border. |
| `theme.info`, `warn`, `error` | colour | The accent bar for each kind. |
| `theme.opacity` | 0 to 255 | Opacity of the whole notification. |
| `theme.corners` | `round`, `small`, `square` | Window corners (Windows 11). |
| `theme.font` | font family | Used for all text. |
| `theme.font_size`, `title_size`, `app_size` | 6 to 96 | Text sizes of the body, title and app name. |
| `theme.width` | 120 to 2000 | Notification width. |
| `theme.padding` | 0 to 200 | Space inside the notification. |
| `theme.line_spacing` | 0 to 100 | Gap between app name, title and body. |
| `theme.body_lines` | 1 to 50 | Body lines shown before the text is cut off. |
| `theme.accent` | `left`, `none` | The coloured bar on the left, or none. |
| `theme.accent_width` | 1 to 50 | Width of the bar. |
| `animation.open` | `slide`, `fade`, `none` | How a notification appears. `slide` moves it in a short way from the screen edge while it fades in. |
| `animation.close` | `slide`, `fade`, `none` | How it goes away. |
| `animation.duration` | 0 to 1000 ms | Length of each animation; 0 turns animation off. Notifications moving up or down the stack animate too. |
| `animation.easing` | `linear`, `ease_out`, `ease_in_out` | The speed curve. |
| `config.auto_reload(b)` | boolean | Reload when the file is saved. |
| `log.level(l)` | `error`, `warn`, `info`, `debug`, `trace` | How much goes to `mnotify.log`. |

The history panel follows `position` and the theme's colours, `font`, `corners`
and `border`; it keeps its own sizes and is never see-through. Changes apply
the next time it opens. The tray menu is an ordinary Windows menu and follows
the Windows theme, not this one.

### Per-app rules

`mnotify.on("notify", fn)` runs `fn` for every notification before it is shown.
`n` has `app`, `title`, `text`, `kind` (`info`, `warn`, `error`), `source`
(`toast`, `balloon`, `send`, or `mnotify` for mnotify's own), `aumid` for
toasts, `long` (a reminder or call), and `timeout`. Change `app`, `title`,
`text`, `kind`, `timeout` or `accent` to change what is shown, or return
`false` to drop the notification:

```lua
mnotify.on("notify", function(n)
    if n.app == "Steam" then
        return false
    end
    if n.app == "Discord" then
        n.timeout = "forever"
    end
    if n.kind == "error" then
        n.accent = "#ff5555"
        n.timeout = 15000
    end
    return n
end)
```

A dropped balloon is reported to its app as timed out. If the function fails,
the error is logged and the notification is shown unchanged. The function
cannot call `setup`; those only work while `init.lua` loads.

## Build

Cross-compiled from Linux with **mingw-w64**. There are no dependencies to
fetch: Lua 5.4 is vendored in `vendor/lua` and linked in (see
[THIRD-PARTY-NOTICES.md](THIRD-PARTY-NOTICES.md)).

```sh
sudo apt install gcc-mingw-w64-x86-64
make            # -> mnotify.exe
make test       # host-side unit tests (tray protocol, toasts, config, animation)
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
