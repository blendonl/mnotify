#include "mnotify.h"
#include "anim.h"

#include <dwmapi.h>
#include <shellscalingapi.h>

#define POPUP_TIMER_ID        1
#define POPUP_TICK_MS         250
#define ANIM_TIMER_ID         0x414E
#define ANIM_TICK_MS          10

#define POPUP_STRIPE_INSET    9
#define POPUP_SLIDE_PX        32

#define DWM_CORNER_PREFERENCE 33
#define DWM_BORDER_COLOR      34
#define DWM_CORNER_SQUARE     1
#define DWM_CORNER_ROUND      2
#define DWM_CORNER_ROUND_SMALL 3
#define DWM_COLOR_NONE        0xFFFFFFFE

typedef enum {
    CLOSE_SILENT = 0,
    CLOSE_TIMEOUT,
    CLOSE_CLICKED,
} CloseReason;

typedef struct {
    HWND      hwnd;
    Note      note;
    ULONGLONG expires;
    bool      placed;
    int       height;
    POINT     from;
    POINT     cur;
    POINT     goal;
    int       alpha_from;
    int       alpha;
    int       alpha_goal;
    double    start;
} Popup;

typedef struct {
    UINT     dpi;
    unsigned generation;
    HFONT    app;
    HFONT    title;
    HFONT    body;
} Fonts;

typedef struct {
    RECT app;
    RECT title;
    RECT body;
    int  height;
} Layout;

static Popup    s_popups[MNOTIFY_MAX_POPUPS];
static int      s_count;
static Popup    s_closing[MNOTIFY_MAX_POPUPS];
static int      s_closing_count;
static Fonts    s_fonts;
static HMONITOR s_monitor;
static bool     s_animating;
static bool     s_layout_held;
static bool     s_class_registered;

static COLORREF rgb(uint32_t c) {
    return RGB((c >> 16) & 0xFF, (c >> 8) & 0xFF, c & 0xFF);
}

static double now_ms(void) {
    static LARGE_INTEGER freq;
    if (!freq.QuadPart) QueryPerformanceFrequency(&freq);
    LARGE_INTEGER count;
    QueryPerformanceCounter(&count);
    return (double)count.QuadPart * 1000.0 / (double)freq.QuadPart;
}

static HFONT make_font(int px, int weight, UINT dpi) {
    wchar_t face[LF_FACESIZE];
    mnotify_utf8_to_wide(mn.cfg.theme.font, face, LF_FACESIZE);
    return CreateFontW(-mnotify_scale(px, dpi), 0, 0, 0, weight, FALSE, FALSE, FALSE,
                       DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                       CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, face);
}

static void fonts_free(void) {
    if (s_fonts.app)   DeleteObject(s_fonts.app);
    if (s_fonts.title) DeleteObject(s_fonts.title);
    if (s_fonts.body)  DeleteObject(s_fonts.body);
    memset(&s_fonts, 0, sizeof s_fonts);
}

static void fonts_for(UINT dpi) {
    if (s_fonts.dpi == dpi && s_fonts.generation == mn.cfg_generation && s_fonts.body) return;
    fonts_free();
    s_fonts.dpi        = dpi;
    s_fonts.generation = mn.cfg_generation;
    s_fonts.app        = make_font(mn.cfg.theme.app_size,   FW_NORMAL,   dpi);
    s_fonts.title      = make_font(mn.cfg.theme.title_size, FW_SEMIBOLD, dpi);
    s_fonts.body       = make_font(mn.cfg.theme.font_size,  FW_NORMAL,   dpi);
}

static HMONITOR target_monitor(void) {
    POINT pt = { 0, 0 };
    if (mn.cfg.position.monitor == MONITOR_CURSOR) GetCursorPos(&pt);
    return MonitorFromPoint(pt, MONITOR_DEFAULTTOPRIMARY);
}

