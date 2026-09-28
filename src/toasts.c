#define COBJMACROS
#include "mnotify.h"

#include <knownfolders.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <shobjidl.h>

#define TOAST_TIMER_ID      0x7451
#define TOAST_POLL_MS       500
#define TOAST_RETRY_MS      10000
#define TOAST_APP_CACHE     32

#define SQLITE_BUSY           5
#define SQLITE_ROW            100
#define SQLITE_DONE           101
#define SQLITE_OPEN_READONLY  0x00000001

#define SYSTEM_TOAST_PREFIX   L"Windows.SystemToast."
#define MNOTIFY_STATE_KEY     L"Software\\mnotify"
#define LAST_TOAST_VALUE      L"LastToastId"
#define TOAST_SETTINGS_KEY    L"Software\\Microsoft\\Windows\\CurrentVersion\\PushNotifications"
#define STORE_RELATIVE_PATH   L"\\Microsoft\\Windows\\Notifications\\wpndatabase.db"

#define QUERY_DATA_VERSION    "PRAGMA data_version"
#define QUERY_MAX_ID          "SELECT COALESCE(MAX(Id), 0) FROM Notification"
#define SELECT_TOASTS                                                             \
    "SELECT n.Id, h.PrimaryId, n.Payload, n.ExpiryTime, n.ArrivalTime, "          \
    "COALESCE((SELECT s.Value FROM HandlerSettings s "                            \
    "WHERE s.HandlerId = h.RecordId AND s.SettingKey = 's:banner'), 1) "          \
    "FROM Notification n JOIN NotificationHandler h ON h.RecordId = n.HandlerId " \
    "WHERE n.Type = 'toast' "
#define QUERY_NEW_TOASTS      SELECT_TOASTS "AND n.Id > ?1 ORDER BY n.Id"
#define QUERY_HISTORY         SELECT_TOASTS "ORDER BY n.Id DESC"

typedef struct sqlite3      sqlite3;
typedef struct sqlite3_stmt sqlite3_stmt;

typedef struct {
    HMODULE dll;
    int            (*open_v2)(const char *, sqlite3 **, int, const char *);
    int            (*close)(sqlite3 *);
    int            (*busy_timeout)(sqlite3 *, int);
    const char    *(*errmsg)(sqlite3 *);
    int            (*prepare_v2)(sqlite3 *, const char *, int, sqlite3_stmt **, const char **);
    int            (*step)(sqlite3_stmt *);
    int            (*reset)(sqlite3_stmt *);
    int            (*finalize)(sqlite3_stmt *);
    int            (*bind_int64)(sqlite3_stmt *, int, long long);
    long long      (*column_int64)(sqlite3_stmt *, int);
    const void    *(*column_blob)(sqlite3_stmt *, int);
    int            (*column_bytes)(sqlite3_stmt *, int);
    const void    *(*column_text16)(sqlite3_stmt *, int);
} Sqlite;

typedef struct {
    sqlite3      *db;
    sqlite3_stmt *data_version;
    sqlite3_stmt *max_id;
    sqlite3_stmt *new_toasts;
    sqlite3_stmt *history;
    long long     version;
    long long     last_id;
    bool          have_baseline;
    bool          failing;
    ULONGLONG     retry_at;
} Store;

typedef struct {
    long long    id;
    long long    arrival;
    wchar_t      aumid[MNOTIFY_AUMID_CAP];
    bool         banner;
    ToastContent content;
} StoredToast;

typedef struct {
    wchar_t aumid[MNOTIFY_AUMID_CAP];
    wchar_t name[MNOTIFY_APP_CAP];
    wchar_t exe[MAX_PATH];
} AppInfo;

static const PROPERTYKEY PKEY_LINK_TARGET = {
    { 0xB9B4B3FC, 0x2B51, 0x4A42, { 0xB5, 0xD8, 0x32, 0x41, 0x46, 0xAF, 0xCF, 0x25 } }, 2
};

static Sqlite       s_sql;
static bool         s_sql_missing;
static Store        s_store;
static StoredToast  s_pending[MNOTIFY_MAX_POPUPS];
static StoredToast  s_row;
static long long    s_saved_id = -1;
static int          s_held_back;
static AppInfo      s_apps[TOAST_APP_CACHE];
static int          s_app_count;
static int          s_app_next;

