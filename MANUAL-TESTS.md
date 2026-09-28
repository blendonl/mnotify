# Manual test checklist

`make test` covers the logic with no Windows in it: parsing what
`Shell_NotifyIcon` sends, the icon table, and how callbacks and clicks are
packed. Everything below needs a Windows session **without Explorer** (mshell,
or any other replacement shell), because it involves the tray window, the
screen and other apps.

`%LOCALAPPDATA%\mnotify\mnotify.log` is written on every run and is the first
place to look. Start with `--log-level debug` to see every icon message.

A tray app to test against, in PowerShell (keep the window open while testing):

```powershell
Add-Type -AssemblyName System.Windows.Forms, System.Drawing
$n = New-Object System.Windows.Forms.NotifyIcon
$n.Icon = [System.Drawing.SystemIcons]::Information
$n.Text = 'mnotify test icon'
$n.add_BalloonTipClicked({ Write-Host 'balloon clicked' })
$n.add_BalloonTipClosed({ Write-Host 'balloon closed' })
$n.add_MouseClick({ param($s, $e) Write-Host "icon $($e.Button) click" })
$n.add_MouseDoubleClick({ Write-Host 'icon double click' })
$n.Visible = $true
$n.ShowBalloonTip(5000, 'Backup complete', '42 files copied', 'Warning')
while ($true) { [System.Windows.Forms.Application]::DoEvents(); Start-Sleep -Milliseconds 30 }
```

## Resident instance and the CLI

| # | Test | Expected |
|---|------|----------|
| 1 | `mnotify.exe` with nothing running. | Nothing appears. The log says it is hosting `Shell_TrayWnd`, and within a few seconds running tray apps (Steam, say) are logged re-adding their icons. |
| 2 | `mnotify.exe` again. | Exits at once. Task Manager shows one `mnotify.exe`. |
| 3 | `mnotify.exe --version` from a terminal. | Prints the version, and nothing else. |
| 4 | `mnotify.exe --nonsense`. | Prints the usage text; exit code 1. `--help` prints it with exit code 0. |
| 5 | `mnotify.exe --tray` with nothing running. | Prints that mnotify is not running; exit code 1. |
| 6 | `mnotify.exe --quit` with nothing running. | Exits quietly with code 0. |
| 7 | `mnotify.exe --quit` while running. | The process exits; the log ends with `mnotify: exiting`. |

## Notifications

| # | Test | Expected |
|---|------|----------|
| 1 | `mnotify --send "Build finished" "a line long enough to wrap onto a second line in the popup" --kind warn` | A dark notification in the bottom-right corner of the monitor under the cursor, above anything else, without taking focus: bold title, dimmer wrapped body, a yellow bar on the left. |
| 2 | `mnotify --send "Plain"` straight after. | A second notification, nearer the corner, pushing the first away from it. Each closes on its own after 6 s. |
| 3 | Send six in a row. | At most five on screen; the oldest goes first. |
| 4 | Hover one past its timeout. | It stays while the pointer is on it and closes about 1.5 s after the pointer leaves. |
| 5 | `mnotify --dismiss` with several up. | All close. |
| 6 | Start with `--corner top-left --timeout 2000`. | Notifications stack from the top-left and close after 2 s. On a multi-monitor setup, they appear on the monitor the cursor is on. |
| 7 | Run the test app above. | A notification naming *Windows PowerShell*, titled *Backup complete*. The console prints nothing until you act on it. |
| 8 | Left-click that notification. | It closes and the console prints `balloon clicked`. |
| 9 | Re-run the app, right-click the notification. | It closes and the console prints `balloon closed`. |
| 10 | Re-run the app, leave the notification alone. | After 6 s it closes and the console prints `balloon closed`. |
| 11 | Re-run the app; while the notification is up, stop the loop with `Ctrl+C` and run `$n.Dispose()`. | The notification closes with the icon. |

## Toasts

A toast to test with, in PowerShell:

```powershell
[void][Windows.UI.Notifications.ToastNotificationManager, Windows.UI.Notifications, ContentType = WindowsRuntime]
[void][Windows.Data.Xml.Dom.XmlDocument, Windows.Data.Xml.Dom.XmlDocument, ContentType = WindowsRuntime]
function Send-Toast($xml) {
    $doc = New-Object Windows.Data.Xml.Dom.XmlDocument; $doc.LoadXml($xml)
    $aumid = '{1AC14E77-02E7-4E5D-B744-2EB1AE5198B7}\WindowsPowerShell\v1.0\powershell.exe'
    [Windows.UI.Notifications.ToastNotificationManager]::CreateToastNotifier($aumid).Show(
        [Windows.UI.Notifications.ToastNotification]::new($doc))
}
Send-Toast '<toast><visual><binding template="ToastGeneric"><text>Build finished</text><text>42 tests passed</text><text placement="attribution">ci</text></binding></visual></toast>'
```

