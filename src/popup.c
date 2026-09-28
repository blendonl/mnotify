#include "mnotify.h"

#include <dwmapi.h>
#include <shellscalingapi.h>

#define POPUP_TIMER_ID        1
#define POPUP_TICK_MS         250
#define POPUP_HOVER_GRACE_MS  1500

#define POPUP_WIDTH           360
#define POPUP_PAD             14
#define POPUP_STRIPE          3
#define POPUP_STRIPE_INSET    9
#define POPUP_LINE_GAP        3
#define POPUP_GAP             10
#define POPUP_MARGIN          16
#define POPUP_BODY_LINES      6

#define FONT_APP_PX           12
#define FONT_TITLE_PX         15
#define FONT_BODY_PX          14

#define COLOR_BG              RGB(0x1e, 0x1e, 0x2e)
#define COLOR_FG              RGB(0xcd, 0xd6, 0xf4)
#define COLOR_DIM             RGB(0xa6, 0xad, 0xc8)
#define COLOR_BORDER          RGB(0x45, 0x47, 0x5a)
#define COLOR_INFO            RGB(0x89, 0xb4, 0xfa)
#define COLOR_WARN            RGB(0xf9, 0xe2, 0xaf)
#define COLOR_ERROR           RGB(0xf3, 0x8b, 0xa8)

#define DWM_CORNER_PREFERENCE 33
#define DWM_BORDER_COLOR      34
#define DWM_CORNER_ROUND      2

typedef enum {
    CLOSE_SILENT = 0,
    CLOSE_TIMEOUT,
    CLOSE_CLICKED,
} CloseReason;

typedef struct {
    HWND      hwnd;
    Note      note;
    ULONGLONG expires;
} Popup;

typedef struct {
    UINT  dpi;
    HFONT app;
    HFONT title;
    HFONT body;
} Fonts;

typedef struct {
    RECT app;
    RECT title;
    RECT body;
    int  height;
} Layout;

static Popup s_popups[MNOTIFY_MAX_POPUPS];
static int   s_count;
static Fonts s_fonts;
static bool  s_class_registered;

