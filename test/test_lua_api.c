#include "tests.h"
#include "lua_api.h"

#include <stdlib.h>

#include <lauxlib.h>

static lua_State *L;
static Config     cfg;
static char       err[512];

static bool load(const char *chunk) {
    if (L) lua_close(L);
    L = lua_api_new();
    err[0] = '\0';
    return lua_api_run(L, chunk, strlen(chunk), "=test", &cfg, err, sizeof err);
}

static bool error_mentions(const char *needle) {
    return strstr(err, needle) != NULL;
}

static char *read_file(const char *path, size_t *len) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    rewind(f);
    char *buf = malloc((size_t)size + 1);
    *len = fread(buf, 1, (size_t)size, f);
    buf[*len] = '\0';
    fclose(f);
    return buf;
}

static void test_defaults(void) {
    CHECK(load(""), "an empty config loads: %s", err);
    CHECK(cfg.behavior.timeout.ms == 6000 && !cfg.behavior.timeout.forever, "default timeout is 6 s");
    CHECK(cfg.behavior.long_timeout.ms == 25000, "default long timeout is 25 s");
    CHECK(cfg.behavior.max_visible == 5, "five notifications fit by default");
    CHECK(cfg.position.corner == CORNER_BOTTOM_RIGHT, "bottom-right by default");
    CHECK(cfg.theme.bg == 0x1e1e2e && cfg.theme.info == 0x89b4fa, "default palette");
    CHECK(strcmp(cfg.theme.font, "Segoe UI") == 0, "Segoe UI by default");
    CHECK(cfg.animation.open == ANIM_SLIDE && cfg.animation.close == ANIM_FADE, "slide in, fade out");
    CHECK(cfg.auto_reload && cfg.log_level == CONFIG_LOG_INFO, "auto reload and info logging");
    CHECK(cfg.notify_ref == LUA_NOREF, "no hook by default");
}

static void test_behavior(void) {
    bool ok = load("mnotify.behavior.setup { timeout = 'forever', long_timeout = 60000,"
                   " pause_on_hover = false, hover_grace = 0, max_visible = 10,"
                   " hold_when_busy = false, catch_up = false }");
    CHECK(ok, "behavior loads: %s", err);
    CHECK(cfg.behavior.timeout.forever, "timeout can be forever");
    CHECK(!cfg.behavior.long_timeout.forever && cfg.behavior.long_timeout.ms == 60000, "long timeout in ms");
    CHECK(!cfg.behavior.pause_on_hover && cfg.behavior.hover_grace == 0, "hover settings");
    CHECK(cfg.behavior.max_visible == 10, "max_visible");
    CHECK(!cfg.behavior.hold_when_busy && !cfg.behavior.catch_up, "busy and catch-up switches");

    CHECK(load("mnotify.behavior.setup { long_timeout = 'forever' }") && cfg.behavior.long_timeout.forever,
          "long timeout can be forever");
    CHECK(load("mnotify.behavior.setup { timeout = 1 }") && cfg.behavior.timeout.ms == 1, "1 ms is allowed");
}

static void test_position(void) {
    CHECK(load("mnotify.position.setup { corner = 'top-center', monitor = 'primary', margin = 0, spacing = 4 }"),
          "position loads: %s", err);
    CHECK(cfg.position.corner == CORNER_TOP_CENTER, "top-center");
    CHECK(cfg.position.monitor == MONITOR_PRIMARY, "primary monitor");
    CHECK(cfg.position.margin == 0 && cfg.position.spacing == 4, "margin and spacing");
    CHECK(load("mnotify.position.setup { corner = 'bottom-center' }") && cfg.position.corner == CORNER_BOTTOM_CENTER,
          "bottom-center");
}

static void test_theme(void) {
    bool ok = load("mnotify.theme.setup { bg = 0x000000, fg = '#FFFFFF', dim = '#a0b0c0', border = 'none',"
                   " error = 0xff5555, opacity = 200, corners = 'square', font = 'Cascadia Code',"
                   " font_size = 16, title_size = 18, app_size = 11, width = 420, padding = 20,"
                   " line_spacing = 5, body_lines = 3, accent = 'none', accent_width = 6 }");
    CHECK(ok, "theme loads: %s", err);
    CHECK(cfg.theme.bg == 0x000000 && cfg.theme.fg == 0xffffff && cfg.theme.dim == 0xa0b0c0,
          "number and hex string colours");
    CHECK(cfg.theme.border_none, "border can be none");
    CHECK(cfg.theme.error == 0xff5555 && cfg.theme.warn == 0xf9e2af, "unset colours keep their defaults");
    CHECK(cfg.theme.opacity == 200 && cfg.theme.corners == CORNERS_SQUARE, "opacity and corners");
    CHECK(strcmp(cfg.theme.font, "Cascadia Code") == 0, "font family");
    CHECK(cfg.theme.font_size == 16 && cfg.theme.title_size == 18 && cfg.theme.app_size == 11, "font sizes");
    CHECK(cfg.theme.width == 420 && cfg.theme.padding == 20 && cfg.theme.line_spacing == 5 &&
          cfg.theme.body_lines == 3, "sizes");
    CHECK(cfg.theme.accent == ACCENT_NONE && cfg.theme.accent_width == 6, "accent");

    CHECK(load("mnotify.theme.setup { border = 'none' } mnotify.theme.setup { border = 0x123456 }") &&
          !cfg.theme.border_none && cfg.theme.border == 0x123456, "a colour turns the border back on");
}

