# Manual test checklist

`make test` covers the logic with no Windows in it: parsing what
`Shell_NotifyIcon` sends, the icon table, how callbacks and clicks are packed,
reading toast XML, and the history panel's scrolling, search words, line
breaking and rounded corners. Everything below needs a Windows session
**without Explorer** (mshell, or any other replacement shell), because it
involves the tray window, the screen and other apps.

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
| 9 | Quit mnotify, send seven toasts, start it. | The newest four appear, with a note *3 more notifications* farthest from the corner. Clicking the note opens the history panel. |
| 10 | `mnotify --history`. | The history panel opens in the notification corner with the stored notifications newest first; see *Notification history* below. `Esc` closes it and the window you were in has the keyboard again. |
| 11 | Pick a Chrome entry in the panel. | Chrome opens that page. |
| 12 | Hold presentation mode on (`presentationsettings /start`), send a toast, then turn it off. | Nothing appears while it is on. When it goes off, *1 notification while you were busy*. |
| 13 | Set `ToastEnabled` to `0` under `HKCU\Software\Microsoft\Windows\CurrentVersion\PushNotifications` and restart mnotify. | A yellow notification says Windows notifications are off. Delete the value afterwards. |

## Notification history

Send a few toasts from different apps first (Discord, Chrome, the `Send-Toast`
above), or use the ones Windows is already holding.

| # | Test | Expected |
|---|------|----------|
| 1 | `mnotify --history`. | A dark panel in the notification corner of the monitor under the pointer, with the keyboard in its search box. *Notifications* and a count at the top, then cards under *Today*, *Yesterday* and weekday headings, newest first. The first card is highlighted. |
| 2 | Look at the cards. | Each has the app's icon, its name (*Google Chrome · site* for web notifications), the time on the right (*Just now*, *12 min ago*, or the clock time), the title in bold and up to two lines of text ending in *…* when there is more. An app without a Start menu entry (a `Windows.SystemToast.*` sender) shows a coloured initial instead of an icon. |
| 3 | Type `discord`. | Only Discord's notifications remain, the count reads *N of M*, the search box is outlined in blue and an *×* appears in it. |
| 4 | Type `DISCÖRD alice` instead. | The same matches as `discord alice`: case and accents are ignored, and every word must match. |
| 5 | Type `zzzz`. | *No matches* with the query quoted, and the footer offers *Esc Clear*. |
| 6 | Press `Ctrl+Backspace`. | The last word of the search is deleted, with no box character left behind. |
| 7 | Press `Esc`, then `Esc` again. | The first clears the search and brings every notification back; the second closes the panel and gives the keyboard back to the window that had it. |
| 8 | Open it, then use `↓`, `↑`, `PgDn`, `PgUp`, `Ctrl+End`, `Ctrl+Home`. | The highlight moves, and the list scrolls to keep it, and its day heading, in view. |
| 9 | Move the pointer over the cards, then scroll the wheel. | The card under the pointer is highlighted; the wheel scrolls and a thin scroll thumb on the right tracks it. |
| 10 | Click a card, or highlight one and press `Enter`. | The panel closes and the notification is opened as if its popup had been clicked. |
| 11 | Open it, then click another window. | The panel closes. |
| 12 | Open it, then run `mnotify --history` again. | The panel closes (the key binding toggles it). `mnotify --dismiss` also closes it. |
| 13 | Start with `--corner top-left`, open the panel. | It opens in the top-left corner of the monitor under the pointer. |
| 14 | Open it on a monitor at 150 % scaling. | Text, icons and spacing are scaled, and icons are sharp. |
| 15 | Open it with no stored notifications. | *No notifications*, with a bell. |

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
