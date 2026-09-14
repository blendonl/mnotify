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
