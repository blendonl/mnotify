#include "lua_api.h"

#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include <lauxlib.h>
#include <lualib.h>

#define LOADING_KEY        "mnotify.loading"
#define BUDGET_STEP        10000
#define LOAD_BUDGET_STEPS  5000
#define HOOK_BUDGET_STEPS  500
#define TIMEOUT_MAX_MS     86400000
#define CHOICE_LIST_CAP    160

_Static_assert(sizeof(Corner) == sizeof(int), "choice fields are written as int");

typedef struct {
    const char *name;
    int         value;
} Choice;

typedef enum {
    FIELD_INT,
    FIELD_BOOL,
    FIELD_COLOR,
    FIELD_BORDER,
    FIELD_CHOICE,
    FIELD_TIMEOUT,
    FIELD_FONT,
} FieldKind;

typedef struct {
    const char   *name;
    FieldKind     kind;
    size_t        offset;
    int           lo;
    int           hi;
    const Choice *choices;
} Field;

typedef struct {
    const char  *name;
    const Field *fields;
} Group;

typedef struct {
    int         ref;
    HookNote   *note;
    HookResult  result;
} HookCall;

static const Choice CORNERS[] = {
    { "bottom-right",  CORNER_BOTTOM_RIGHT  },
    { "top-right",     CORNER_TOP_RIGHT     },
    { "bottom-left",   CORNER_BOTTOM_LEFT   },
    { "top-left",      CORNER_TOP_LEFT      },
    { "top-center",    CORNER_TOP_CENTER    },
    { "bottom-center", CORNER_BOTTOM_CENTER },
    { NULL, 0 },
};

static const Choice MONITORS[] = {
    { "cursor",  MONITOR_CURSOR  },
    { "primary", MONITOR_PRIMARY },
    { NULL, 0 },
};

static const Choice CORNER_STYLES[] = {
    { "round",  CORNERS_ROUND  },
    { "small",  CORNERS_SMALL  },
    { "square", CORNERS_SQUARE },
    { NULL, 0 },
};

static const Choice ACCENTS[] = {
    { "left", ACCENT_LEFT },
    { "none", ACCENT_NONE },
    { NULL, 0 },
};

static const Choice ANIMATIONS[] = {
    { "slide", ANIM_SLIDE },
    { "fade",  ANIM_FADE  },
    { "none",  ANIM_NONE  },
    { NULL, 0 },
};

static const Choice EASINGS[] = {
    { "linear",      EASE_LINEAR },
    { "ease_out",    EASE_OUT    },
    { "ease_in_out", EASE_IN_OUT },
    { NULL, 0 },
};

static const Choice LOG_LEVELS[] = {
    { "error", CONFIG_LOG_ERROR },
    { "warn",  CONFIG_LOG_WARN  },
    { "info",  CONFIG_LOG_INFO  },
    { "debug", CONFIG_LOG_DEBUG },
    { "trace", CONFIG_LOG_TRACE },
    { NULL, 0 },
};

static const Choice KINDS[] = {
    { "info",  NOTE_INFO  },
    { "warn",  NOTE_WARN  },
    { "error", NOTE_ERROR },
    { NULL, 0 },
};

static const Field BEHAVIOR_FIELDS[] = {
    { "timeout",        FIELD_TIMEOUT, offsetof(Config, behavior.timeout),        0, 0, NULL },
    { "long_timeout",   FIELD_TIMEOUT, offsetof(Config, behavior.long_timeout),   0, 0, NULL },
    { "pause_on_hover", FIELD_BOOL,    offsetof(Config, behavior.pause_on_hover), 0, 0, NULL },
    { "hover_grace",    FIELD_INT,     offsetof(Config, behavior.hover_grace),    0, 60000, NULL },
    { "max_visible",    FIELD_INT,     offsetof(Config, behavior.max_visible),    1, CONFIG_MAX_VISIBLE, NULL },
    { "hold_when_busy", FIELD_BOOL,    offsetof(Config, behavior.hold_when_busy), 0, 0, NULL },
    { "catch_up",       FIELD_BOOL,    offsetof(Config, behavior.catch_up),       0, 0, NULL },
    { NULL, 0, 0, 0, 0, NULL },
};