static bool load_sqlite(void) {
    if (s_sql.dll) return true;
    if (s_sql_missing) return false;

    HMODULE dll = LoadLibraryExW(L"winsqlite3.dll", NULL, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!dll) {
        s_sql_missing = true;
        log_msg(LOG_WARN, L"toasts: winsqlite3.dll is not available (%lu); "
                          L"only tray balloons will be shown", GetLastError());
        return false;
    }

    s_sql.open_v2       = (void *)GetProcAddress(dll, "sqlite3_open_v2");
    s_sql.close         = (void *)GetProcAddress(dll, "sqlite3_close");
    s_sql.busy_timeout  = (void *)GetProcAddress(dll, "sqlite3_busy_timeout");
    s_sql.errmsg        = (void *)GetProcAddress(dll, "sqlite3_errmsg");
    s_sql.prepare_v2    = (void *)GetProcAddress(dll, "sqlite3_prepare_v2");
    s_sql.step          = (void *)GetProcAddress(dll, "sqlite3_step");
    s_sql.reset         = (void *)GetProcAddress(dll, "sqlite3_reset");
    s_sql.finalize      = (void *)GetProcAddress(dll, "sqlite3_finalize");
    s_sql.bind_int64    = (void *)GetProcAddress(dll, "sqlite3_bind_int64");
    s_sql.column_int64  = (void *)GetProcAddress(dll, "sqlite3_column_int64");
    s_sql.column_blob   = (void *)GetProcAddress(dll, "sqlite3_column_blob");
    s_sql.column_bytes  = (void *)GetProcAddress(dll, "sqlite3_column_bytes");
    s_sql.column_text16 = (void *)GetProcAddress(dll, "sqlite3_column_text16");

    if (!s_sql.open_v2 || !s_sql.close || !s_sql.busy_timeout || !s_sql.errmsg ||
        !s_sql.prepare_v2 || !s_sql.step || !s_sql.reset || !s_sql.finalize ||
        !s_sql.bind_int64 || !s_sql.column_int64 || !s_sql.column_blob ||
        !s_sql.column_bytes || !s_sql.column_text16) {
        FreeLibrary(dll);
        memset(&s_sql, 0, sizeof s_sql);
        s_sql_missing = true;
        log_msg(LOG_WARN, L"toasts: winsqlite3.dll lacks the functions mnotify needs; "
                          L"only tray balloons will be shown");
        return false;
    }

    s_sql.dll = dll;
    return true;
}

static bool store_path(char *out, int cap) {
    PWSTR base = NULL;
    if (FAILED(SHGetKnownFolderPath(&FOLDERID_LocalAppData, 0, NULL, &base))) return false;

    wchar_t path[MAX_PATH];
    int n = _snwprintf(path, MAX_PATH, L"%ls%ls", base, STORE_RELATIVE_PATH);
    CoTaskMemFree(base);
    if (n < 0 || n >= MAX_PATH) return false;

    return WideCharToMultiByte(CP_UTF8, 0, path, -1, out, cap, NULL, NULL) > 0;
}

static void store_close(void) {
    if (s_store.history)      s_sql.finalize(s_store.history);
    if (s_store.new_toasts)   s_sql.finalize(s_store.new_toasts);
    if (s_store.max_id)       s_sql.finalize(s_store.max_id);
    if (s_store.data_version) s_sql.finalize(s_store.data_version);
    if (s_store.db)           s_sql.close(s_store.db);
    s_store.history      = NULL;
    s_store.new_toasts   = NULL;
    s_store.max_id       = NULL;
    s_store.data_version = NULL;
    s_store.db           = NULL;
}

static void store_fail(const wchar_t *what) {
    const char *why = s_store.db ? s_sql.errmsg(s_store.db) : "no database";
    log_msg(s_store.failing ? LOG_DEBUG : LOG_WARN,
            L"toasts: %ls failed (%hs); retrying every %d s", what, why, TOAST_RETRY_MS / 1000);
    s_store.failing  = true;
    s_store.retry_at = GetTickCount64() + TOAST_RETRY_MS;
    store_close();
}

