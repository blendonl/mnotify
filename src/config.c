#include "mnotify.h"
#include "lua_api.h"

#include <errno.h>
#include <shlobj.h>

#include <lauxlib.h>

#define CONFIG_CHUNK_NAME    "@init.lua"
#define CONFIG_ERROR_CAP     512
#define CONFIG_DEBOUNCE_MS   250
#define CONFIG_DEBOUNCE_MAX  2000
#define CONFIG_SETTLE_MS     100
#define CONFIG_SETTLE_MAX    2000
#define WATCH_BUF_SIZE       4096
#define WATCH_STOP_WAIT_MS   2000

_Static_assert(HOOK_APP_CAP   >= MNOTIFY_APP_CAP   * 3, "hook app buffer holds any UTF-8 app name");
_Static_assert(HOOK_TITLE_CAP >= MNOTIFY_TITLE_CAP * 3, "hook title buffer holds any UTF-8 title");
_Static_assert(HOOK_TEXT_CAP  >= MNOTIFY_TEXT_CAP  * 3, "hook text buffer holds any UTF-8 text");
_Static_assert(HOOK_AUMID_CAP >= MNOTIFY_AUMID_CAP * 3, "hook aumid buffer holds any UTF-8 AUMID");

typedef struct {
    wchar_t  dir[MAX_PATH];
    wchar_t  file[MAX_PATH];
    unsigned generation;
    HANDLE   stop;
    HWND     target;
} WatchArgs;

typedef enum {
    BATCH_STOP,
    BATCH_NONE,
    BATCH_RELEVANT,
} BatchResult;

static lua_State *s_lua;
static wchar_t    s_path[MAX_PATH];
static HANDLE     s_watch_thread;
static HANDLE     s_watch_stop;
static wchar_t    s_watch_dir[MAX_PATH];
static unsigned   s_watch_generation;

static bool file_exists(const wchar_t *path) {
    DWORD attr = GetFileAttributesW(path);
    return attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY);
}

static bool dir_of(const wchar_t *path, wchar_t *out, size_t cap) {
    const wchar_t *slash = wcsrchr(path, L'\\');
    if (!slash || slash == path) return false;

    size_t len = (size_t)(slash - path);
    if (len >= cap) return false;
    memcpy(out, path, len * sizeof(wchar_t));
    out[len] = L'\0';
    return true;
}

static bool appdata_config_path(wchar_t *out, size_t cap) {
    PWSTR roaming = NULL;
    bool  ok = false;

    if (SUCCEEDED(SHGetKnownFolderPath(&FOLDERID_RoamingAppData, 0, NULL, &roaming))) {
        int n = _snwprintf(out, cap, L"%ls\\mnotify\\init.lua", roaming);
        ok = n > 0 && (size_t)n < cap;
        CoTaskMemFree(roaming);
    }

    if (!ok) {
        const wchar_t *env = _wgetenv(L"APPDATA");
        if (env && env[0]) {
            int n = _snwprintf(out, cap, L"%ls\\mnotify\\init.lua", env);
            ok = n > 0 && (size_t)n < cap;
        }
    }
    if (!ok) out[0] = L'\0';
    return ok;
}

static bool portable_config_path(wchar_t *out, size_t cap) {
    wchar_t exe_dir[MAX_PATH];
    DWORD   n = GetModuleFileNameW(NULL, exe_dir, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) return false;
    if (!dir_of(exe_dir, exe_dir, MAX_PATH)) return false;

    int w = _snwprintf(out, cap, L"%ls\\config\\init.lua", exe_dir);
    return w > 0 && (size_t)w < cap;
}

static void resolve_config_path(wchar_t *out, size_t cap) {
    wchar_t appdata[MAX_PATH];
    bool    have_appdata = appdata_config_path(appdata, MAX_PATH);
    if (have_appdata && file_exists(appdata)) {
        mnotify_copy_w(out, cap, appdata);
        return;
    }

    wchar_t portable[MAX_PATH];
    if (portable_config_path(portable, MAX_PATH) && file_exists(portable)) {
        mnotify_copy_w(out, cap, portable);
        return;
    }

    mnotify_copy_w(out, cap, have_appdata ? appdata : L"config\\init.lua");
}

static void set_package_path(lua_State *L, const wchar_t *config_path) {
    wchar_t dir[MAX_PATH];
    char    u8[MAX_PATH * 3];
    if (!dir_of(config_path, dir, MAX_PATH) || !mnotify_wide_to_utf8(dir, u8, sizeof u8)) return;

    lua_getglobal(L, "package");
    if (!lua_istable(L, -1)) {
        lua_pop(L, 1);
        return;
    }

    lua_getfield(L, -1, "path");
    const char *existing = lua_tostring(L, -1);
    lua_pushfstring(L, "%s\\?.lua;%s\\?\\init.lua;%s", u8, u8, existing ? existing : "");
    lua_setfield(L, -3, "path");
    lua_pop(L, 2);
}

