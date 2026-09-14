#include "mnotify.h"

static bool s_class_registered;

bool tray_host_other_tray_exists(void) {
    HWND h = NULL;
    while ((h = FindWindowExW(NULL, h, MNOTIFY_TRAY_CLASS, NULL)) != NULL) {
        if (h != mn.tray) return true;
    }
    return false;
}

static bool file_description(const wchar_t *path, wchar_t *out, size_t cap) {
    DWORD ignored = 0;
    DWORD size = GetFileVersionInfoSizeW(path, &ignored);
    if (!size) return false;

    BYTE *block = (BYTE *)malloc(size);
    if (!block) return false;

    bool found = false;
    struct { WORD lang; WORD codepage; } *tr = NULL;
    UINT tr_len = 0;

    if (GetFileVersionInfoW(path, 0, size, block) &&
        VerQueryValueW(block, L"\\VarFileInfo\\Translation", (LPVOID *)&tr, &tr_len) &&
        tr_len >= sizeof *tr) {
        wchar_t query[64];
        _snwprintf(query, 63, L"\\StringFileInfo\\%04x%04x\\FileDescription",
                   tr->lang, tr->codepage);
        query[63] = L'\0';

        wchar_t *desc = NULL;
        UINT desc_len = 0;
        if (VerQueryValueW(block, query, (LPVOID *)&desc, &desc_len) &&
            desc_len > 1 && desc && desc[0]) {
            mnotify_copy_w(out, cap, desc);
            found = true;
        }
    }

    free(block);
    return found;
}

void tray_host_app_name(uint32_t hwnd, wchar_t *out, size_t cap) {
    out[0] = L'\0';

    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd_of(hwnd), &pid);
    if (!pid) return;

    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!process) return;

    wchar_t path[MAX_PATH];
    DWORD len = MAX_PATH;
    bool ok = QueryFullProcessImageNameW(process, 0, path, &len);
    CloseHandle(process);
    if (!ok) return;

    if (file_description(path, out, cap)) return;

    const wchar_t *base = wcsrchr(path, L'\\');
    mnotify_copy_w(out, cap, base ? base + 1 : path);
    wchar_t *dot = wcsrchr(out, L'.');
    if (dot) *dot = L'\0';
}

void tray_host_grant_foreground(uint32_t hwnd) {
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd_of(hwnd), &pid);
    if (pid) AllowSetForegroundWindow(pid);
}

void tray_host_notify(const TrayIcon *icon, uint32_t event) {
    if (!icon->callback || !IsWindow(hwnd_of(icon->id.hwnd))) return;

    POINT pt = { 0, 0 };
    GetCursorPos(&pt);
    TrayCallback cb = tray_pack_callback(icon->version, icon->id.uid, event, pt.x, pt.y);
    SendNotifyMessageW(hwnd_of(icon->id.hwnd), icon->callback,
                       (WPARAM)cb.wparam, (LPARAM)cb.lparam);
}

void tray_host_click(const TrayIcon *icon, TrayClick click) {
    tray_host_grant_foreground(icon->id.hwnd);

    uint32_t events[TRAY_MAX_CLICK_EVENTS];
    int n = tray_click_events(icon->version, click, events, TRAY_MAX_CLICK_EVENTS);
    for (int i = 0; i < n; i++) tray_host_notify(icon, events[i]);
}

void tray_host_prune(void) {
    for (int i = mn.table.count - 1; i >= 0; i--) {
        if (IsWindow(hwnd_of(mn.table.icons[i].id.hwnd))) continue;
        log_msg(LOG_DEBUG, L"tray: dropping icon %u of a window that is gone",
                mn.table.icons[i].id.uid);
        tray_remove_at(&mn.table, i);
    }
}

static NoteKind kind_of(uint32_t info_flags) {
    switch (info_flags & TRAY_NIIF_ICON_MASK) {
    case TRAY_NIIF_WARNING: return NOTE_WARN;
    case TRAY_NIIF_ERROR:   return NOTE_ERROR;
    default:                return NOTE_INFO;
    }
}