static void test_animation(void) {
    CHECK(load("mnotify.animation.setup { open = 'fade', close = 'slide', duration = 0, easing = 'ease_in_out' }"),
          "animation loads: %s", err);
    CHECK(cfg.animation.open == ANIM_FADE && cfg.animation.close == ANIM_SLIDE, "open and close");
    CHECK(cfg.animation.duration == 0 && cfg.animation.easing == EASE_IN_OUT, "duration and easing");
    CHECK(load("mnotify.animation.setup { open = 'none', easing = 'linear' }") &&
          cfg.animation.open == ANIM_NONE && cfg.animation.easing == EASE_LINEAR, "none and linear");
}

static void test_setup_merges(void) {
    CHECK(load("mnotify.theme.setup { width = 400 } mnotify.theme.setup { padding = 8 }"), "two setups: %s", err);
    CHECK(cfg.theme.width == 400 && cfg.theme.padding == 8, "later setup calls merge onto earlier ones");
}

static void test_misc(void) {
    CHECK(load("mnotify.config.auto_reload(false) mnotify.log.level('debug')"), "misc loads: %s", err);
    CHECK(!cfg.auto_reload, "auto reload can be turned off");
    CHECK(cfg.log_level == CONFIG_LOG_DEBUG, "log level");
}

static void test_rejects(void) {
    CHECK(!load("mnotify.theme.setup { bgg = 0 }") && error_mentions("unknown setting 'bgg'"),
          "unknown settings are rejected: %s", err);
    CHECK(!load("mnotify.position.setup { corner = 'middle' }") && error_mentions("top-center"),
          "unknown choices list the valid ones: %s", err);
    CHECK(!load("mnotify.behavior.setup { max_visible = 11 }") && error_mentions("1 to 10"),
          "out of range numbers are rejected: %s", err);
    CHECK(!load("mnotify.behavior.setup { timeout = 0 }") && error_mentions("forever"),
          "a zero timeout is rejected: %s", err);
    CHECK(!load("mnotify.behavior.setup { timeout = 1.5 }"), "fractional timeouts are rejected");
    CHECK(!load("mnotify.behavior.setup { timeout = 'never' }"), "unknown timeout words are rejected");
    CHECK(!load("mnotify.theme.setup { bg = '#12345' }") && error_mentions("colour"),
          "short hex colours are rejected: %s", err);
    CHECK(!load("mnotify.theme.setup { bg = 0x1000000 }"), "colours above 0xFFFFFF are rejected");
    CHECK(!load("mnotify.theme.setup { font = '' }"), "an empty font is rejected");
    CHECK(!load("mnotify.behavior.setup { catch_up = 1 }") && error_mentions("true or false"),
          "booleans must be booleans: %s", err);
    CHECK(!load("mnotify.theme.setup(5)") && error_mentions("expects a table"), "setup needs a table: %s", err);
    CHECK(!load("mnotify.on('click', function() end)") && error_mentions("unknown event"),
          "unknown events are rejected: %s", err);
    CHECK(!load("mnotify.on('notify', 5)") && error_mentions("function"), "hooks must be functions: %s", err);
    CHECK(!load("mnotify.log.level('loud')") && error_mentions("error|warn|info|debug|trace"),
          "unknown log levels are rejected: %s", err);
    CHECK(!load("this is not lua"), "syntax errors are reported");
    CHECK(!load("mnotify.theme.setup { bgg = 0 }") && error_mentions("test:1:"),
          "errors point at the line: %s", err);
}

static void test_endless_loop(void) {
    CHECK(!load("while true do end") && error_mentions("took too long"),
          "an endless loop in the config is stopped: %s", err);
}

static HookNote sample(void) {
    HookNote n;
    memset(&n, 0, sizeof n);
    snprintf(n.app,   sizeof n.app,   "Discord");
    snprintf(n.title, sizeof n.title, "hello");
    snprintf(n.text,  sizeof n.text,  "world");
    n.kind       = NOTE_INFO;
    n.source     = "toast";
    n.timeout.ms = 6000;
    return n;
}