static HFONT make_font(int px, int weight, UINT dpi) {
    return CreateFontW(-mnotify_scale(px, dpi), 0, 0, 0, weight, FALSE, FALSE, FALSE,
                       DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                       CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
}

static void fonts_free(void) {
    if (s_fonts.app)   DeleteObject(s_fonts.app);
    if (s_fonts.title) DeleteObject(s_fonts.title);
    if (s_fonts.body)  DeleteObject(s_fonts.body);
    memset(&s_fonts, 0, sizeof s_fonts);
}

static void fonts_for(UINT dpi) {
    if (s_fonts.dpi == dpi && s_fonts.body) return;
    fonts_free();
    s_fonts.dpi   = dpi;
    s_fonts.app   = make_font(FONT_APP_PX,   FW_NORMAL,   dpi);
    s_fonts.title = make_font(FONT_TITLE_PX, FW_SEMIBOLD, dpi);
    s_fonts.body  = make_font(FONT_BODY_PX,  FW_NORMAL,   dpi);
}

static HMONITOR target_monitor(void) {
    POINT pt = { 0, 0 };
    GetCursorPos(&pt);
    return MonitorFromPoint(pt, MONITOR_DEFAULTTOPRIMARY);
}

static UINT monitor_dpi(HMONITOR monitor) {
    UINT x = 96, y = 96;
    if (FAILED(GetDpiForMonitor(monitor, MDT_EFFECTIVE_DPI, &x, &y)) || !x) return 96;
    return x;
}

static COLORREF kind_color(NoteKind kind) {
    switch (kind) {
    case NOTE_WARN:  return COLOR_WARN;
    case NOTE_ERROR: return COLOR_ERROR;
    default:         return COLOR_INFO;
    }
}

static int line_height(HDC dc, HFONT font) {
    HFONT old = (HFONT)SelectObject(dc, font);
    TEXTMETRICW tm;
    GetTextMetricsW(dc, &tm);
    SelectObject(dc, old);
    return tm.tmHeight;
}

static Layout layout_note(HDC dc, const Note *note, int width, UINT dpi) {
    Layout l;
    memset(&l, 0, sizeof l);

    int pad  = mnotify_scale(POPUP_PAD, dpi);
    int gap  = mnotify_scale(POPUP_LINE_GAP, dpi);
    int left = mnotify_scale(POPUP_STRIPE_INSET + POPUP_STRIPE, dpi) + pad;
    int right = width - pad;
    int y = pad;

    if (note->app[0]) {
        SetRect(&l.app, left, y, right, y + line_height(dc, s_fonts.app));
        y = l.app.bottom + gap;
    }

    if (note->title[0]) {
        SetRect(&l.title, left, y, right, y + line_height(dc, s_fonts.title));
        y = l.title.bottom + gap;
    }

    if (note->text[0]) {
        RECT r = { left, y, right, y };
        HFONT old = (HFONT)SelectObject(dc, s_fonts.body);
        DrawTextW(dc, note->text, -1, &r,
                  DT_CALCRECT | DT_WORDBREAK | DT_EDITCONTROL | DT_NOPREFIX);
        SelectObject(dc, old);

        int max_h = line_height(dc, s_fonts.body) * POPUP_BODY_LINES;
        if (r.bottom - r.top > max_h) r.bottom = r.top + max_h;
        r.right = right;
        l.body = r;
        y = r.bottom;
    } else if (y > pad) {
        y -= gap;
    }

    l.height = y + pad;
    return l;
}

static int index_of(HWND hwnd) {
    for (int i = 0; i < s_count; i++)
        if (s_popups[i].hwnd == hwnd) return i;
    return -1;
}

static int index_of_identity(const TrayIdentity *id) {
    for (int i = 0; i < s_count; i++)
        if (s_popups[i].note.source == NOTE_FROM_TRAY && tray_same_identity(&s_popups[i].note.icon.id, id))
            return i;
    return -1;
}

static void relayout(void) {
    if (!s_count) return;

    HMONITOR monitor = target_monitor();
    MONITORINFO mi = { .cbSize = sizeof mi };
    if (!GetMonitorInfoW(monitor, &mi)) return;

    UINT dpi = monitor_dpi(monitor);
    fonts_for(dpi);

    int  width  = mnotify_scale(POPUP_WIDTH, dpi);
    int  margin = mnotify_scale(POPUP_MARGIN, dpi);
    int  gap    = mnotify_scale(POPUP_GAP, dpi);
    bool top    = mn.corner == CORNER_TOP_RIGHT || mn.corner == CORNER_TOP_LEFT;
    bool right  = mn.corner == CORNER_TOP_RIGHT || mn.corner == CORNER_BOTTOM_RIGHT;

    RECT work = mi.rcWork;
    int  x = right ? work.right - margin - width : work.left + margin;
    int  y = top ? work.top + margin : work.bottom - margin;

    HDC screen = GetDC(NULL);
    for (int i = 0; i < s_count; i++) {
        Layout l = layout_note(screen, &s_popups[i].note, width, dpi);
        int py = top ? y : y - l.height;
        SetWindowPos(s_popups[i].hwnd, HWND_TOPMOST, x, py, width, l.height,
                     SWP_NOACTIVATE | SWP_SHOWWINDOW);
        InvalidateRect(s_popups[i].hwnd, NULL, FALSE);
        y = top ? y + l.height + gap : y - l.height - gap;
    }
    ReleaseDC(NULL, screen);
}

static void report_close(const Note *note, CloseReason why) {
    if (note->source == NOTE_FROM_TOAST && why == CLOSE_CLICKED) toast_activate(&note->toast);
    if (note->source == NOTE_FROM_BACKLOG && why == CLOSE_CLICKED)
        PostMessageW(mn.control, WM_MNOTIFY_HISTORY, MNOTIFY_HISTORY_OPEN, 0);
    if (note->source != NOTE_FROM_TRAY) return;

    tray_balloon_closed(&mn.table, &note->icon.id);

    int t = tray_find(&mn.table, &note->icon.id);
    const TrayIcon *icon = t >= 0 ? &mn.table.icons[t] : &note->icon;

    if (why == CLOSE_TIMEOUT) {
        tray_host_notify(icon, TRAY_NIN_BALLOONTIMEOUT);
    } else if (why == CLOSE_CLICKED) {
        tray_host_grant_foreground(icon->id.hwnd);
        tray_host_notify(icon, TRAY_NIN_BALLOONUSERCLICK);
    }
}

static void close_at(int i, CloseReason why) {
    if (i < 0 || i >= s_count) return;

    Popup gone = s_popups[i];
    memmove(&s_popups[i], &s_popups[i + 1], (size_t)(s_count - i - 1) * sizeof s_popups[0]);
    s_count--;

    KillTimer(gone.hwnd, POPUP_TIMER_ID);
    DestroyWindow(gone.hwnd);
    report_close(&gone.note, why);
    relayout();
}

static bool cursor_inside(HWND hwnd) {
    POINT pt;
    RECT  r;
    return GetCursorPos(&pt) && GetWindowRect(hwnd, &r) && PtInRect(&r, pt);
}

static void paint(HWND hwnd, const Popup *p) {
    PAINTSTRUCT ps;
    HDC dc = BeginPaint(hwnd, &ps);

    RECT client;
    GetClientRect(hwnd, &client);
    int w = client.right, h = client.bottom;

    HDC     mem = CreateCompatibleDC(dc);
    HBITMAP bmp = CreateCompatibleBitmap(dc, w, h);
    HGDIOBJ old_bmp = SelectObject(mem, bmp);

    UINT dpi = s_fonts.dpi ? s_fonts.dpi : 96;
    Layout l = layout_note(mem, &p->note, w, dpi);

    HBRUSH bg = CreateSolidBrush(COLOR_BG);
    FillRect(mem, &client, bg);
    DeleteObject(bg);

    int inset = mnotify_scale(POPUP_STRIPE_INSET, dpi);
    int pad   = mnotify_scale(POPUP_PAD, dpi);
    RECT stripe = { inset, pad, inset + mnotify_scale(POPUP_STRIPE, dpi), h - pad };
    HBRUSH accent = CreateSolidBrush(kind_color(p->note.kind));
    FillRect(mem, &stripe, accent);
    DeleteObject(accent);

    SetBkMode(mem, TRANSPARENT);
    HGDIOBJ old_font = SelectObject(mem, s_fonts.app);

    if (p->note.app[0]) {
        SetTextColor(mem, COLOR_DIM);
        DrawTextW(mem, p->note.app, -1, &l.app,
                  DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
    }

    if (p->note.title[0]) {
        SelectObject(mem, s_fonts.title);
        SetTextColor(mem, COLOR_FG);
        DrawTextW(mem, p->note.title, -1, &l.title,
                  DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
    }

    if (p->note.text[0]) {
        SelectObject(mem, s_fonts.body);
        SetTextColor(mem, p->note.title[0] ? COLOR_DIM : COLOR_FG);
        DrawTextW(mem, p->note.text, -1, &l.body,
                  DT_WORDBREAK | DT_EDITCONTROL | DT_END_ELLIPSIS | DT_NOPREFIX);
    }

    SelectObject(mem, old_font);
    BitBlt(dc, 0, 0, w, h, mem, 0, 0, SRCCOPY);
    SelectObject(mem, old_bmp);
    DeleteObject(bmp);
    DeleteDC(mem);
    EndPaint(hwnd, &ps);
}

static LRESULT CALLBACK popup_wndproc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    int i = index_of(hwnd);

    switch (msg) {
    case WM_MOUSEACTIVATE:
        return MA_NOACTIVATE;

    case WM_SETCURSOR:
        SetCursor(LoadCursorW(NULL, IDC_HAND));
        return TRUE;

    case WM_ERASEBKGND:
        return 1;

    case WM_DPICHANGED:
        return 0;

    case WM_PAINT:
        if (i < 0) break;
        paint(hwnd, &s_popups[i]);
        return 0;

    case WM_TIMER:
        if (i < 0 || wp != POPUP_TIMER_ID) break;
        if (cursor_inside(hwnd)) {
            ULONGLONG floor = GetTickCount64() + POPUP_HOVER_GRACE_MS;
            if (s_popups[i].expires < floor) s_popups[i].expires = floor;
        } else if (GetTickCount64() >= s_popups[i].expires) {
            close_at(i, CLOSE_TIMEOUT);
        }
        return 0;

    case WM_LBUTTONUP:
        if (i < 0) break;
        close_at(i, s_popups[i].note.source == NOTE_FROM_SEND ? CLOSE_SILENT : CLOSE_CLICKED);
        return 0;

    case WM_RBUTTONUP:
        if (i < 0) break;
        close_at(i, CLOSE_TIMEOUT);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

static HWND create_popup_window(void) {
    HWND hwnd = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_NOACTIVATE,
                                MNOTIFY_POPUP_CLASS, L"mnotify", WS_POPUP,
                                0, 0, 1, 1, NULL, NULL, mn.hinst, NULL);
    if (!hwnd) {
        log_err(L"popup: CreateWindowEx failed (%lu)", GetLastError());
        return NULL;
    }

    DWORD corner = DWM_CORNER_ROUND;
    DwmSetWindowAttribute(hwnd, DWM_CORNER_PREFERENCE, &corner, sizeof corner);
    COLORREF border = COLOR_BORDER;
    DwmSetWindowAttribute(hwnd, DWM_BORDER_COLOR, &border, sizeof border);
    return hwnd;
}

void popup_show(const Note *note) {
    int       timeout = note->timeout_ms > 0 ? note->timeout_ms : mn.timeout_ms;
    ULONGLONG expires = GetTickCount64() + (ULONGLONG)timeout;

    int existing = note->source == NOTE_FROM_TRAY ? index_of_identity(&note->icon.id) : -1;
    if (existing >= 0) {
        Popup updated = s_popups[existing];
        updated.note    = *note;
        updated.expires = expires;
        memmove(&s_popups[1], &s_popups[0], (size_t)existing * sizeof s_popups[0]);
        s_popups[0] = updated;
        relayout();
        return;
    }

    if (s_count == MNOTIFY_MAX_POPUPS) close_at(s_count - 1, CLOSE_TIMEOUT);

    HWND hwnd = create_popup_window();
    if (!hwnd) return;

    memmove(&s_popups[1], &s_popups[0], (size_t)s_count * sizeof s_popups[0]);
    s_popups[0].hwnd    = hwnd;
    s_popups[0].note    = *note;
    s_popups[0].expires = expires;
    s_count++;

    SetTimer(hwnd, POPUP_TIMER_ID, POPUP_TICK_MS, NULL);
    relayout();
}

void popup_hide_for(const TrayIdentity *id) {
    int i;
    while ((i = index_of_identity(id)) >= 0) close_at(i, CLOSE_SILENT);
}

void popup_dismiss_all(void) {
    while (s_count) close_at(0, CLOSE_TIMEOUT);
}

bool popup_init(void) {
    WNDCLASSEXW wc = {
        .cbSize        = sizeof wc,
        .lpfnWndProc   = popup_wndproc,
        .hInstance     = mn.hinst,
        .hCursor       = LoadCursorW(NULL, IDC_HAND),
        .lpszClassName = MNOTIFY_POPUP_CLASS,
    };
    if (!RegisterClassExW(&wc)) {
        log_err(L"popup: RegisterClassEx failed (%lu)", GetLastError());
        return false;
    }
    s_class_registered = true;
    return true;
}

void popup_shutdown(void) {
    while (s_count) {
        s_count--;
        KillTimer(s_popups[s_count].hwnd, POPUP_TIMER_ID);
        DestroyWindow(s_popups[s_count].hwnd);
    }
    fonts_free();
    if (s_class_registered) {
        UnregisterClassW(MNOTIFY_POPUP_CLASS, mn.hinst);
        s_class_registered = false;
    }
}