static void show_balloon(const TrayEffect *effect) {
    Note note;
    memset(&note, 0, sizeof note);
    note.kind      = kind_of(effect->info_flags);
    note.from_tray = true;
    note.icon      = effect->icon;
    mnotify_copy_w(note.title, MNOTIFY_TITLE_CAP, (const wchar_t *)effect->title);
    mnotify_copy_w(note.text,  MNOTIFY_TEXT_CAP,  (const wchar_t *)effect->text);
    tray_host_app_name(effect->icon.id.hwnd, note.app, MNOTIFY_APP_CAP);

    log_msg(LOG_INFO, L"balloon from %ls: [%ls] %ls",
            note.app[0] ? note.app : L"?", note.title, note.text);

    popup_show(&note);
    tray_host_notify(&effect->icon, TRAY_NIN_BALLOONSHOW);
}

static const wchar_t *nim_name(uint32_t message) {
    switch (message) {
    case TRAY_NIM_ADD:        return L"add";
    case TRAY_NIM_MODIFY:     return L"modify";
    case TRAY_NIM_DELETE:     return L"delete";
    case TRAY_NIM_SETFOCUS:   return L"setfocus";
    case TRAY_NIM_SETVERSION: return L"setversion";
    default:                  return L"unknown";
    }
}

static LRESULT on_notify_icon(const COPYDATASTRUCT *cds) {
    TrayMessage msg;
    if (!tray_parse((const uint8_t *)cds->lpData, cds->cbData, &msg)) {
        log_msg(LOG_WARN, L"tray: ignored a malformed Shell_NotifyIcon payload (%lu bytes)",
                cds->cbData);
        return FALSE;
    }

    TrayEffect effect;
    bool ok = tray_apply(&mn.table, &msg, &effect);

    log_msg(LOG_DEBUG, L"tray: %ls hwnd %08x uid %u flags %02x -> %ls (%d icons)",
            nim_name(msg.message), msg.hwnd, msg.uid, msg.flags,
            ok ? L"ok" : L"refused", mn.table.count);

    if (effect.kind == TRAY_EFFECT_SHOW_BALLOON)      show_balloon(&effect);
    else if (effect.kind == TRAY_EFFECT_HIDE_BALLOON) popup_hide_for(&effect.icon.id);

    return ok ? TRUE : FALSE;
}

static LRESULT CALLBACK tray_wndproc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_COPYDATA) {
        const COPYDATASTRUCT *cds = (const COPYDATASTRUCT *)lp;
        if (!cds || !cds->lpData) return FALSE;
        if (cds->dwData == TRAY_COPY_NOTIFYICON) return on_notify_icon(cds);
        log_msg(LOG_TRACE, L"tray: unanswered copydata %llu", (unsigned long long)cds->dwData);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

bool tray_host_init(void) {
    if (tray_host_other_tray_exists()) {
        log_err(L"tray: a Shell_TrayWnd already exists (is Explorer running?); not taking it over");
        return false;
    }

    WNDCLASSEXW wc = {
        .cbSize        = sizeof wc,
        .lpfnWndProc   = tray_wndproc,
        .hInstance     = mn.hinst,
        .lpszClassName = MNOTIFY_TRAY_CLASS,
    };
    if (!RegisterClassExW(&wc)) {
        log_err(L"tray: RegisterClassEx failed (%lu)", GetLastError());
        return false;
    }
    s_class_registered = true;

    mn.tray = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST, MNOTIFY_TRAY_CLASS,
                              NULL, WS_POPUP, 0, 0, 0, 0, NULL, NULL, mn.hinst, NULL);
    if (!mn.tray) {
        log_err(L"tray: CreateWindowEx failed (%lu)", GetLastError());
        return false;
    }

    ChangeWindowMessageFilterEx(mn.tray, WM_COPYDATA, MSGFLT_ALLOW, NULL);

    mn.taskbar_created = RegisterWindowMessageW(L"TaskbarCreated");
    SendNotifyMessageW(HWND_BROADCAST, mn.taskbar_created, 0, 0);
    log_msg(LOG_INFO, L"tray: hosting Shell_TrayWnd; asked running apps to re-add their icons");
    return true;
}

void tray_host_shutdown(void) {
    if (mn.tray) {
        DestroyWindow(mn.tray);
        mn.tray = NULL;
    }
    if (s_class_registered) {
        UnregisterClassW(MNOTIFY_TRAY_CLASS, mn.hinst);
        s_class_registered = false;
    }
    mn.table.count = 0;
}