static UINT monitor_dpi(HMONITOR monitor) {
    UINT x = 96, y = 96;
    if (FAILED(GetDpiForMonitor(monitor, MDT_EFFECTIVE_DPI, &x, &y)) || !x) return 96;
    return x;
}

static COLORREF accent_color(const Note *note) {
    if (note->has_accent) return rgb(note->accent);
    switch (note->kind) {
    case NOTE_WARN:  return rgb(mn.cfg.theme.warn);
    case NOTE_ERROR: return rgb(mn.cfg.theme.error);
    default:         return rgb(mn.cfg.theme.info);
    }
}

static bool corner_is_top(Corner corner) {
    return corner == CORNER_TOP_RIGHT || corner == CORNER_TOP_LEFT || corner == CORNER_TOP_CENTER;
}

static int line_height(HDC dc, HFONT font) {
    HFONT old = (HFONT)SelectObject(dc, font);
    TEXTMETRICW tm;
    GetTextMetricsW(dc, &tm);
    SelectObject(dc, old);
    return tm.tmHeight;
}

static int text_left(UINT dpi) {
    int pad = mnotify_scale(mn.cfg.theme.padding, dpi);
    if (mn.cfg.theme.accent == ACCENT_NONE) return pad;
    return mnotify_scale(POPUP_STRIPE_INSET + mn.cfg.theme.accent_width, dpi) + pad;
}