static const Field POSITION_FIELDS[] = {
    { "corner",  FIELD_CHOICE, offsetof(Config, position.corner),  0, 0,    CORNERS  },
    { "monitor", FIELD_CHOICE, offsetof(Config, position.monitor), 0, 0,    MONITORS },
    { "margin",  FIELD_INT,    offsetof(Config, position.margin),  0, 1000, NULL     },
    { "spacing", FIELD_INT,    offsetof(Config, position.spacing), 0, 500,  NULL     },
    { NULL, 0, 0, 0, 0, NULL },
};

static const Field THEME_FIELDS[] = {
    { "bg",           FIELD_COLOR,  offsetof(Config, theme.bg),           0, 0, NULL },
    { "fg",           FIELD_COLOR,  offsetof(Config, theme.fg),           0, 0, NULL },
    { "dim",          FIELD_COLOR,  offsetof(Config, theme.dim),          0, 0, NULL },
    { "border",       FIELD_BORDER, offsetof(Config, theme.border),       0, 0, NULL },
    { "info",         FIELD_COLOR,  offsetof(Config, theme.info),         0, 0, NULL },
    { "warn",         FIELD_COLOR,  offsetof(Config, theme.warn),         0, 0, NULL },
    { "error",        FIELD_COLOR,  offsetof(Config, theme.error),        0, 0, NULL },
    { "opacity",      FIELD_INT,    offsetof(Config, theme.opacity),      0, 255, NULL },
    { "corners",      FIELD_CHOICE, offsetof(Config, theme.corners),      0, 0, CORNER_STYLES },
    { "font",         FIELD_FONT,   offsetof(Config, theme.font),         0, 0, NULL },
    { "font_size",    FIELD_INT,    offsetof(Config, theme.font_size),    6, 96, NULL },
    { "title_size",   FIELD_INT,    offsetof(Config, theme.title_size),   6, 96, NULL },
    { "app_size",     FIELD_INT,    offsetof(Config, theme.app_size),     6, 96, NULL },
    { "width",        FIELD_INT,    offsetof(Config, theme.width),        120, 2000, NULL },
    { "padding",      FIELD_INT,    offsetof(Config, theme.padding),      0, 200, NULL },
    { "line_spacing", FIELD_INT,    offsetof(Config, theme.line_spacing), 0, 100, NULL },
    { "body_lines",   FIELD_INT,    offsetof(Config, theme.body_lines),   1, 50, NULL },
    { "accent",       FIELD_CHOICE, offsetof(Config, theme.accent),       0, 0, ACCENTS },
    { "accent_width", FIELD_INT,    offsetof(Config, theme.accent_width), 1, 50, NULL },
    { NULL, 0, 0, 0, 0, NULL },
};

static const Field ANIMATION_FIELDS[] = {
    { "open",     FIELD_CHOICE, offsetof(Config, animation.open),     0, 0,    ANIMATIONS },
    { "close",    FIELD_CHOICE, offsetof(Config, animation.close),    0, 0,    ANIMATIONS },
    { "duration", FIELD_INT,    offsetof(Config, animation.duration), 0, 1000, NULL       },
    { "easing",   FIELD_CHOICE, offsetof(Config, animation.easing),   0, 0,    EASINGS    },
    { NULL, 0, 0, 0, 0, NULL },
};

static const Group GROUPS[] = {
    { "behavior",  BEHAVIOR_FIELDS  },
    { "position",  POSITION_FIELDS  },
    { "theme",     THEME_FIELDS     },
    { "animation", ANIMATION_FIELDS },
};

static int s_budget;

