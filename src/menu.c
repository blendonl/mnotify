#include "mnotify.h"

#define MENU_ACTIONS 3
#define MENU_LABEL_CAP 128

typedef struct {
    TrayIdentity ids[TRAY_MAX_ICONS];
    int          count;
} MenuSlots;

static bool s_menu_open;

static UINT command_for(int slot, TrayClick click) {
    return (UINT)(slot * MENU_ACTIONS + (int)click + 1);
}

static void escape_ampersands(const wchar_t *src, wchar_t *out, size_t cap) {
    size_t n = 0;
    for (; *src && n + 1 < cap; src++) {
        if (*src == L'&') {
            if (n + 2 >= cap) break;
            out[n++] = L'&';
        }
        out[n++] = *src;
    }
    out[n] = L'\0';
}

static void label_for(const TrayIcon *icon, wchar_t *out, size_t cap) {
    TrayChar line[MENU_LABEL_CAP];
    tray_first_line(icon->tip, line, MENU_LABEL_CAP);

    wchar_t plain[MENU_LABEL_CAP];
    if (line[0]) mnotify_copy_w(plain, MENU_LABEL_CAP, (const wchar_t *)line);
    else         tray_host_app_name(icon->id.hwnd, plain, MENU_LABEL_CAP);

    if (!plain[0]) mnotify_copy_w(plain, MENU_LABEL_CAP, L"(unnamed icon)");
    escape_ampersands(plain, out, cap);
}

static HMENU build_menu(MenuSlots *slots) {
    HMENU root = CreatePopupMenu();
    if (!root) return NULL;

    slots->count = 0;
    for (int i = 0; i < mn.table.count; i++) {
        const TrayIcon *icon = &mn.table.icons[i];
        if (icon->state & TRAY_NIS_HIDDEN) continue;

        HMENU sub = CreatePopupMenu();
        if (!sub) continue;

        int slot = slots->count++;
        slots->ids[slot] = icon->id;

        AppendMenuW(sub, MF_STRING, command_for(slot, TRAY_CLICK_LEFT),   L"Click");
        AppendMenuW(sub, MF_STRING, command_for(slot, TRAY_CLICK_DOUBLE), L"Double-click");
        AppendMenuW(sub, MF_STRING, command_for(slot, TRAY_CLICK_RIGHT),  L"Right-click");

        wchar_t label[MENU_LABEL_CAP];
        label_for(icon, label, MENU_LABEL_CAP);
        AppendMenuW(root, MF_POPUP | MF_STRING, (UINT_PTR)sub, label);
    }

    if (!slots->count) AppendMenuW(root, MF_STRING | MF_GRAYED, 0, L"No tray icons");
    return root;
}

static UINT track_menu(HMENU menu) {
    HWND previous = GetForegroundWindow();
    if (!SetForegroundWindow(mn.control))
        log_msg(LOG_WARN, L"menu: could not take the foreground; "
                          L"clicking outside the menu may not close it (Esc will)");

    POINT pt = { 0, 0 };
    GetCursorPos(&pt);

    s_menu_open = true;
    SetLastError(0);
    UINT cmd = (UINT)TrackPopupMenuEx(menu, TPM_RETURNCMD | TPM_NONOTIFY | TPM_RIGHTBUTTON,
                                      pt.x, pt.y, mn.control, NULL);
    DWORD error = GetLastError();
    s_menu_open = false;
    PostMessageW(mn.control, WM_NULL, 0, 0);
    DestroyMenu(menu);

    if (!cmd && error) log_msg(LOG_WARN, L"menu: TrackPopupMenuEx failed (%lu)", error);
    else               log_msg(LOG_DEBUG, L"menu: closed with command %u", cmd);

    if (!cmd && previous && previous != mn.control && IsWindow(previous) &&
        GetForegroundWindow() == mn.control)
        SetForegroundWindow(previous);
    return cmd;
}

void menu_show_tray(void) {
    if (s_menu_open) return;
    tray_host_prune();

    MenuSlots *slots = (MenuSlots *)calloc(1, sizeof *slots);
    if (!slots) return;

    HMENU menu = build_menu(slots);
    if (!menu) { free(slots); return; }

    log_msg(LOG_DEBUG, L"menu: opening with %d icon(s)", slots->count);
    UINT cmd = track_menu(menu);

    if (cmd) {
        int       slot  = (int)(cmd - 1) / MENU_ACTIONS;
        TrayClick click = (TrayClick)((cmd - 1) % MENU_ACTIONS);
        int       i     = slot < slots->count ? tray_find(&mn.table, &slots->ids[slot]) : -1;

        if (i >= 0) {
            log_msg(LOG_DEBUG, L"menu: click %d on icon %u", (int)click,
                    mn.table.icons[i].id.uid);
            tray_host_click(&mn.table.icons[i], click);
        } else {
            log_msg(LOG_INFO, L"menu: that icon went away while the menu was open");
        }
    }

    free(slots);
}

void menu_show_history(void) {
    if (s_menu_open) return;

    HistoryItem *items = (HistoryItem *)calloc(MNOTIFY_HISTORY_MAX, sizeof *items);
    if (!items) return;

    HMENU menu = CreatePopupMenu();
    if (!menu) { free(items); return; }

    int count = toasts_history(items, MNOTIFY_HISTORY_MAX);
    if (count <= 0)
        AppendMenuW(menu, MF_STRING | MF_GRAYED, 0,
                    count < 0 ? L"Notification history is unavailable" : L"No notifications");

    for (int i = 0; i < count; i++) {
        wchar_t label[MNOTIFY_LABEL_CAP * 2];
        escape_ampersands(items[i].label, label, MNOTIFY_LABEL_CAP * 2);

        wchar_t entry[MNOTIFY_LABEL_CAP * 2 + MNOTIFY_WHEN_CAP + 2];
        _snwprintf(entry, sizeof entry / sizeof entry[0], L"%ls\t%ls", label, items[i].when);
        entry[sizeof entry / sizeof entry[0] - 1] = L'\0';
        AppendMenuW(menu, MF_STRING, (UINT_PTR)(i + 1), entry);
    }

    log_msg(LOG_DEBUG, L"menu: opening the history with %d notification(s)", count);
    UINT cmd = track_menu(menu);
    if (cmd >= 1 && (int)cmd <= count) toast_activate(&items[cmd - 1].target);

    free(items);
}
