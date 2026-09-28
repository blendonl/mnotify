#ifndef MNOTIFY_LUA_API_H
#define MNOTIFY_LUA_API_H

#include <stddef.h>

#include <lua.h>

#include "config_types.h"

#define HOOK_APP_CAP    512
#define HOOK_TITLE_CAP  512
#define HOOK_TEXT_CAP   1024
#define HOOK_AUMID_CAP  1024

typedef enum {
    HOOK_SHOW = 0,
    HOOK_DROP,
    HOOK_FAILED,
} HookResult;

typedef struct {
    char        app[HOOK_APP_CAP];
    char        title[HOOK_TITLE_CAP];
    char        text[HOOK_TEXT_CAP];
    char        aumid[HOOK_AUMID_CAP];
    NoteKind    kind;
    const char *source;
    bool        long_duration;
    Timeout     timeout;
    bool        has_accent;
    uint32_t    accent;
} HookNote;

lua_State *lua_api_new(void);
bool       lua_api_run(lua_State *L, const char *chunk, size_t len, const char *name,
                       Config *cfg, char *err, size_t cap);
HookResult lua_api_run_hook(lua_State *L, int ref, HookNote *note, char *err, size_t cap);

#endif
