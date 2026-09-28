---@meta

---@alias mnotify.Timeout integer|"forever"
---@alias mnotify.Color integer|string
---@alias mnotify.Corner "bottom-right"|"top-right"|"bottom-left"|"top-left"|"top-center"|"bottom-center"
---@alias mnotify.Animation "slide"|"fade"|"none"
---@alias mnotify.Kind "info"|"warn"|"error"

---@class mnotify.Behavior
---@field timeout? mnotify.Timeout How long a notification stays up, in ms, or "forever".
---@field long_timeout? mnotify.Timeout How long reminders, calls and mnotify's own notices stay up.
---@field pause_on_hover? boolean Keep a notification up while the pointer is on it.
---@field hover_grace? integer How long a notification stays after the pointer leaves it, in ms.
---@field max_visible? integer How many notifications fit on screen at once, 1 to 10.
---@field hold_when_busy? boolean Hold toasts back during fullscreen games, presentations and screensavers.
---@field catch_up? boolean Show toasts that arrived while mnotify was not running.

---@class mnotify.Position
---@field corner? mnotify.Corner Where notifications stack.
---@field monitor? "cursor"|"primary" Which monitor they appear on.
---@field margin? integer Distance from the screen edge, in px.
---@field spacing? integer Gap between stacked notifications, in px.

---@class mnotify.Theme
---@field bg? mnotify.Color Background.
---@field fg? mnotify.Color Title, and body text when there is no title.
---@field dim? mnotify.Color App name, and body text under a title.
---@field border? mnotify.Color|"none" Window border.
---@field info? mnotify.Color Accent of info notifications.
---@field warn? mnotify.Color Accent of warnings.
---@field error? mnotify.Color Accent of errors.
---@field opacity? integer 0 to 255.
---@field corners? "round"|"small"|"square" Window corners.
---@field font? string Font family.
---@field font_size? integer Body text size, in px.
---@field title_size? integer Title size, in px.
---@field app_size? integer App name size, in px.
---@field width? integer Notification width, in px.
---@field padding? integer Space inside the notification, in px.
---@field line_spacing? integer Gap between app name, title and body, in px.
---@field body_lines? integer Body lines shown before it is cut off.
---@field accent? "left"|"none" The coloured bar.
---@field accent_width? integer Width of the coloured bar, in px.

---@class mnotify.AnimationSettings
---@field open? mnotify.Animation How a notification appears.
---@field close? mnotify.Animation How a notification goes away.
---@field duration? integer Length of each animation in ms, 0 to 1000; 0 turns animation off.
---@field easing? "linear"|"ease_out"|"ease_in_out"

---@class mnotify.Notification
---@field app string
---@field title string
---@field text string
---@field kind mnotify.Kind
---@field source "toast"|"balloon"|"send"|"mnotify"
---@field aumid? string The app user model ID, for toasts.
---@field long boolean Whether the app asked for a long notification, such as a reminder or call.
---@field timeout mnotify.Timeout
---@field accent? mnotify.Color

---@class mnotify
mnotify = {}

mnotify.behavior = {}
---@param settings mnotify.Behavior
function mnotify.behavior.setup(settings) end

mnotify.position = {}
---@param settings mnotify.Position
function mnotify.position.setup(settings) end

mnotify.theme = {}
---@param settings mnotify.Theme
function mnotify.theme.setup(settings) end

mnotify.animation = {}
---@param settings mnotify.AnimationSettings
function mnotify.animation.setup(settings) end

mnotify.config = {}
---@param enabled boolean Reload when init.lua is saved.
function mnotify.config.auto_reload(enabled) end

mnotify.log = {}
---@param level "error"|"warn"|"info"|"debug"|"trace"
function mnotify.log.level(level) end

---@param event "notify"
---@param handler fun(n: mnotify.Notification): mnotify.Notification|false|nil
function mnotify.on(event, handler) end