static char *read_file(const wchar_t *path, size_t *len, char *err, size_t cap) {
    FILE *f = _wfopen(path, L"rb");
    if (!f) {
        snprintf(err, cap, "cannot open init.lua: %s", strerror(errno));
        return NULL;
    }

    long size = -1;
    if (fseek(f, 0, SEEK_END) == 0) size = ftell(f);
    if (size < 0) {
        snprintf(err, cap, "cannot read init.lua: %s", strerror(errno));
        fclose(f);
        return NULL;
    }
    rewind(f);

    char *buf = (char *)malloc((size_t)size + 1);
    if (!buf) {
        snprintf(err, cap, "out of memory reading init.lua");
        fclose(f);
        return NULL;
    }

    *len = fread(buf, 1, (size_t)size, f);
    buf[*len] = '\0';
    fclose(f);
    return buf;
}

static bool load(const wchar_t *path, lua_State **out_lua, Config *out, char *err, size_t cap) {
    *out_lua = NULL;
    if (!file_exists(path)) {
        config_defaults(out);
        return true;
    }

    size_t len = 0;
    char  *buf = read_file(path, &len, err, cap);
    if (!buf) return false;

    lua_State *L = lua_api_new();
    if (!L) {
        snprintf(err, cap, "out of memory creating the Lua state");
        free(buf);
        return false;
    }
    set_package_path(L, path);

    const char *src = buf;
    if (len >= 3 && (unsigned char)buf[0] == 0xEF && (unsigned char)buf[1] == 0xBB &&
                    (unsigned char)buf[2] == 0xBF) {
        src += 3;
        len -= 3;
    }

    bool ok = lua_api_run(L, src, len, CONFIG_CHUNK_NAME, out, err, cap);
    free(buf);
    if (!ok) {
        lua_close(L);
        return false;
    }
    *out_lua = L;
    return true;
}

static void apply_overrides(void) {
    const Overrides *o = &mn.overrides;
    if (o->timeout_set) mn.cfg.behavior.timeout = o->timeout;
    if (o->corner_set) mn.cfg.position.corner = o->corner;
    log_set_level(o->level_set ? o->level : (LogLevel)mn.cfg.log_level);
}

static void adopt(lua_State *L, const Config *cfg) {
    if (s_lua) lua_close(s_lua);
    s_lua  = L;
    mn.cfg = *cfg;
    mn.cfg_generation++;
    apply_overrides();
}

static void report_failure(const wchar_t *title, const char *err) {
    Note note;
    memset(&note, 0, sizeof note);
    note.kind          = NOTE_ERROR;
    note.source        = NOTE_FROM_MNOTIFY;
    note.long_duration = true;
    note.internal      = true;
    mnotify_copy_w(note.app,   MNOTIFY_APP_CAP,   L"mnotify");
    mnotify_copy_w(note.title, MNOTIFY_TITLE_CAP, title);
    mnotify_utf8_to_wide(err, note.text, MNOTIFY_TEXT_CAP);

    log_err(L"config: %ls: %ls", title, note.text);
    popup_show(&note);
}

static BatchResult watch_next_batch(const WatchArgs *wa, HANDLE dir, OVERLAPPED *ov, BYTE *buf, DWORD ms) {
    ResetEvent(ov->hEvent);
    if (!ReadDirectoryChangesW(dir, buf, WATCH_BUF_SIZE, FALSE,
                               FILE_NOTIFY_CHANGE_LAST_WRITE | FILE_NOTIFY_CHANGE_FILE_NAME |
                               FILE_NOTIFY_CHANGE_SIZE,
                               NULL, ov, NULL))
        return BATCH_STOP;

    HANDLE waits[2] = { wa->stop, ov->hEvent };
    DWORD  r = WaitForMultipleObjects(2, waits, FALSE, ms);
    if (r == WAIT_TIMEOUT) return BATCH_NONE;

    DWORD bytes = 0;
    if (r != WAIT_OBJECT_0 + 1) {
        CancelIoEx(dir, ov);
        GetOverlappedResult(dir, ov, &bytes, TRUE);
        return BATCH_STOP;
    }
    if (!GetOverlappedResult(dir, ov, &bytes, FALSE))
        return GetLastError() == ERROR_NOTIFY_ENUM_DIR ? BATCH_RELEVANT : BATCH_STOP;
    return BATCH_RELEVANT;
}

static bool watch_debounce(const WatchArgs *wa, HANDLE dir, OVERLAPPED *ov, BYTE *buf) {
    for (unsigned waited = 0; waited < CONFIG_DEBOUNCE_MAX; waited += CONFIG_DEBOUNCE_MS) {
        BatchResult b = watch_next_batch(wa, dir, ov, buf, CONFIG_DEBOUNCE_MS);
        if (b == BATCH_STOP) return false;
        if (b == BATCH_NONE) return true;
    }
    return true;
}