| # | Test | Expected |
|---|------|----------|
| 1 | Start `mnotify --log-level debug`. | The log says `toasts: watching the Windows notification store`. |
| 2 | Run the `Send-Toast` above. | Within half a second, a notification naming *Windows PowerShell · ci*, titled *Build finished*. |
| 3 | `Send-Toast` with `duration="long"` on `<toast>`. | It stays up for 25 s. |
| 4 | Send a message to yourself on Discord from another device, with Discord's window not focused. | A notification naming *Discord*. Left-click brings Discord's window forward, or restores it from the tray. |
| 5 | Get a notification from a site in Chrome. | A notification naming *Google Chrome · the site*. Left-click opens the site in Chrome. |
| 6 | Turn off banners for an app in Windows' notification settings, then have it notify. | Nothing appears; the debug log says banners are off for it. |
| 7 | Run a game in exclusive fullscreen and have Discord notify. | Nothing appears over the game. `SHQueryUserNotificationState` reports 3. |
| 8 | With `mnotify --quit`, send three toasts, then start mnotify again. | All three appear, oldest farthest from the corner. Restarting again shows nothing new. |
| 9 | Quit mnotify, send seven toasts, start it. | The newest four appear, with a note *3 more notifications* farthest from the corner. Clicking the note opens the history menu. |
| 10 | `mnotify --history`. | A menu at the pointer lists stored notifications newest first, *App — Title: body* with the arrival time on the right. `Esc` closes it and the window you were in has the keyboard again. |
| 11 | Pick a Chrome entry in that menu. | Chrome opens that page. |
| 12 | Hold presentation mode on (`presentationsettings /start`), send a toast, then turn it off. | Nothing appears while it is on. When it goes off, *1 notification while you were busy*. |
| 13 | Set `ToastEnabled` to `0` under `HKCU\Software\Microsoft\Windows\CurrentVersion\PushNotifications` and restart mnotify. | A yellow notification says Windows notifications are off. Delete the value afterwards. |

## Configuration

Edit `%APPDATA%\mnotify\init.lua` (copy `config\init.lua` there first if the
installer has not) while mnotify runs, and send notifications with
`mnotify --send "Test" "some body text"` to see each change.

| # | Test | Expected |
|---|------|----------|
| 1 | `mnotify --check` with the default config. | Prints `ok:` and the path; exit code 0. |
| 2 | Change `corner = "middle"`, then `mnotify --check`. | Prints `FAILED:` with `init.lua:<line>: … unknown corner 'middle' (bottom-right|…)`; exit code 1. |
| 3 | Save that broken file while mnotify runs. | A red *Config error, previous config kept* notification with the same message. New notifications still look as before. |
| 4 | Fix it to `corner = "top-center"` and save. | Within a second, new notifications stack from the top centre. No restart. |
| 5 | Set `theme.bg = "#303446"`, `opacity = 220`, `corners = "square"`, `font = "Consolas"`, `width = 460`, `accent = "none"` and save. | Notifications already on screen restyle at once; they are wider, square, see-through, in Consolas, with no bar. |
| 6 | With the default config, check the corners of a notification on Windows 11. | Rounded, with the grey border. |
| 7 | Set `behavior.timeout = "forever"`. | A notification stays until clicked, right-clicked or `mnotify --dismiss`. |
| 8 | With `max_visible = 5`, show five, then set `max_visible = 2`. | The three oldest close; two remain. |
| 9 | Default animations: send a notification, then right-click it. | It slides in a short way from the right edge while fading in, and fades out. The notifications behind it glide into place. |
| 10 | Set `open = "fade"`, `close = "slide"`, `duration = 600`, `easing = "linear"`. | Slow fade in; on close it slides back out toward the edge while fading. |
| 11 | Set `animation.duration = 0`. | Notifications appear and vanish at once, as before this feature. |
| 12 | Set `position.monitor = "primary"` on a multi-monitor setup. | Notifications appear on the primary monitor wherever the pointer is. |
| 13 | Add `mnotify.on("notify", function(n) if n.title == "drop" then return false end n.timeout = "forever" end)`. `mnotify --send drop`, then `mnotify --send keep`. | *drop* never appears; *keep* stays until clicked. |
| 14 | Hook that drops balloons: `if n.source == "balloon" then return false end`. Run the tray test app. | No notification. The console prints `balloon closed` once. |
| 15 | Hook with `error("boom")`. | Notifications still show, unchanged. The log has `notify hook failed … boom`. |
| 16 | Save `while true do end` as the config. | Within a second, *Config error, previous config kept … took too long*. mnotify and the tray keep working. |
| 17 | `mnotify.config.auto_reload(false)`, save, then edit something else and save. | The second edit does nothing until `mnotify --reload`. |
| 18 | `mnotify --reload` with mnotify not running. | Exits quietly with code 0; nothing starts. |
| 19 | Start with `--corner top-left --timeout 2000` and a config that says `bottom-right`. | Top-left, 2 s, and still so after saving the config again. |
| 20 | Delete `init.lua` while mnotify runs. | The defaults come back. |

## Tray menu

| # | Test | Expected |
|---|------|----------|
| 1 | Bind `mnotify --tray` to a key and press it. | A menu at the pointer listing every tray icon by tooltip (`mnotify test icon`, `Steam`, …). `Esc` or clicking elsewhere closes it. |
| 2 | *mnotify test icon* → *Click*. | The console prints `icon Left click`. |
| 3 | *Double-click*. | The console prints `icon double click`. |
| 4 | *Right-click* on a real app (Steam). | The app's own tray menu opens at the pointer and takes the keyboard. |
| 5 | Close an app that hid itself in the tray (Discord, Steam), then use *Click* or *Double-click* on its entry. | Its window comes back. |
| 6 | Kill a tray app from Task Manager, then open the menu. | Its entry is gone. |
| 7 | With nothing in the tray, open the menu. | A single greyed *No tray icons* entry. |
| 8 | `mnotify --dismiss` while the menu is open. | The menu closes. |

## Living alongside Explorer

| # | Test | Expected |
|---|------|----------|
| 1 | Start `mnotify.exe` in a normal Explorer session. | Refuses: prints that another tray is already running, exit code 1. Explorer's tray is untouched. |
| 2 | With mnotify running under mshell, trigger mshell's panic key (it starts Explorer). | Explorer comes up with its own taskbar and tray. mnotify logs `another tray appeared … stepping aside` and exits. Tray apps move their icons to Explorer's tray. |