static Layout layout_note(HDC dc, const Note *note, int width, UINT dpi) {
    Layout l;
    memset(&l, 0, sizeof l);

    int pad   = mnotify_scale(mn.cfg.theme.padding, dpi);
    int gap   = mnotify_scale(mn.cfg.theme.line_spacing, dpi);
    int left  = text_left(dpi);
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

        int max_h = line_height(dc, s_fonts.body) * mn.cfg.theme.body_lines;
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

static Popup *find_popup(HWND hwnd) {
    int i = index_of(hwnd);
    if (i >= 0) return &s_popups[i];
    for (int j = 0; j < s_closing_count; j++)
        if (s_closing[j].hwnd == hwnd) return &s_closing[j];
    return NULL;
}

static int index_of_identity(const TrayIdentity *id) {
    for (int i = 0; i < s_count; i++)
        if (s_popups[i].note.source == NOTE_FROM_TRAY && tray_same_identity(&s_popups[i].note.icon.id, id))
            return i;
    return -1;
}

static void set_alpha(Popup *p, int alpha) {
    p->alpha = alpha;
    SetLayeredWindowAttributes(p->hwnd, 0, (BYTE)alpha, LWA_ALPHA);
}

static bool step(Popup *p, double now) {
    const AnimationConfig *a = &mn.cfg.animation;
    double t = anim_ease(a->easing, anim_progress(now - p->start, a->duration));

    POINT next = { anim_lerp(p->from.x, p->goal.x, t), anim_lerp(p->from.y, p->goal.y, t) };
    if (next.x != p->cur.x || next.y != p->cur.y) {
        p->cur = next;
        SetWindowPos(p->hwnd, NULL, next.x, next.y, 0, 0,
                     SWP_NOZORDER | SWP_NOSIZE | SWP_NOACTIVATE);
    }

    int alpha = anim_lerp(p->alpha_from, p->alpha_goal, t);
    if (alpha != p->alpha) set_alpha(p, alpha);

    return p->cur.x != p->goal.x || p->cur.y != p->goal.y || p->alpha != p->alpha_goal;
}

static void anim_stop(void) {
    if (!s_animating) return;
    KillTimer(mn.control, ANIM_TIMER_ID);
    s_animating = false;
}

static void remove_closing(int i) {
    DestroyWindow(s_closing[i].hwnd);
    memmove(&s_closing[i], &s_closing[i + 1], (size_t)(s_closing_count - i - 1) * sizeof s_closing[0]);
    s_closing_count--;
}

static void CALLBACK anim_tick(HWND hwnd, UINT msg, UINT_PTR id, DWORD time) {
    (void)hwnd; (void)msg; (void)id; (void)time;

    double now  = now_ms();
    bool   busy = false;
    for (int i = 0; i < s_count; i++)
        if (step(&s_popups[i], now)) busy = true;

    for (int i = s_closing_count - 1; i >= 0; i--) {
        if (step(&s_closing[i], now)) busy = true;
        else                          remove_closing(i);
    }

    if (!busy) anim_stop();
}

static void anim_kick(void) {
    if (s_animating || !mn.control) return;
    if (mn.cfg.animation.duration <= 0 && !s_closing_count) return;
    if (SetTimer(mn.control, ANIM_TIMER_ID, ANIM_TICK_MS, anim_tick)) s_animating = true;
}

static void begin(Popup *p, double now) {
    p->from       = p->cur;
    p->alpha_from = p->alpha;
    p->start      = now;
}

static int slide_distance(void) {
    return mnotify_scale(POPUP_SLIDE_PX, s_fonts.dpi ? s_fonts.dpi : 96);
}

static void slide(POINT *pt) {
    int dx, dy;
    anim_slide_offset(mn.cfg.position.corner, slide_distance(), &dx, &dy);
    pt->x += dx;
    pt->y += dy;
}

static void place_new(Popup *p, double now) {
    const AnimationConfig *a = &mn.cfg.animation;
    p->cur = p->goal;
    int alpha = p->alpha_goal;
    if (a->duration > 0 && a->open != ANIM_NONE) {
        alpha = 0;
        if (a->open == ANIM_SLIDE) slide(&p->cur);
    }
    set_alpha(p, alpha);
    begin(p, now);
    p->placed = true;
}

static void snap(Popup *p, double now) {
    p->cur = p->goal;
    if (p->alpha != p->alpha_goal) set_alpha(p, p->alpha_goal);
    begin(p, now);
}

static void relayout(void) {
    if (s_layout_held || !s_count) return;

    HMONITOR monitor = target_monitor();
    MONITORINFO mi = { .cbSize = sizeof mi };
    if (!GetMonitorInfoW(monitor, &mi)) return;
    bool changed_screen = s_monitor && s_monitor != monitor;
    s_monitor = monitor;

    UINT dpi = monitor_dpi(monitor);
    fonts_for(dpi);

    const PositionConfig *pos = &mn.cfg.position;
    int  width  = mnotify_scale(mn.cfg.theme.width, dpi);
    int  margin = mnotify_scale(pos->margin, dpi);
    int  gap    = mnotify_scale(pos->spacing, dpi);
    bool top    = corner_is_top(pos->corner);
    bool snaps  = changed_screen || mn.cfg.animation.duration <= 0;

    RECT work = mi.rcWork;
    int  x;
    switch (pos->corner) {
    case CORNER_TOP_RIGHT:
    case CORNER_BOTTOM_RIGHT: x = work.right - margin - width;                     break;
    case CORNER_TOP_LEFT:
    case CORNER_BOTTOM_LEFT:  x = work.left + margin;                              break;
    default:                  x = work.left + (work.right - work.left - width) / 2; break;
    }
    int y = top ? work.top + margin : work.bottom - margin;

    double now    = now_ms();
    HDC    screen = GetDC(NULL);
    for (int i = 0; i < s_count; i++) {
        Popup *p = &s_popups[i];
        Layout l = layout_note(screen, &p->note, width, dpi);
        p->height     = l.height;
        p->goal.x     = x;
        p->goal.y     = top ? y : y - l.height;
        p->alpha_goal = mn.cfg.theme.opacity;

        if (!p->placed) place_new(p, now);
        else if (snaps) snap(p, now);
        else            begin(p, now);

        SetWindowPos(p->hwnd, HWND_TOPMOST, p->cur.x, p->cur.y, width, l.height,
                     SWP_NOACTIVATE | SWP_SHOWWINDOW);
        InvalidateRect(p->hwnd, NULL, FALSE);
        y = top ? y + l.height + gap : y - l.height - gap;
    }
    ReleaseDC(NULL, screen);
    anim_kick();
}

static void begin_closing(const Popup *gone) {
    const AnimationConfig *a = &mn.cfg.animation;
    if (!gone->placed || a->duration <= 0 || a->close == ANIM_NONE) {
        DestroyWindow(gone->hwnd);
        return;
    }

    if (s_closing_count == MNOTIFY_MAX_POPUPS) remove_closing(0);
    Popup *p = &s_closing[s_closing_count++];
    *p = *gone;

    LONG_PTR ex = GetWindowLongPtrW(p->hwnd, GWL_EXSTYLE);
    SetWindowLongPtrW(p->hwnd, GWL_EXSTYLE, ex | WS_EX_TRANSPARENT);

    p->goal       = p->cur;
    p->alpha_goal = 0;
    if (a->close == ANIM_SLIDE) slide(&p->goal);
    begin(p, now_ms());
    anim_kick();
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
    begin_closing(&gone);
    relayout();
    report_close(&gone.note, why);
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

    const ThemeConfig *theme = &mn.cfg.theme;
    UINT dpi = s_fonts.dpi ? s_fonts.dpi : 96;
    Layout l = layout_note(mem, &p->note, w, dpi);

    HBRUSH bg = CreateSolidBrush(rgb(theme->bg));
    FillRect(mem, &client, bg);
    DeleteObject(bg);

    if (theme->accent != ACCENT_NONE) {
        int inset = mnotify_scale(POPUP_STRIPE_INSET, dpi);
        int pad   = mnotify_scale(theme->padding, dpi);
        RECT stripe = { inset, pad, inset + mnotify_scale(theme->accent_width, dpi), h - pad };
        HBRUSH accent = CreateSolidBrush(accent_color(&p->note));
        FillRect(mem, &stripe, accent);
        DeleteObject(accent);
    }

    SetBkMode(mem, TRANSPARENT);
    HGDIOBJ old_font = SelectObject(mem, s_fonts.app);

    if (p->note.app[0]) {
        SetTextColor(mem, rgb(theme->dim));
        DrawTextW(mem, p->note.app, -1, &l.app,
                  DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
    }

    if (p->note.title[0]) {
        SelectObject(mem, s_fonts.title);
        SetTextColor(mem, rgb(theme->fg));
        DrawTextW(mem, p->note.title, -1, &l.title,
                  DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
    }

    if (p->note.text[0]) {
        SelectObject(mem, s_fonts.body);
        SetTextColor(mem, rgb(p->note.title[0] ? theme->dim : theme->fg));
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

    case WM_PAINT: {
        Popup *p = find_popup(hwnd);
        if (!p) break;
        paint(hwnd, p);
        return 0;
    }

    case WM_TIMER:
        if (i < 0 || wp != POPUP_TIMER_ID) break;
        if (mn.cfg.behavior.pause_on_hover && cursor_inside(hwnd)) {
            ULONGLONG floor = GetTickCount64() + (ULONGLONG)mn.cfg.behavior.hover_grace;
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

static void style_window(HWND hwnd) {
    const ThemeConfig *theme = &mn.cfg.theme;

    DWORD corner = theme->corners == CORNERS_SQUARE ? DWM_CORNER_SQUARE
                 : theme->corners == CORNERS_SMALL  ? DWM_CORNER_ROUND_SMALL
                 :                                    DWM_CORNER_ROUND;
    DwmSetWindowAttribute(hwnd, DWM_CORNER_PREFERENCE, &corner, sizeof corner);

    COLORREF border = theme->border_none ? DWM_COLOR_NONE : rgb(theme->border);
    DwmSetWindowAttribute(hwnd, DWM_BORDER_COLOR, &border, sizeof border);
}

static HWND create_popup_window(void) {
    HWND hwnd = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_NOACTIVATE | WS_EX_LAYERED,
                                MNOTIFY_POPUP_CLASS, L"mnotify", WS_POPUP,
                                0, 0, 1, 1, NULL, NULL, mn.hinst, NULL);
    if (!hwnd) {
        log_err(L"popup: CreateWindowEx failed (%lu)", GetLastError());
        return NULL;
    }

    SetLayeredWindowAttributes(hwnd, 0, 0, LWA_ALPHA);
    style_window(hwnd);
    return hwnd;
}

Timeout popup_timeout_for(const Note *note) {
    if (note->has_timeout) return note->timeout;
    return note->long_duration ? mn.cfg.behavior.long_timeout : mn.cfg.behavior.timeout;
}

static ULONGLONG expiry_for(const Note *note) {
    Timeout t = popup_timeout_for(note);
    if (t.forever) return ULLONG_MAX;
    return GetTickCount64() + (ULONGLONG)t.ms;
}

static void drop(const Note *note) {
    if (note->source != NOTE_FROM_TRAY) return;

    int existing = index_of_identity(&note->icon.id);
    if (existing >= 0) close_at(existing, CLOSE_TIMEOUT);
    else               report_close(note, CLOSE_TIMEOUT);
}

bool popup_show(const Note *shown) {
    Note note = *shown;
    if (!note.internal && !config_filter(&note)) {
        drop(&note);
        return false;
    }

    ULONGLONG expires  = expiry_for(&note);
    int       existing = note.source == NOTE_FROM_TRAY ? index_of_identity(&note.icon.id) : -1;
    if (existing >= 0) {
        Popup updated = s_popups[existing];
        updated.note    = note;
        updated.expires = expires;
        memmove(&s_popups[1], &s_popups[0], (size_t)existing * sizeof s_popups[0]);
        s_popups[0] = updated;
        relayout();
        return true;
    }

    while (s_count >= mn.cfg.behavior.max_visible) close_at(s_count - 1, CLOSE_TIMEOUT);

    HWND hwnd = create_popup_window();
    if (!hwnd) return false;

    memmove(&s_popups[1], &s_popups[0], (size_t)s_count * sizeof s_popups[0]);
    Popup *p = &s_popups[0];
    memset(p, 0, sizeof *p);
    p->hwnd    = hwnd;
    p->note    = note;
    p->expires = expires;
    s_count++;

    SetTimer(hwnd, POPUP_TIMER_ID, POPUP_TICK_MS, NULL);
    relayout();
    return true;
}

void popup_hide_for(const TrayIdentity *id) {
    int i;
    while ((i = index_of_identity(id)) >= 0) close_at(i, CLOSE_SILENT);
}

void popup_dismiss_all(void) {
    s_layout_held = true;
    while (s_count) close_at(0, CLOSE_TIMEOUT);
    s_layout_held = false;
    relayout();
}

void popup_config_changed(void) {
    fonts_free();
    for (int i = 0; i < s_count; i++)         style_window(s_popups[i].hwnd);
    for (int i = 0; i < s_closing_count; i++) style_window(s_closing[i].hwnd);

    s_layout_held = true;
    while (s_count > mn.cfg.behavior.max_visible) close_at(s_count - 1, CLOSE_TIMEOUT);
    s_layout_held = false;
    relayout();
    anim_kick();
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
    anim_stop();
    while (s_count) {
        s_count--;
        KillTimer(s_popups[s_count].hwnd, POPUP_TIMER_ID);
        DestroyWindow(s_popups[s_count].hwnd);
    }
    while (s_closing_count) DestroyWindow(s_closing[--s_closing_count].hwnd);
    fonts_free();
    if (s_class_registered) {
        UnregisterClassW(MNOTIFY_POPUP_CLASS, mn.hinst);
        s_class_registered = false;
    }
}