static HookResult hook(const char *config, HookNote *n) {
    if (!load(config)) return HOOK_FAILED;
    err[0] = '\0';
    return lua_api_run_hook(L, cfg.notify_ref, n, err, sizeof err);
}

static void test_hook(void) {
    HookNote n = sample();
    CHECK(load("") && lua_api_run_hook(L, cfg.notify_ref, &n, err, sizeof err) == HOOK_SHOW,
          "no hook shows everything");

    n = sample();
    CHECK(hook("mnotify.on('notify', function(n) if n.app == 'Discord' then return false end end)", &n)
          == HOOK_DROP, "returning false drops the notification");

    n = sample();
    HookResult r = hook("mnotify.on('notify', function(n)"
                        " n.timeout = 'forever'; n.kind = 'error'; n.accent = '#ff5555';"
                        " n.title = n.title .. '!'; n.text = nil end)", &n);
    CHECK(r == HOOK_SHOW, "editing n in place shows it: %s", err);
    CHECK(n.timeout.forever, "the hook can make it stay forever");
    CHECK(n.kind == NOTE_ERROR, "the hook can change the kind");
    CHECK(n.has_accent && n.accent == 0xff5555, "the hook can set an accent");
    CHECK(strcmp(n.title, "hello!") == 0, "the hook can change the title");
    CHECK(n.text[0] == '\0', "clearing a field empties it");

    n = sample();
    r = hook("mnotify.on('notify', function(n) return { app = 'x', title = 'y', text = 'z', timeout = 100 } end)",
             &n);
    CHECK(r == HOOK_SHOW && strcmp(n.app, "x") == 0 && n.timeout.ms == 100 && !n.timeout.forever,
          "a returned table replaces the notification: %s", err);

    n = sample();
    r = hook("mnotify.on('notify', function(n) return n.source == 'toast' and n.timeout == 6000"
             " and n.kind == 'info' and n.long == false end)", &n);
    CHECK(r == HOOK_SHOW, "the hook sees source, timeout, kind and long: %s", err);

    n = sample();
    r = hook("mnotify.on('notify', function(n) n.timeout = -1 end)", &n);
    CHECK(r == HOOK_FAILED && error_mentions("n.timeout"), "bad edits fail the hook: %s", err);
    CHECK(strcmp(n.title, "hello") == 0 && n.timeout.ms == 6000, "a failed hook leaves the notification alone");

    n = sample();
    r = hook("mnotify.on('notify', function(n) error('boom') end)", &n);
    CHECK(r == HOOK_FAILED && error_mentions("boom"), "runtime errors are reported: %s", err);

    n = sample();
    r = hook("mnotify.on('notify', function(n) while true do end end)", &n);
    CHECK(r == HOOK_FAILED && error_mentions("took too long"), "an endless hook is stopped: %s", err);

    n = sample();
    r = hook("mnotify.on('notify', function(n) mnotify.theme.setup { width = 500 } end)", &n);
    CHECK(r == HOOK_FAILED && error_mentions("only be called while init.lua loads"),
          "setup is refused outside loading: %s", err);

    n = sample();
    r = hook("mnotify.on('notify', function(n) n.title = string.rep('\xc3\xa9', 400) end)", &n);
    size_t len = strlen(n.title);
    CHECK(r == HOOK_SHOW && len < HOOK_TITLE_CAP && len % 2 == 0,
          "long titles are cut on a character boundary (%zu bytes)", len);
}

static void test_shipped_config(void) {
    size_t len  = 0;
    char  *text = read_file("config/init.lua", &len);
    CHECK(text != NULL, "config/init.lua is readable");
    if (!text) return;

    if (L) lua_close(L);
    L = lua_api_new();
    bool ok = lua_api_run(L, text, len, "@init.lua", &cfg, err, sizeof err);
    free(text);
    CHECK(ok, "the shipped config loads: %s", err);
    CHECK(cfg.notify_ref != LUA_NOREF, "the shipped config registers a hook");

    Config defaults;
    config_defaults(&defaults);
    cfg.notify_ref = defaults.notify_ref;
    CHECK(memcmp(&cfg, &defaults, sizeof cfg) == 0, "the shipped config matches the built-in defaults");
}

int main(void) {
    test_defaults();
    test_behavior();
    test_position();
    test_theme();
    test_animation();
    test_setup_merges();
    test_misc();
    test_rejects();
    test_endless_loop();
    test_hook();
    test_shipped_config();
    if (L) lua_close(L);
    return tests_report("lua_api");
}