void config_defaults(Config *cfg) {
    memset(cfg, 0, sizeof *cfg);

    cfg->behavior.timeout.ms        = 6000;
    cfg->behavior.long_timeout.ms   = 25000;
    cfg->behavior.pause_on_hover    = true;
    cfg->behavior.hover_grace       = 1500;
    cfg->behavior.max_visible       = 5;
    cfg->behavior.hold_when_busy    = true;
    cfg->behavior.catch_up          = true;

    cfg->position.corner            = CORNER_BOTTOM_RIGHT;
    cfg->position.monitor           = MONITOR_CURSOR;
    cfg->position.margin            = 16;
    cfg->position.spacing           = 10;

    cfg->theme.bg                   = 0x1e1e2e;
    cfg->theme.fg                   = 0xcdd6f4;
    cfg->theme.dim                  = 0xa6adc8;
    cfg->theme.border               = 0x45475a;
    cfg->theme.info                 = 0x89b4fa;
    cfg->theme.warn                 = 0xf9e2af;
    cfg->theme.error                = 0xf38ba8;
    cfg->theme.opacity              = 255;
    cfg->theme.corners              = CORNERS_ROUND;
    snprintf(cfg->theme.font, CONFIG_FONT_CAP, "%s", "Segoe UI");
    cfg->theme.font_size            = 14;
    cfg->theme.title_size           = 15;
    cfg->theme.app_size             = 12;
    cfg->theme.width                = 360;
    cfg->theme.padding              = 14;
    cfg->theme.line_spacing         = 3;
    cfg->theme.body_lines           = 6;
    cfg->theme.accent               = ACCENT_LEFT;
    cfg->theme.accent_width         = 3;

    cfg->animation.open             = ANIM_SLIDE;
    cfg->animation.close            = ANIM_FADE;
    cfg->animation.duration         = 200;
    cfg->animation.easing           = EASE_OUT;

    cfg->log_level                  = CONFIG_LOG_INFO;
    cfg->auto_reload                = true;
    cfg->notify_ref                 = LUA_NOREF;
}

static void copy_utf8(char *out, size_t cap, const char *src, size_t len) {
    if (!cap) return;
    if (len >= cap) {
        len = cap - 1;
        while (len && ((unsigned char)src[len] & 0xC0) == 0x80) len--;
    }
    memcpy(out, src, len);
    out[len] = '\0';
}

static void copy_error(lua_State *L, char *err, size_t cap) {
    if (!err || !cap) return;
    const char *msg = lua_tostring(L, -1);
    snprintf(err, cap, "%s", msg ? msg : "error object is not a string");
}

static void budget_hook(lua_State *L, lua_Debug *ar) {
    (void)ar;
    if (--s_budget <= 0) luaL_error(L, "took too long; is there an endless loop?");
}

static int call_limited(lua_State *L, int nargs, int nresults, int steps) {
    s_budget = steps;
    lua_sethook(L, budget_hook, LUA_MASKCOUNT, BUDGET_STEP);
    int rc = lua_pcall(L, nargs, nresults, 0);
    lua_sethook(L, NULL, 0, 0);
    return rc;
}

static void choice_list(const Choice *choices, char *out, size_t cap) {
    size_t used = 0;
    out[0] = '\0';
    for (const Choice *c = choices; c->name; c++) {
        int w = snprintf(out + used, cap - used, "%s%s", used ? "|" : "", c->name);
        if (w < 0 || (size_t)w >= cap - used) break;
        used += (size_t)w;
    }
}

static const char *choice_name(const Choice *choices, int value) {
    for (const Choice *c = choices; c->name; c++)
        if (c->value == value) return c->name;
    return choices[0].name;
}

static int read_choice(lua_State *L, int idx, const char *what, const char *field,
                       const Choice *choices) {
    const char *name = lua_type(L, idx) == LUA_TSTRING ? lua_tostring(L, idx) : NULL;
    if (name) {
        for (const Choice *c = choices; c->name; c++)
            if (strcmp(c->name, name) == 0) return c->value;
    }

    char list[CHOICE_LIST_CAP];
    choice_list(choices, list, sizeof list);
    if (!name) return luaL_error(L, "%s: %s must be one of %s", what, field, list);
    return luaL_error(L, "%s: unknown %s '%s' (%s)", what, field, name, list);
}

static bool read_whole(lua_State *L, int idx, lua_Integer *out) {
    if (lua_type(L, idx) != LUA_TNUMBER) return false;
    int ok = 0;
    *out = lua_tointegerx(L, idx, &ok);
    return ok;
}