static int step_int64(sqlite3_stmt *stmt, long long *out) {
    int rc = s_sql.step(stmt);
    if (rc == SQLITE_ROW) *out = s_sql.column_int64(stmt, 0);
    s_sql.reset(stmt);
    return rc;
}

static long long saved_last_id(void) {
    ULONGLONG value = 0;
    DWORD     size  = sizeof value;
    if (RegGetValueW(HKEY_CURRENT_USER, MNOTIFY_STATE_KEY, LAST_TOAST_VALUE, RRF_RT_REG_QWORD,
                     NULL, &value, &size) != ERROR_SUCCESS)
        return 0;
    s_saved_id = (long long)value;
    return s_saved_id;
}

static void save_last_id(void) {
    if (s_store.last_id == s_saved_id) return;

    HKEY key;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, MNOTIFY_STATE_KEY, 0, NULL, REG_OPTION_NON_VOLATILE,
                        KEY_SET_VALUE, NULL, &key, NULL) != ERROR_SUCCESS)
        return;

    ULONGLONG value = (ULONGLONG)s_store.last_id;
    if (RegSetValueExW(key, LAST_TOAST_VALUE, 0, REG_QWORD, (const BYTE *)&value, sizeof value)
            == ERROR_SUCCESS)
        s_saved_id = s_store.last_id;
    RegCloseKey(key);
}

static bool store_open(void) {
    if (!load_sqlite()) return false;

    char path[MAX_PATH * 3];
    if (!store_path(path, (int)sizeof path)) {
        store_fail(L"locating the notification store");
        return false;
    }

    if (s_sql.open_v2(path, &s_store.db, SQLITE_OPEN_READONLY, NULL) != 0) {
        store_fail(L"opening the notification store");
        return false;
    }
    s_sql.busy_timeout(s_store.db, 50);

    if (s_sql.prepare_v2(s_store.db, QUERY_DATA_VERSION, -1, &s_store.data_version, NULL) != 0 ||
        s_sql.prepare_v2(s_store.db, QUERY_MAX_ID, -1, &s_store.max_id, NULL) != 0 ||
        s_sql.prepare_v2(s_store.db, QUERY_NEW_TOASTS, -1, &s_store.new_toasts, NULL) != 0 ||
        s_sql.prepare_v2(s_store.db, QUERY_HISTORY, -1, &s_store.history, NULL) != 0) {
        store_fail(L"reading the notification store's layout");
        return false;
    }

    long long max_id = 0;
    if (step_int64(s_store.max_id, &max_id) != SQLITE_ROW) {
        store_fail(L"reading the notification store");
        return false;
    }

    if (!s_store.have_baseline) {
        s_store.last_id       = mn.cfg.behavior.catch_up ? saved_last_id() : max_id;
        s_store.have_baseline = true;
        if (!mn.cfg.behavior.catch_up)
            log_msg(LOG_INFO, L"toasts: catch_up is off; skipping notifications that arrived earlier");
        else if (s_store.last_id)
            log_msg(LOG_INFO, L"toasts: catching up on notifications after %lld", s_store.last_id);
        else
            log_msg(LOG_INFO, L"toasts: first run; catching up on every stored notification");
    }
    s_store.version = -1;
    s_store.failing = false;
    log_msg(LOG_INFO, L"toasts: watching the Windows notification store");
    return true;
}

static bool is_expired(long long expiry) {
    if (expiry <= 0) return false;
    FILETIME ft;
    GetSystemTimeAsFileTime(&ft);
    long long now = (long long)(((ULONGLONG)ft.dwHighDateTime << 32) | ft.dwLowDateTime);
    return expiry < now;
}

static bool read_row(sqlite3_stmt *q, StoredToast *t) {
    t->id      = s_sql.column_int64(q, 0);
    t->arrival = s_sql.column_int64(q, 4);
    t->banner  = s_sql.column_int64(q, 5) != 0;
    if (is_expired(s_sql.column_int64(q, 3))) return false;

    const char *payload = (const char *)s_sql.column_blob(q, 2);
    int         bytes   = s_sql.column_bytes(q, 2);
    if (!payload || bytes <= 0 || !toast_parse(payload, (size_t)bytes, &t->content)) {
        log_msg(LOG_DEBUG, L"toasts: skipped notification %lld (nothing to show)", t->id);
        return false;
    }

    const wchar_t *aumid = (const wchar_t *)s_sql.column_text16(q, 1);
    mnotify_copy_w(t->aumid, MNOTIFY_AUMID_CAP, aumid ? aumid : L"");
    return true;
}