static bool file_stamp(const wchar_t *path, LONGLONG *size, FILETIME *mtime) {
    HANDLE h = CreateFileW(path, FILE_READ_ATTRIBUTES,
                           FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                           NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return false;

    LARGE_INTEGER li;
    bool ok = GetFileSizeEx(h, &li) && GetFileTime(h, NULL, NULL, mtime);
    CloseHandle(h);
    if (!ok) return false;
    *size = li.QuadPart;
    return true;
}

static bool watch_settle(const WatchArgs *wa) {
    LONGLONG prev_size = 0, size = 0;
    FILETIME prev_mtime = {0}, mtime = {0};
    bool     have_prev = file_stamp(wa->file, &prev_size, &prev_mtime);

    for (unsigned waited = 0; waited < CONFIG_SETTLE_MAX; waited += CONFIG_SETTLE_MS) {
        if (WaitForSingleObject(wa->stop, CONFIG_SETTLE_MS) == WAIT_OBJECT_0) return false;

        if (!file_stamp(wa->file, &size, &mtime)) {
            have_prev = false;
            continue;
        }
        if (have_prev && size == prev_size && CompareFileTime(&mtime, &prev_mtime) == 0) return true;

        prev_size  = size;
        prev_mtime = mtime;
        have_prev  = true;
    }
    return true;
}

static DWORD WINAPI watch_proc(LPVOID param) {
    WatchArgs *wa = (WatchArgs *)param;

    HANDLE dir = CreateFileW(wa->dir, FILE_LIST_DIRECTORY,
                             FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                             NULL, OPEN_EXISTING,
                             FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED, NULL);
    if (dir == INVALID_HANDLE_VALUE) {
        log_msg(LOG_WARN, L"config: cannot watch %ls (%lu); save changes and run mnotify --reload",
                wa->dir, GetLastError());
        free(wa);
        return 1;
    }

    OVERLAPPED ov = {0};
    ov.hEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
    if (!ov.hEvent) {
        CloseHandle(dir);
        free(wa);
        return 1;
    }

    BYTE buf[WATCH_BUF_SIZE];
    for (;;) {
        if (watch_next_batch(wa, dir, &ov, buf, INFINITE) == BATCH_STOP) break;
        if (!watch_debounce(wa, dir, &ov, buf)) break;
        if (!watch_settle(wa)) break;
        PostMessageW(wa->target, WM_MNOTIFY_CONFIG_CHANGED, (WPARAM)wa->generation, 0);
    }

    CloseHandle(ov.hEvent);
    CloseHandle(dir);
    free(wa);
    return 0;
}

static void watch_stop(void) {
    if (!s_watch_thread) return;

    SetEvent(s_watch_stop);
    if (WaitForSingleObject(s_watch_thread, WATCH_STOP_WAIT_MS) == WAIT_OBJECT_0) {
        CloseHandle(s_watch_thread);
        CloseHandle(s_watch_stop);
    } else {
        log_msg(LOG_WARN, L"config: the file watcher did not stop; leaking its handles");
    }
    s_watch_thread = NULL;
    s_watch_stop   = NULL;
    s_watch_dir[0] = L'\0';
}

static void watch_sync(void) {
    wchar_t dir[MAX_PATH];
    bool    want = mn.cfg.auto_reload && mn.control && dir_of(s_path, dir, MAX_PATH);

    if (s_watch_thread) {
        bool alive = WaitForSingleObject(s_watch_thread, 0) == WAIT_TIMEOUT;
        if (alive && want && _wcsicmp(dir, s_watch_dir) == 0) return;
        watch_stop();
    }
    if (!want) return;

    WatchArgs *wa = (WatchArgs *)calloc(1, sizeof *wa);
    if (!wa) return;
    mnotify_copy_w(wa->dir,  MAX_PATH, dir);
    mnotify_copy_w(wa->file, MAX_PATH, s_path);
    wa->generation = ++s_watch_generation;
    wa->target     = mn.control;
    wa->stop       = CreateEventW(NULL, TRUE, FALSE, NULL);
    if (!wa->stop) {
        free(wa);
        return;
    }

    HANDLE stop = wa->stop;
    s_watch_thread = CreateThread(NULL, 0, watch_proc, wa, 0, NULL);
    if (!s_watch_thread) {
        log_msg(LOG_WARN, L"config: cannot start the file watcher (%lu)", GetLastError());
        CloseHandle(stop);
        free(wa);
        return;
    }

    s_watch_stop = stop;
    mnotify_copy_w(s_watch_dir, MAX_PATH, dir);
    log_msg(LOG_DEBUG, L"config: watching %ls for changes", s_watch_dir);
}

static void log_loaded(const lua_State *L) {
    if (L) log_msg(LOG_INFO, L"config: loaded %ls", s_path);
    else   log_msg(LOG_INFO, L"config: no %ls; using the defaults", s_path);
}

void config_init(void) {
    resolve_config_path(s_path, MAX_PATH);

    Config     cfg;
    lua_State *L = NULL;
    char       err[CONFIG_ERROR_CAP];
    if (load(s_path, &L, &cfg, err, sizeof err)) {
        adopt(L, &cfg);
        log_loaded(L);
    } else {
        config_defaults(&cfg);
        adopt(NULL, &cfg);
        report_failure(L"Config error, using the defaults", err);
    }
    watch_sync();
}

void config_reload(void) {
    resolve_config_path(s_path, MAX_PATH);

    Config     cfg;
    lua_State *L = NULL;
    char       err[CONFIG_ERROR_CAP];
    if (!load(s_path, &L, &cfg, err, sizeof err)) {
        report_failure(L"Config error, previous config kept", err);
        watch_sync();
        return;
    }

    adopt(L, &cfg);
    log_loaded(L);
    watch_sync();
    popup_config_changed();
}

void config_on_file_changed(unsigned generation) {
    if (generation != s_watch_generation || !mn.cfg.auto_reload) return;
    log_msg(LOG_INFO, L"config: %ls changed on disk", s_path);
    config_reload();
}

void config_shutdown(void) {
    watch_stop();
    if (s_lua) {
        lua_close(s_lua);
        s_lua = NULL;
    }
}

bool config_check(char *out, size_t cap) {
    wchar_t path[MAX_PATH];
    char    shown[MAX_PATH * 3];
    resolve_config_path(path, MAX_PATH);
    if (!mnotify_wide_to_utf8(path, shown, sizeof shown)) snprintf(shown, sizeof shown, "init.lua");

    if (!file_exists(path)) {
        snprintf(out, cap, "ok: %s does not exist, so mnotify uses its defaults", shown);
        return true;
    }

    Config     cfg;
    lua_State *L = NULL;
    char       err[CONFIG_ERROR_CAP];
    if (!load(path, &L, &cfg, err, sizeof err)) {
        snprintf(out, cap, "FAILED: %s: %s", shown, err);
        return false;
    }
    if (L) lua_close(L);
    snprintf(out, cap, "ok: %s", shown);
    return true;
}

static const char *source_name(NoteSource source) {
    switch (source) {
    case NOTE_FROM_TRAY:    return "balloon";
    case NOTE_FROM_TOAST:   return "toast";
    case NOTE_FROM_BACKLOG:
    case NOTE_FROM_MNOTIFY: return "mnotify";
    default:                return "send";
    }
}

bool config_filter(Note *note) {
    if (!s_lua || mn.cfg.notify_ref == LUA_NOREF) return true;

    HookNote hook;
    memset(&hook, 0, sizeof hook);
    mnotify_wide_to_utf8(note->app,   hook.app,   sizeof hook.app);
    mnotify_wide_to_utf8(note->title, hook.title, sizeof hook.title);
    mnotify_wide_to_utf8(note->text,  hook.text,  sizeof hook.text);
    if (note->source == NOTE_FROM_TOAST)
        mnotify_wide_to_utf8(note->toast.aumid, hook.aumid, sizeof hook.aumid);
    hook.kind          = note->kind;
    hook.source        = source_name(note->source);
    hook.long_duration = note->long_duration;
    hook.timeout       = popup_timeout_for(note);
    hook.has_accent    = note->has_accent;
    hook.accent        = note->accent;

    char       err[CONFIG_ERROR_CAP];
    HookResult result = lua_api_run_hook(s_lua, mn.cfg.notify_ref, &hook, err, sizeof err);

    if (result == HOOK_DROP) {
        log_msg(LOG_DEBUG, L"config: the notify hook dropped a notification from %ls",
                note->app[0] ? note->app : L"?");
        return false;
    }
    if (result == HOOK_FAILED) {
        wchar_t werr[CONFIG_ERROR_CAP];
        mnotify_utf8_to_wide(err, werr, CONFIG_ERROR_CAP);
        log_err(L"config: notify hook failed, showing the notification unchanged: %ls", werr);
        return true;
    }

    mnotify_utf8_to_wide(hook.app,   note->app,   MNOTIFY_APP_CAP);
    mnotify_utf8_to_wide(hook.title, note->title, MNOTIFY_TITLE_CAP);
    mnotify_utf8_to_wide(hook.text,  note->text,  MNOTIFY_TEXT_CAP);
    note->kind        = hook.kind;
    note->has_timeout = true;
    note->timeout     = hook.timeout;
    note->has_accent  = hook.has_accent;
    note->accent      = hook.accent;
    return true;
}