static bool read_color(lua_State *L, int idx, uint32_t *out) {
    lua_Integer n = 0;
    if (read_whole(L, idx, &n)) {
        if (n < 0 || n > 0xFFFFFF) return false;
        *out = (uint32_t)n;
        return true;
    }

    if (lua_type(L, idx) != LUA_TSTRING) return false;
    size_t len = 0;
    const char *s = lua_tolstring(L, idx, &len);
    if (len != 7 || s[0] != '#') return false;

    uint32_t value = 0;
    for (size_t i = 1; i < len; i++) {
        char c = s[i];
        int digit = c >= '0' && c <= '9' ? c - '0'
                  : c >= 'a' && c <= 'f' ? c - 'a' + 10
                  : c >= 'A' && c <= 'F' ? c - 'A' + 10
                  : -1;
        if (digit < 0) return false;
        value = (value << 4) | (uint32_t)digit;
    }
    *out = value;
    return true;
}

static bool read_timeout(lua_State *L, int idx, Timeout *out) {
    if (lua_type(L, idx) == LUA_TSTRING && strcmp(lua_tostring(L, idx), "forever") == 0) {
        out->forever = true;
        out->ms      = 0;
        return true;
    }

    lua_Integer n = 0;
    if (!read_whole(L, idx, &n) || n < 1 || n > TIMEOUT_MAX_MS) return false;
    out->forever = false;
    out->ms      = (int)n;
    return true;
}

static void read_field(lua_State *L, int idx, const char *what, const Field *f, Config *cfg) {
    void *dst = (char *)cfg + f->offset;

    switch (f->kind) {
    case FIELD_INT: {
        lua_Integer n = 0;
        if (!read_whole(L, idx, &n) || n < f->lo || n > f->hi)
            luaL_error(L, "%s: %s must be a whole number from %d to %d", what, f->name, f->lo, f->hi);
        *(int *)dst = (int)n;
        break;
    }
    case FIELD_BOOL:
        if (!lua_isboolean(L, idx)) luaL_error(L, "%s: %s must be true or false", what, f->name);
        *(bool *)dst = lua_toboolean(L, idx);
        break;
    case FIELD_COLOR:
        if (!read_color(L, idx, (uint32_t *)dst))
            luaL_error(L, "%s: %s must be a colour, 0xRRGGBB or \"#rrggbb\"", what, f->name);
        break;
    case FIELD_BORDER:
        if (lua_type(L, idx) == LUA_TSTRING && strcmp(lua_tostring(L, idx), "none") == 0) {
            cfg->theme.border_none = true;
            break;
        }
        if (!read_color(L, idx, (uint32_t *)dst))
            luaL_error(L, "%s: %s must be a colour, 0xRRGGBB or \"#rrggbb\", or \"none\"", what, f->name);
        cfg->theme.border_none = false;
        break;
    case FIELD_CHOICE:
        *(int *)dst = read_choice(L, idx, what, f->name, f->choices);
        break;
    case FIELD_TIMEOUT:
        if (!read_timeout(L, idx, (Timeout *)dst))
            luaL_error(L, "%s: %s must be a whole number of ms from 1 to %d, or \"forever\"",
                       what, f->name, TIMEOUT_MAX_MS);
        break;
    case FIELD_FONT: {
        size_t len = 0;
        const char *s = lua_type(L, idx) == LUA_TSTRING ? lua_tolstring(L, idx, &len) : NULL;
        if (!s || !len || len >= CONFIG_FONT_CAP)
            luaL_error(L, "%s: %s must be a font name of 1 to %d bytes", what, f->name, CONFIG_FONT_CAP - 1);
        memcpy(dst, s, len + 1);
        break;
    }
    }
}

static Config *loading_config(lua_State *L, const char *what) {
    lua_getfield(L, LUA_REGISTRYINDEX, LOADING_KEY);
    Config *cfg = (Config *)lua_touserdata(L, -1);
    lua_pop(L, 1);
    if (!cfg) luaL_error(L, "%s can only be called while init.lua loads", what);
    return cfg;
}