static int collect_new_toasts(void) {
    sqlite3_stmt *q = s_store.new_toasts;
    s_sql.bind_int64(q, 1, s_store.last_id);

    int count = 0;
    int rc;
    while ((rc = s_sql.step(q)) == SQLITE_ROW) {
        long long id = s_sql.column_int64(q, 0);
        if (id > s_store.last_id) s_store.last_id = id;
        if (!read_row(q, &s_row)) continue;
        if (!s_row.banner) {
            log_msg(LOG_DEBUG, L"toasts: banners are off for %ls; not showing %lld",
                    s_row.aumid, s_row.id);
            continue;
        }
        s_pending[count % MNOTIFY_MAX_POPUPS] = s_row;
        count++;
    }
    s_sql.reset(q);

    if (rc == SQLITE_BUSY) s_store.version = -1;
    if (rc != SQLITE_DONE && rc != SQLITE_BUSY) {
        store_fail(L"reading new notifications");
        return 0;
    }
    return count;
}

static void name_from_registry(const wchar_t *aumid, wchar_t *out, size_t cap) {
    wchar_t key[MNOTIFY_AUMID_CAP + 64];
    _snwprintf(key, MNOTIFY_AUMID_CAP + 63, L"Software\\Classes\\AppUserModelId\\%ls", aumid);
    key[MNOTIFY_AUMID_CAP + 63] = L'\0';

    wchar_t value[512];
    DWORD   size = sizeof value;
    if (RegGetValueW(HKEY_CURRENT_USER, key, L"DisplayName", RRF_RT_REG_SZ, NULL, value, &size)
            != ERROR_SUCCESS || !value[0])
        return;

    if (value[0] == L'@') {
        wchar_t resolved[256];
        if (SUCCEEDED(SHLoadIndirectString(value, resolved, 256, NULL)))
            mnotify_copy_w(out, cap, resolved);
    } else {
        mnotify_copy_w(out, cap, value);
    }
}

static void name_from_aumid(const wchar_t *aumid, wchar_t *out, size_t cap) {
    size_t prefix = wcslen(SYSTEM_TOAST_PREFIX);
    if (_wcsnicmp(aumid, SYSTEM_TOAST_PREFIX, prefix) == 0 && aumid[prefix]) {
        mnotify_copy_w(out, cap, aumid + prefix);
        return;
    }

    const wchar_t *slash = wcsrchr(aumid, L'\\');
    mnotify_copy_w(out, cap, slash ? slash + 1 : aumid);
    wchar_t *dot = slash ? wcsrchr(out, L'.') : NULL;
    if (dot) *dot = L'\0';
}

static void resolve_app(AppInfo *app) {
    IShellItem2 *item = NULL;
    if (SUCCEEDED(SHCreateItemInKnownFolder(&FOLDERID_AppsFolder, KF_FLAG_DONT_VERIFY, app->aumid,
                                            &IID_IShellItem2, (void **)&item))) {
        PWSTR name = NULL;
        if (SUCCEEDED(IShellItem2_GetDisplayName(item, SIGDN_NORMALDISPLAY, &name)) && name) {
            mnotify_copy_w(app->name, MNOTIFY_APP_CAP, name);
            CoTaskMemFree(name);
        }
        PWSTR target = NULL;
        if (SUCCEEDED(IShellItem2_GetString(item, &PKEY_LINK_TARGET, &target)) && target) {
            mnotify_copy_w(app->exe, MAX_PATH, target);
            CoTaskMemFree(target);
        }
        IShellItem2_Release(item);
    }

    if (!app->name[0]) name_from_registry(app->aumid, app->name, MNOTIFY_APP_CAP);
    if (!app->name[0]) name_from_aumid(app->aumid, app->name, MNOTIFY_APP_CAP);

    log_msg(LOG_DEBUG, L"toasts: %ls is '%ls' (%ls)", app->aumid, app->name,
            app->exe[0] ? app->exe : L"no executable");
}

