#ifndef MNOTIFY_H
#define MNOTIFY_H

#include <windows.h>
#include <windowsx.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

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

#define MNOTIFY_COPY_SEND    0x6D6E0001

#define MNOTIFY_APP_CAP      96
#define MNOTIFY_TITLE_CAP    TRAY_TITLE_CAP
#define MNOTIFY_TEXT_CAP     TRAY_INFO_CAP
#define MNOTIFY_MAX_POPUPS   5
#define MNOTIFY_AUMID_CAP    256
#define MNOTIFY_LAUNCH_CAP   TOAST_LAUNCH_CAP

#define MNOTIFY_DEFAULT_TIMEOUT_MS 6000
#define MNOTIFY_LONG_TIMEOUT_MS    25000

typedef enum {
    NOTE_INFO = 0,
    NOTE_WARN,
    NOTE_ERROR,
} NoteKind;

typedef enum {
    CORNER_BOTTOM_RIGHT = 0,
    CORNER_TOP_RIGHT,
    CORNER_BOTTOM_LEFT,
    CORNER_TOP_LEFT,
} Corner;

typedef enum {
    NOTE_FROM_SEND = 0,
    NOTE_FROM_TRAY,
    NOTE_FROM_TOAST,
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
    int         timeout_ms;
    TrayIcon    icon;
    ToastTarget toast;
} Note;

typedef struct {
    uint32_t kind;
    wchar_t  title[MNOTIFY_TITLE_CAP];
    wchar_t  text[MNOTIFY_TEXT_CAP];
} SendPayload;

typedef struct {
    HINSTANCE hinst;
    HWND      control;
    HWND      tray;
    UINT      taskbar_created;
    TrayTable table;
    int       timeout_ms;
    Corner    corner;
} Mnotify;

extern Mnotify mn;

static inline HWND hwnd_of(uint32_t value) {
    return (HWND)(LONG_PTR)(LONG)value;
}

void mnotify_copy_w(wchar_t *out, size_t cap, const wchar_t *src);
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

void toast_activate(const ToastTarget *target);
void toast_focus_pending(void);

bool popup_init(void);
void popup_shutdown(void);
void popup_show(const Note *note);
void popup_hide_for(const TrayIdentity *id);
void popup_dismiss_all(void);

void menu_show_tray(void);

#endif