static const Field *find_field(const Field *fields, const char *name) {
    for (const Field *f = fields; f->name; f++)
        if (strcmp(f->name, name) == 0) return f;
    return NULL;
}

static int l_setup(lua_State *L) {
    const Group *group = (const Group *)lua_touserdata(L, lua_upvalueindex(1));
    char what[48];
    snprintf(what, sizeof what, "mnotify.%s.setup", group->name);

    Config *cfg = loading_config(L, what);
    if (!lua_istable(L, 1)) return luaL_error(L, "%s: expects a table of settings", what);

    lua_pushnil(L);
    while (lua_next(L, 1)) {
        if (lua_type(L, -2) != LUA_TSTRING) return luaL_error(L, "%s: setting names must be strings", what);
        const char  *key   = lua_tostring(L, -2);
        const Field *field = find_field(group->fields, key);
        if (!field) return luaL_error(L, "%s: unknown setting '%s'", what, key);
        read_field(L, lua_gettop(L), what, field, cfg);
        lua_pop(L, 1);
    }
    return 0;
}

static int l_auto_reload(lua_State *L) {
    Config *cfg = loading_config(L, "mnotify.config.auto_reload");
    if (!lua_isboolean(L, 1)) return luaL_error(L, "mnotify.config.auto_reload: expects true or false");
    cfg->auto_reload = lua_toboolean(L, 1);
    return 0;
}

static int l_log_level(lua_State *L) {
    Config *cfg = loading_config(L, "mnotify.log.level");
    cfg->log_level = (ConfigLogLevel)read_choice(L, 1, "mnotify.log.level", "level", LOG_LEVELS);
    return 0;
}

static int l_on(lua_State *L) {
    Config *cfg = loading_config(L, "mnotify.on");
    const char *event = lua_type(L, 1) == LUA_TSTRING ? lua_tostring(L, 1) : NULL;
    if (!event || strcmp(event, "notify") != 0)
        return luaL_error(L, "mnotify.on: unknown event '%s' (notify)", event ? event : "?");
    if (!lua_isfunction(L, 2)) return luaL_error(L, "mnotify.on: expects a function after the event name");

    if (cfg->notify_ref != LUA_NOREF) luaL_unref(L, LUA_REGISTRYINDEX, cfg->notify_ref);
    lua_pushvalue(L, 2);
    cfg->notify_ref = luaL_ref(L, LUA_REGISTRYINDEX);
    return 0;
}

static void register_api(lua_State *L) {
    lua_newtable(L);

    for (size_t i = 0; i < sizeof GROUPS / sizeof GROUPS[0]; i++) {
        lua_newtable(L);
        lua_pushlightuserdata(L, (void *)&GROUPS[i]);
        lua_pushcclosure(L, l_setup, 1);
        lua_setfield(L, -2, "setup");
        lua_setfield(L, -2, GROUPS[i].name);
    }

    lua_newtable(L);
    lua_pushcfunction(L, l_auto_reload);
    lua_setfield(L, -2, "auto_reload");
    lua_setfield(L, -2, "config");

    lua_newtable(L);
    lua_pushcfunction(L, l_log_level);
    lua_setfield(L, -2, "level");
    lua_setfield(L, -2, "log");

    lua_pushcfunction(L, l_on);
    lua_setfield(L, -2, "on");

    lua_setglobal(L, "mnotify");
}

lua_State *lua_api_new(void) {
    lua_State *L = luaL_newstate();
    if (!L) return NULL;
    luaL_openlibs(L);
    register_api(L);
    return L;
}

bool lua_api_run(lua_State *L, const char *chunk, size_t len, const char *name,
                 Config *cfg, char *err, size_t cap) {
    config_defaults(cfg);

    lua_pushlightuserdata(L, cfg);
    lua_setfield(L, LUA_REGISTRYINDEX, LOADING_KEY);

    int rc = luaL_loadbufferx(L, chunk, len, name, "t");
    if (rc == LUA_OK) rc = call_limited(L, 0, 0, LOAD_BUDGET_STEPS);

    lua_pushnil(L);
    lua_setfield(L, LUA_REGISTRYINDEX, LOADING_KEY);

    if (rc == LUA_OK) return true;
    copy_error(L, err, cap);
    lua_pop(L, 1);
    return false;
}

