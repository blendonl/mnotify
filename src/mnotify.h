#ifndef MNOTIFY_H
#define MNOTIFY_H

#include <windows.h>
#include <windowsx.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#include "config_types.h"
#include "log.h"
#include "toast_xml.h"
#include "tray_proto.h"

#ifndef MNOTIFY_VERSION
#define MNOTIFY_VERSION "0.0.0"
#endif

_Static_assert(sizeof(wchar_t) == sizeof(TrayChar),
               "tray text is UTF-16 and is used as wchar_t directly");

#define MNOTIFY_CLASS        L"mnotify_Window"
#define MNOTIFY_POPUP_CLASS  L"mnotify_Popup"
#define MNOTIFY_TRAY_CLASS   L"Shell_TrayWnd"
#define MNOTIFY_MUTEX        L"Local\\mnotify_singleton"

#define WM_TRAY_QUERY_NOTIFICATION_STATE 0x04EF
#define PRESENTATION_MODE_EVENT L"{A1965210-3A9D-4bca-822B-433645B3F5A2}"
#define DDRAW_EXCLUSIVE_MUTEX   L"Local\\__DDrawExclMode__"

#define WM_MNOTIFY_TRAY_MENU (WM_APP + 1)
#define WM_MNOTIFY_DISMISS   (WM_APP + 2)
#define WM_MNOTIFY_QUIT      (WM_APP + 3)
#define WM_MNOTIFY_TOAST_FOCUS (WM_APP + 4)
#define WM_MNOTIFY_HISTORY   (WM_APP + 5)
#define WM_MNOTIFY_RELOAD    (WM_APP + 6)
#define WM_MNOTIFY_CONFIG_CHANGED (WM_APP + 7)

#define MNOTIFY_HISTORY_TOGGLE 0
#define MNOTIFY_HISTORY_OPEN   1

#define MNOTIFY_COPY_SEND    0x6D6E0001

#define MNOTIFY_APP_CAP      96
#define MNOTIFY_TITLE_CAP    TRAY_TITLE_CAP
#define MNOTIFY_TEXT_CAP     TRAY_INFO_CAP
#define MNOTIFY_MAX_POPUPS   CONFIG_MAX_VISIBLE
#define MNOTIFY_AUMID_CAP    256
#define MNOTIFY_LAUNCH_CAP   TOAST_LAUNCH_CAP
#define MNOTIFY_HISTORY_MAX  200

typedef enum {
    NOTE_FROM_SEND = 0,
    NOTE_FROM_TRAY,
    NOTE_FROM_TOAST,
    NOTE_FROM_BACKLOG,
    NOTE_FROM_MNOTIFY,
} NoteSource;

typedef struct {
    wchar_t         aumid[MNOTIFY_AUMID_CAP];
    wchar_t         exe[MAX_PATH];
    wchar_t         launch[MNOTIFY_LAUNCH_CAP];
    bool            launch_complete;
    ToastActivation activation;
} ToastTarget;

typedef struct {
    wchar_t     app[MNOTIFY_APP_CAP];
    wchar_t     title[MNOTIFY_TITLE_CAP];
    wchar_t     text[MNOTIFY_TEXT_CAP];
    NoteKind    kind;
    NoteSource  source;
    bool        long_duration;
    bool        has_timeout;
    Timeout     timeout;
    bool        has_accent;
    uint32_t    accent;
    bool        internal;
    TrayIcon    icon;
    ToastTarget toast;
} Note;

typedef struct {
    long long   arrival;
    wchar_t     app[MNOTIFY_APP_CAP];
    wchar_t     title[TOAST_TITLE_CAP];
    wchar_t     text[TOAST_BODY_CAP];
    ToastTarget target;
} HistoryItem;

typedef struct {
    wchar_t   aumid[MNOTIFY_AUMID_CAP];
    int       size;
    uint32_t *pixels;
} AppIcon;

typedef struct {
    uint32_t kind;
    wchar_t  title[MNOTIFY_TITLE_CAP];
    wchar_t  text[MNOTIFY_TEXT_CAP];
} SendPayload;

typedef struct {
    bool     timeout_set;
    Timeout  timeout;
    bool     corner_set;
    Corner   corner;
    bool     level_set;
    LogLevel level;
} Overrides;

typedef struct {
    HINSTANCE hinst;
    HWND      control;
    HWND      tray;
    UINT      taskbar_created;
    TrayTable table;
    Config    cfg;
    unsigned  cfg_generation;
    Overrides overrides;
} Mnotify;

extern Mnotify mn;

static inline HWND hwnd_of(uint32_t value) {
    return (HWND)(LONG_PTR)(LONG)value;
}

void mnotify_copy_w(wchar_t *out, size_t cap, const wchar_t *src);
bool mnotify_utf8_to_wide(const char *s, wchar_t *out, size_t cap);
bool mnotify_wide_to_utf8(const wchar_t *s, char *out, size_t cap);
int  mnotify_scale(int px, UINT dpi);

bool tray_host_init(void);
void tray_host_shutdown(void);
bool tray_host_other_tray_exists(void);
void tray_host_notify(const TrayIcon *icon, uint32_t event);
void tray_host_click(const TrayIcon *icon, TrayClick click);
void tray_host_grant_foreground(uint32_t hwnd);
void tray_host_prune(void);
void tray_host_app_name(uint32_t hwnd, wchar_t *out, size_t cap);
QUERY_USER_NOTIFICATION_STATE tray_host_user_state(void);

void toasts_init(void);
void toasts_shutdown(void);
int  toasts_history(HistoryItem *items, int cap);

void toast_activate(const ToastTarget *target);
void toast_focus_pending(void);

void config_init(void);
void config_reload(void);
void config_on_file_changed(unsigned generation);
void config_shutdown(void);
bool config_check(char *out, size_t cap);
bool config_filter(Note *note);

bool popup_init(void);
void popup_shutdown(void);
bool    popup_show(const Note *note);
void    popup_config_changed(void);
Timeout popup_timeout_for(const Note *note);
void popup_hide_for(const TrayIdentity *id);
void popup_dismiss_all(void);

void menu_show_tray(void);

bool history_init(void);
void history_shutdown(void);
void history_show(void);
void history_toggle(void);
void history_close(void);

const AppIcon *icons_find(const wchar_t *aumid, int size);
void           icons_load(const ToastTarget *target, int size);
void           icons_shutdown(void);

#endif