static const AppInfo *app_info(const wchar_t *aumid) {
    for (int i = 0; i < s_app_count; i++)
        if (_wcsicmp(s_apps[i].aumid, aumid) == 0) return &s_apps[i];

    AppInfo *app = &s_apps[s_app_next];
    s_app_next = (s_app_next + 1) % TOAST_APP_CACHE;
    if (s_app_count < TOAST_APP_CACHE) s_app_count++;

    memset(app, 0, sizeof *app);
    mnotify_copy_w(app->aumid, MNOTIFY_AUMID_CAP, aumid);
    resolve_app(app);
    return app;
}

static void app_line(const AppInfo *app, const char *attribution, wchar_t *out, size_t cap) {
    wchar_t source[TOAST_ATTRIBUTION_CAP];
    mnotify_utf8_to_wide(attribution, source, TOAST_ATTRIBUTION_CAP);

    if (source[0] && _wcsicmp(source, app->name) != 0)
        _snwprintf(out, cap, L"%ls · %ls", app->name, source);
    else
        _snwprintf(out, cap, L"%ls", app->name);
    out[cap - 1] = L'\0';
}

static void fill_target(const StoredToast *t, const AppInfo *app, ToastTarget *target) {
    mnotify_copy_w(target->aumid, MNOTIFY_AUMID_CAP, t->aumid);
    mnotify_copy_w(target->exe,   MAX_PATH,          app->exe);
    target->activation      = t->content.activation;
    target->launch_complete = mnotify_utf8_to_wide(t->content.launch, target->launch, MNOTIFY_LAUNCH_CAP) &&
                              !t->content.launch_truncated;
}

static void show_toast(const StoredToast *t) {
    const AppInfo *app = app_info(t->aumid);

    Note note;
    memset(&note, 0, sizeof note);
    note.source        = NOTE_FROM_TOAST;
    note.kind          = NOTE_INFO;
    note.long_duration = t->content.long_duration;
    app_line(app, t->content.attribution, note.app, MNOTIFY_APP_CAP);
    mnotify_utf8_to_wide(t->content.title, note.title, MNOTIFY_TITLE_CAP);
    mnotify_utf8_to_wide(t->content.body,  note.text,  MNOTIFY_TEXT_CAP);

    fill_target(t, app, &note.toast);

    log_msg(LOG_INFO,  L"toast from %ls", note.app);
    log_msg(LOG_DEBUG, L"toasts: %lld [%ls] %ls", t->id, note.title, note.text);
    popup_show(&note);
}

static void show_summary(int count, bool while_busy) {
    Note note;
    memset(&note, 0, sizeof note);
    note.source        = NOTE_FROM_BACKLOG;
    note.kind          = NOTE_INFO;
    note.long_duration = true;

    const wchar_t *noun = count == 1 ? L"notification" : L"notifications";
    if (while_busy) _snwprintf(note.title, MNOTIFY_TITLE_CAP, L"%d %ls while you were busy", count, noun);
    else            _snwprintf(note.title, MNOTIFY_TITLE_CAP, L"%d more %ls", count, noun);
    note.title[MNOTIFY_TITLE_CAP - 1] = L'\0';
    mnotify_copy_w(note.app,  MNOTIFY_APP_CAP,  L"mnotify");
    mnotify_copy_w(note.text, MNOTIFY_TEXT_CAP, L"Click to list them, or run mnotify --history.");

    log_msg(LOG_INFO, L"toasts: %ls", note.title);
    popup_show(&note);
}

static void present(int count) {
    if (!count) return;

    QUERY_USER_NOTIFICATION_STATE state = tray_host_user_state();
    if (mn.cfg.behavior.hold_when_busy && state != QUNS_ACCEPTS_NOTIFICATIONS) {
        s_held_back += count;
        log_msg(LOG_INFO, L"toasts: holding back %d (user notification state %d)", count, (int)state);
        return;
    }

    int max   = mn.cfg.behavior.max_visible;
    int shown = count > max ? max - 1 : count;
    if (shown < count) show_summary(count - shown, false);
    for (int i = count - shown; i < count; i++) show_toast(&s_pending[i % MNOTIFY_MAX_POPUPS]);
}