static void set_string(lua_State *L, int t, const char *key, const char *value) {
    lua_pushstring(L, value);
    lua_setfield(L, t, key);
}

static void read_hook_string(lua_State *L, int t, const char *key, char *out, size_t cap) {
    lua_getfield(L, t, key);
    int type = lua_type(L, -1);
    if (type == LUA_TNIL) {
        out[0] = '\0';
    } else if (type == LUA_TSTRING || type == LUA_TNUMBER) {
        size_t len = 0;
        const char *s = lua_tolstring(L, -1, &len);
        copy_utf8(out, cap, s, len);
    } else {
        luaL_error(L, "notify hook: n.%s must be a string", key);
    }
    lua_pop(L, 1);
}

static int hook_bridge(lua_State *L) {
    HookCall *call = (HookCall *)lua_touserdata(L, 1);
    HookNote *note = call->note;

    lua_createtable(L, 0, 8);
    int t = lua_gettop(L);
    set_string(L, t, "app",    note->app);
    set_string(L, t, "title",  note->title);
    set_string(L, t, "text",   note->text);
    set_string(L, t, "kind",   choice_name(KINDS, note->kind));
    set_string(L, t, "source", note->source ? note->source : "send");
    if (note->aumid[0]) set_string(L, t, "aumid", note->aumid);
    lua_pushboolean(L, note->long_duration);
    lua_setfield(L, t, "long");
    if (note->timeout.forever) lua_pushliteral(L, "forever");
    else                       lua_pushinteger(L, note->timeout.ms);
    lua_setfield(L, t, "timeout");

    lua_rawgeti(L, LUA_REGISTRYINDEX, call->ref);
    lua_pushvalue(L, t);
    lua_call(L, 1, 1);

    if (lua_isboolean(L, -1) && !lua_toboolean(L, -1)) {
        call->result = HOOK_DROP;
        return 0;
    }
    int from = lua_istable(L, -1) ? lua_gettop(L) : t;

    HookNote edited = *note;
    read_hook_string(L, from, "app",   edited.app,   sizeof edited.app);
    read_hook_string(L, from, "title", edited.title, sizeof edited.title);
    read_hook_string(L, from, "text",  edited.text,  sizeof edited.text);

    lua_getfield(L, from, "kind");
    if (!lua_isnil(L, -1))
        edited.kind = (NoteKind)read_choice(L, -1, "notify hook", "n.kind", KINDS);
    lua_pop(L, 1);

    lua_getfield(L, from, "timeout");
    if (!lua_isnil(L, -1) && !read_timeout(L, -1, &edited.timeout))
        luaL_error(L, "notify hook: n.timeout must be a whole number of ms from 1 to %d, or \"forever\"",
                   TIMEOUT_MAX_MS);
    lua_pop(L, 1);

    lua_getfield(L, from, "accent");
    edited.has_accent = !lua_isnil(L, -1);
    if (edited.has_accent && !read_color(L, -1, &edited.accent))
        luaL_error(L, "notify hook: n.accent must be a colour, 0xRRGGBB or \"#rrggbb\"");
    lua_pop(L, 1);

    *note = edited;
    call->result = HOOK_SHOW;
    return 0;
}

HookResult lua_api_run_hook(lua_State *L, int ref, HookNote *note, char *err, size_t cap) {
    if (!L || ref == LUA_NOREF || ref == LUA_REFNIL) return HOOK_SHOW;

    HookCall call = { .ref = ref, .note = note, .result = HOOK_SHOW };
    int top = lua_gettop(L);

    lua_pushcfunction(L, hook_bridge);
    lua_pushlightuserdata(L, &call);
    if (call_limited(L, 1, 0, HOOK_BUDGET_STEPS) != LUA_OK) {
        copy_error(L, err, cap);
        lua_settop(L, top);
        return HOOK_FAILED;
    }

    lua_settop(L, top);
    return call.result;
}