static void release_held_back(void) {
    if (!s_held_back || tray_host_user_state() != QUNS_ACCEPTS_NOTIFICATIONS) return;
    show_summary(s_held_back, true);
    s_held_back = 0;
}

static void poll(void) {
    release_held_back();

    if (!s_store.db) {
        if (s_sql_missing || GetTickCount64() < s_store.retry_at || !store_open()) return;
    }

    long long version = 0;
    int rc = step_int64(s_store.data_version, &version);
    if (rc == SQLITE_BUSY) return;
    if (rc != SQLITE_ROW) { store_fail(L"checking the notification store"); return; }
    if (version == s_store.version) return;
    s_store.version = version;

    long long max_id = 0;
    rc = step_int64(s_store.max_id, &max_id);
    if (rc == SQLITE_BUSY) { s_store.version = -1; return; }
    if (rc != SQLITE_ROW)  { store_fail(L"checking the notification store"); return; }

    if (max_id < s_store.last_id) s_store.last_id = max_id;
    if (max_id > s_store.last_id) present(collect_new_toasts());
    save_last_id();
}

int toasts_history(HistoryItem *items, int cap) {
    if (!s_store.db && (s_sql_missing || !store_open())) return -1;

    sqlite3_stmt *q     = s_store.history;
    int           count = 0;
    int           rc    = SQLITE_DONE;
    while (count < cap && (rc = s_sql.step(q)) == SQLITE_ROW) {
        if (!read_row(q, &s_row)) continue;

        const AppInfo *app  = app_info(s_row.aumid);
        HistoryItem   *item = &items[count++];
        item->arrival = s_row.arrival;
        app_line(app, s_row.content.attribution, item->app, MNOTIFY_APP_CAP);
        mnotify_utf8_to_wide(s_row.content.title, item->title, TOAST_TITLE_CAP);
        mnotify_utf8_to_wide(s_row.content.body,  item->text,  TOAST_BODY_CAP);
        fill_target(&s_row, app, &item->target);
    }
    s_sql.reset(q);

    if (rc != SQLITE_ROW && rc != SQLITE_DONE && rc != SQLITE_BUSY) {
        store_fail(L"reading the notification history");
        return -1;
    }
    return count;
}

static void warn_if_toasts_disabled(void) {
    DWORD enabled = 1;
    DWORD size    = sizeof enabled;
    if (RegGetValueW(HKEY_CURRENT_USER, TOAST_SETTINGS_KEY, L"ToastEnabled", RRF_RT_REG_DWORD,
                     NULL, &enabled, &size) != ERROR_SUCCESS || enabled)
        return;

    log_msg(LOG_WARN, L"toasts: Windows notifications are turned off for this user "
                      L"(PushNotifications\\ToastEnabled = 0), so apps' toasts are dropped "
                      L"before they reach mnotify");

    Note note;
    memset(&note, 0, sizeof note);
    note.kind          = NOTE_WARN;
    note.source        = NOTE_FROM_MNOTIFY;
    note.long_duration = true;
    mnotify_copy_w(note.app,   MNOTIFY_APP_CAP,   L"mnotify");
    mnotify_copy_w(note.title, MNOTIFY_TITLE_CAP, L"Windows notifications are off");
    mnotify_copy_w(note.text,  MNOTIFY_TEXT_CAP,
                   L"Apps' notifications are dropped before mnotify can show them. Set "
                   L"HKCU\\" TOAST_SETTINGS_KEY L"\\ToastEnabled to 1, then sign out and back in.");
    popup_show(&note);
}

static void CALLBACK poll_timer(HWND hwnd, UINT msg, UINT_PTR id, DWORD time) {
    (void)hwnd; (void)msg; (void)id; (void)time;
    poll();
}

void toasts_init(void) {
    warn_if_toasts_disabled();
    store_open();
    SetTimer(mn.control, TOAST_TIMER_ID, TOAST_POLL_MS, poll_timer);
}

void toasts_shutdown(void) {
    if (mn.control) KillTimer(mn.control, TOAST_TIMER_ID);
    if (s_sql.dll) {
        store_close();
        FreeLibrary(s_sql.dll);
        memset(&s_sql, 0, sizeof s_sql);
    }
}
