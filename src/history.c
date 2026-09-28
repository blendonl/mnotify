#include "mnotify.h"
#include "history_list.h"

#include <dwmapi.h>
#include <shellscalingapi.h>

#define HISTORY_CLASS        L"mnotify_History"
#define WM_HISTORY_ICON      (WM_APP + 1)
#define SEARCH_ID            1
#define QUERY_CAP            128
#define LABEL_CAP            64
#define MEASURE_CAP          512
#define DAY_TICKS            864000000000LL
#define MINUTE_TICKS         600000000LL

#define PANEL_WIDTH          400
#define PANEL_MAX_HEIGHT     640
#define PANEL_MARGIN         16
#define PANEL_PAD            12
#define HEADER_HEIGHT        50
#define COUNT_HEIGHT         20
#define COUNT_PAD            8
#define SEARCH_HEIGHT        36
#define SEARCH_RADIUS        8
#define SEARCH_INSET         12
#define SEARCH_GLYPH         16
#define SEARCH_GLYPH_GAP     10
#define LIST_GAP             4
#define SECTION_HEIGHT       32
#define SECTION_INSET        4
#define SECTION_BASELINE     8
#define CARD_PAD_X           12
#define CARD_PAD_Y           10
#define CARD_GAP             6
#define CARD_RADIUS          8
#define ICON_PX              32
#define ICON_RADIUS          8
#define ICON_GAP             12
#define LINE_GAP             2
#define TIME_GAP             8
#define LIST_BOTTOM_PAD      8
#define FOOTER_HEIGHT        40
#define KEYCAP_PAD           6
#define KEYCAP_HEIGHT        20
#define KEYCAP_RADIUS        4
#define HINT_GAP             6
#define HINT_SPACING         16
#define THUMB_WIDTH          4
#define THUMB_MIN            24
#define THUMB_INSET          4
#define WHEEL_STEP           72
#define PAGE_CARD_HEIGHT     80
#define EMPTY_GAP            6
#define EMPTY_LIFT           16

#define FONT_HEADER_PX       16
#define FONT_COUNT_PX        12
#define FONT_SEARCH_PX       14
#define FONT_SECTION_PX      12
#define FONT_APP_PX          12
#define FONT_TITLE_PX        14
#define FONT_BODY_PX         13
#define FONT_HINT_PX         12
#define FONT_GLYPH_PX        14
#define FONT_EMPTY_GLYPH_PX  28
#define FONT_MONOGRAM_PX     15

#define GLYPH_SEARCH         L""
#define GLYPH_CLEAR          L""
#define GLYPH_BELL           L""
#define GLYPH_WARNING        L""

#define DWM_CORNER_PREFERENCE 33
#define DWM_BORDER_COLOR      34
#define DWM_CORNER_ROUND      2

#define COLOR_PANEL          RGB(0x18, 0x18, 0x25)
#define COLOR_CARD           RGB(0x1e, 0x1e, 0x2e)
#define COLOR_FG             RGB(0xcd, 0xd6, 0xf4)
#define COLOR_DIM            RGB(0xa6, 0xad, 0xc8)
#define COLOR_BORDER         RGB(0x45, 0x47, 0x5a)
#define COLOR_INFO           RGB(0x89, 0xb4, 0xfa)
#define COLOR_RAISED         RGB(0x31, 0x32, 0x44)
#define COLOR_MUTED          RGB(0x7f, 0x84, 0x9c)
#define COLOR_FAINT          RGB(0x6c, 0x70, 0x86)
#define COLOR_MONOGRAM_TEXT  RGB(0x11, 0x11, 0x1b)

static const COLORREF MONOGRAM_COLORS[] = {
    RGB(0x89, 0xb4, 0xfa), RGB(0xcb, 0xa6, 0xf7), RGB(0xa6, 0xe3, 0xa1), RGB(0xfa, 0xb3, 0x87),
    RGB(0x94, 0xe2, 0xd5), RGB(0xf5, 0xc2, 0xe7), RGB(0xf9, 0xe2, 0xaf), RGB(0x74, 0xc7, 0xec),
};

typedef enum {
    ROW_SECTION = 0,
    ROW_CARD,
} RowKind;

typedef struct {
    RowKind kind;
    int     item;
    int     top;
    int     height;
    int     line1;
    int     line1_len;
    int     line2;
    int     body_lines;
} Row;

typedef struct {
    HDC       dc;
    HBITMAP   bitmap;
    HGDIOBJ   previous;
    uint32_t *px;
    int       w;
    int       h;
} Canvas;

typedef struct {
    HFONT header;
    HFONT count;
    HFONT search;
    HFONT section;
    HFONT app;
    HFONT title;
    HFONT body;
    HFONT hint;
    HFONT glyph;
    HFONT empty_glyph;
    HFONT monogram;
} Fonts;

typedef struct {
    int app;
    int title;
    int body;
    int search;
} Metrics;

typedef struct {
    RECT header;
    RECT search;
    RECT clear;
    RECT list;
    RECT footer;
} Frame;

typedef struct {
    HWND         hwnd;
    HWND         edit;
    HWND         previous;
    UINT         dpi;
    Fonts        fonts;
    Metrics      metrics;
    HBRUSH       search_brush;
    Canvas       canvas;
    Frame        frame;
    HistoryItem *items;
    long long   *days;
    int          count;
    Row         *rows;
    int          row_count;
    int         *cards;
    int          shown;
    int          content;
    int          scroll;
    int          selected;
    int          pressed;
    int          icon_cursor;
    bool         filtering;
    bool         clear_hot;
    bool         tracking;
    bool         closing;
    POINT        mouse;
    long long    now;
    long long    today;
    wchar_t      query[QUERY_CAP];
} Panel;

static Panel          s_panel;
static bool           s_opening;
static WNDPROC        s_edit_proc;
static bool           s_class_registered;
static const wchar_t *s_glyph_face = L"Segoe MDL2 Assets";

static int scaled(int px) {
    return mnotify_scale(px, s_panel.dpi);
}

static long long filetime_value(FILETIME ft) {
    return (long long)(((ULONGLONG)ft.dwHighDateTime << 32) | ft.dwLowDateTime);
}

static bool local_time(long long utc, SYSTEMTIME *out) {
    FILETIME   ft = { (DWORD)utc, (DWORD)((ULONGLONG)utc >> 32) };
    SYSTEMTIME at;
    return FileTimeToSystemTime(&ft, &at) && SystemTimeToTzSpecificLocalTime(NULL, &at, out);
}

static long long day_number(SYSTEMTIME st) {
    st.wHour = st.wMinute = st.wSecond = st.wMilliseconds = 0;
    FILETIME ft;
    return SystemTimeToFileTime(&st, &ft) ? filetime_value(ft) / DAY_TICKS : 0;
}

static long long arrival_day(long long arrival) {
    SYSTEMTIME local;
    return local_time(arrival, &local) ? day_number(local) : 0;
}

static void flatten(wchar_t *text) {
    for (; *text; text++)
        if (*text < L' ') *text = L' ';
}

static COLORREF mix(COLORREF a, COLORREF b, int a_weight) {
    int b_weight = 255 - a_weight;
    return RGB((GetRValue(a) * a_weight + GetRValue(b) * b_weight) / 255,
               (GetGValue(a) * a_weight + GetGValue(b) * b_weight) / 255,
               (GetBValue(a) * a_weight + GetBValue(b) * b_weight) / 255);
}

static uint32_t pixel_of(COLORREF c) {
    return (uint32_t)GetRValue(c) << 16 | (uint32_t)GetGValue(c) << 8 | GetBValue(c);
}

static void blend(uint32_t *dst, uint32_t src, unsigned alpha) {
    if (alpha >= 255) { *dst = src; return; }

    uint32_t d   = *dst;
    unsigned inv = 255 - alpha;
    uint32_t r = ((src >> 16 & 0xFF) * alpha + (d >> 16 & 0xFF) * inv + 127) / 255;
    uint32_t g = ((src >> 8  & 0xFF) * alpha + (d >> 8  & 0xFF) * inv + 127) / 255;
    uint32_t b = ((src       & 0xFF) * alpha + (d       & 0xFF) * inv + 127) / 255;
    *dst = r << 16 | g << 8 | b;
}

static bool clip_to_canvas(RECT r, RECT clip, RECT *out) {
    RECT canvas = { 0, 0, s_panel.canvas.w, s_panel.canvas.h };
    return IntersectRect(out, &r, &clip) && IntersectRect(out, out, &canvas);
}

static void paint_shape(RECT r, int radius, int ring, COLORREF color, RECT clip) {
    RECT area;
    if (!clip_to_canvas(r, clip, &area)) return;
    GdiFlush();

    HistRect shape = { r.left, r.top, r.right, r.bottom };
    uint32_t src   = pixel_of(color);
    for (int y = area.top; y < area.bottom; y++) {
        uint32_t *row   = s_panel.canvas.px + (size_t)y * (size_t)s_panel.canvas.w;
        bool      solid = !ring && y >= r.top + radius && y < r.bottom - radius;
        for (int x = area.left; x < area.right; x++) {
            unsigned alpha = solid ? 255
                           : ring  ? hist_ring_coverage(x, y, shape, radius, ring)
                                   : hist_round_coverage(x, y, shape, radius);
            if (alpha) blend(&row[x], src, alpha);
        }
    }
}

static void paint_rect(RECT r, COLORREF color) {
    RECT all = { 0, 0, s_panel.canvas.w, s_panel.canvas.h };
    paint_shape(r, 0, 0, color, all);
}

static void paint_pixels(const uint32_t *px, int size, int left, int top, RECT clip) {
    RECT area;
    RECT r = { left, top, left + size, top + size };
    if (!clip_to_canvas(r, clip, &area)) return;
    GdiFlush();

    for (int y = area.top; y < area.bottom; y++) {
        uint32_t       *row = s_panel.canvas.px + (size_t)y * (size_t)s_panel.canvas.w;
        const uint32_t *src = px + (size_t)(y - top) * (size_t)size;
        for (int x = area.left; x < area.right; x++) {
            uint32_t s     = src[x - left];
            unsigned alpha = s >> 24;
            if (!alpha) continue;
            if (alpha == 255) { row[x] = s & 0xFFFFFF; continue; }

            uint32_t d   = row[x];
            unsigned inv = 255 - alpha;
            uint32_t r8 = (s >> 16 & 0xFF) + ((d >> 16 & 0xFF) * inv + 127) / 255;
            uint32_t g8 = (s >> 8  & 0xFF) + ((d >> 8  & 0xFF) * inv + 127) / 255;
            uint32_t b8 = (s       & 0xFF) + ((d       & 0xFF) * inv + 127) / 255;
            row[x] = (r8 > 255 ? 255 : r8) << 16 | (g8 > 255 ? 255 : g8) << 8 | (b8 > 255 ? 255 : b8);
        }
    }
}

static void draw_text(HFONT font, COLORREF color, const wchar_t *text, int len, RECT r, UINT flags) {
    HDC     dc  = s_panel.canvas.dc;
    HGDIOBJ old = SelectObject(dc, font);
    SetTextColor(dc, color);
    DrawTextW(dc, text, len, &r, flags | DT_NOPREFIX);
    SelectObject(dc, old);
}

static int text_width(HFONT font, const wchar_t *text) {
    HDC     dc   = s_panel.canvas.dc;
    HGDIOBJ old  = SelectObject(dc, font);
    SIZE    size = { 0, 0 };
    GetTextExtentPoint32W(dc, text, (int)wcslen(text), &size);
    SelectObject(dc, old);
    return size.cx;
}

static int line_height(HFONT font) {
    HDC     dc  = s_panel.canvas.dc;
    HGDIOBJ old = SelectObject(dc, font);
    TEXTMETRICW tm;
    GetTextMetricsW(dc, &tm);
    SelectObject(dc, old);
    return tm.tmHeight;
}

static HFONT make_font(const wchar_t *face, int px, int weight) {
    return CreateFontW(-scaled(px), 0, 0, 0, weight, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                       OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                       DEFAULT_PITCH | FF_SWISS, face);
}

static void fonts_create(void) {
    Fonts *f = &s_panel.fonts;
    f->header      = make_font(L"Segoe UI", FONT_HEADER_PX,   FW_SEMIBOLD);
    f->count       = make_font(L"Segoe UI", FONT_COUNT_PX,    FW_SEMIBOLD);
    f->search      = make_font(L"Segoe UI", FONT_SEARCH_PX,   FW_NORMAL);
    f->section     = make_font(L"Segoe UI", FONT_SECTION_PX,  FW_SEMIBOLD);
    f->app         = make_font(L"Segoe UI", FONT_APP_PX,      FW_NORMAL);
    f->title       = make_font(L"Segoe UI", FONT_TITLE_PX,    FW_SEMIBOLD);
    f->body        = make_font(L"Segoe UI", FONT_BODY_PX,     FW_NORMAL);
    f->hint        = make_font(L"Segoe UI", FONT_HINT_PX,     FW_NORMAL);
    f->monogram    = make_font(L"Segoe UI", FONT_MONOGRAM_PX, FW_BOLD);
    f->glyph       = make_font(s_glyph_face, FONT_GLYPH_PX,       FW_NORMAL);
    f->empty_glyph = make_font(s_glyph_face, FONT_EMPTY_GLYPH_PX, FW_NORMAL);

    s_panel.metrics.app    = line_height(f->app);
    s_panel.metrics.title  = line_height(f->title);
    s_panel.metrics.body   = line_height(f->body);
    s_panel.metrics.search = line_height(f->search);
}

static void fonts_free(void) {
    HFONT *all = (HFONT *)&s_panel.fonts;
    for (size_t i = 0; i < sizeof s_panel.fonts / sizeof(HFONT); i++)
        if (all[i]) DeleteObject(all[i]);
    memset(&s_panel.fonts, 0, sizeof s_panel.fonts);
}

static bool canvas_create(int w, int h) {
    BITMAPINFO bi;
    memset(&bi, 0, sizeof bi);
    bi.bmiHeader.biSize        = sizeof bi.bmiHeader;
    bi.bmiHeader.biWidth       = w;
    bi.bmiHeader.biHeight      = -h;
    bi.bmiHeader.biPlanes      = 1;
    bi.bmiHeader.biBitCount    = 32;
    bi.bmiHeader.biCompression = BI_RGB;

    Canvas *c = &s_panel.canvas;
    HDC screen = GetDC(NULL);
    c->dc     = CreateCompatibleDC(screen);
    c->bitmap = CreateDIBSection(screen, &bi, DIB_RGB_COLORS, (void **)&c->px, NULL, 0);
    ReleaseDC(NULL, screen);
    if (!c->dc || !c->bitmap) return false;

    c->previous = SelectObject(c->dc, c->bitmap);
    c->w = w;
    c->h = h;
    SetBkMode(c->dc, TRANSPARENT);
    return true;
}

static void canvas_free(void) {
    Canvas *c = &s_panel.canvas;
    if (c->dc) {
        if (c->previous) SelectObject(c->dc, c->previous);
        DeleteDC(c->dc);
    }
    if (c->bitmap) DeleteObject(c->bitmap);
    memset(c, 0, sizeof *c);
}

static void frame_layout(void) {
    int    w   = s_panel.canvas.w;
    int    h   = s_panel.canvas.h;
    int    pad = scaled(PANEL_PAD);
    Frame *f   = &s_panel.frame;

    SetRect(&f->header, pad, 0, w - pad, scaled(HEADER_HEIGHT));
    SetRect(&f->search, pad, f->header.bottom, w - pad, f->header.bottom + scaled(SEARCH_HEIGHT));
    SetRect(&f->clear, f->search.right - scaled(SEARCH_HEIGHT), f->search.top,
            f->search.right, f->search.bottom);
    SetRect(&f->footer, 0, h - scaled(FOOTER_HEIGHT), w, h);
    SetRect(&f->list, 0, f->search.bottom + scaled(LIST_GAP), w, f->footer.top);
}

static int view_height(void) {
    return s_panel.frame.list.bottom - s_panel.frame.list.top;
}

static const HistoryItem *item_of_card(int card) {
    return &s_panel.items[s_panel.rows[s_panel.cards[card]].item];
}

static void load_next_icon(void) {
    int size = scaled(ICON_PX);
    while (s_panel.icon_cursor < s_panel.count) {
        const HistoryItem *item = &s_panel.items[s_panel.icon_cursor++];
        if (!item->target.aumid[0] || icons_find(item->target.aumid, size)) continue;

        ToastTarget target = item->target;
        icons_load(&target, size);
        if (!s_panel.hwnd) return;

        InvalidateRect(s_panel.hwnd, NULL, FALSE);
        PostMessageW(s_panel.hwnd, WM_HISTORY_ICON, 0, 0);
        return;
    }
}

static bool contains(const wchar_t *text, const wchar_t *term, int len) {
    return text[0] &&
           FindNLSStringEx(LOCALE_NAME_USER_DEFAULT,
                           FIND_FROMSTART | LINGUISTIC_IGNORECASE | LINGUISTIC_IGNOREDIACRITIC |
                           NORM_IGNOREWIDTH | NORM_IGNOREKANATYPE,
                           text, -1, term, len, NULL, NULL, NULL, 0) >= 0;
}

static bool matches(const HistoryItem *item) {
    const HistChar *query = (const HistChar *)s_panel.query;
    int len = (int)wcslen(s_panel.query), pos = 0, start = 0, term = 0;
    while (hist_next_term(query, len, &pos, &start, &term)) {
        const wchar_t *t = s_panel.query + start;
        if (!contains(item->app, t, term) && !contains(item->title, t, term) &&
            !contains(item->text, t, term))
            return false;
    }
    return true;
}

static const wchar_t *heading_of(const HistoryItem *item) {
    return item->title[0] ? item->title : item->text;
}

static const wchar_t *body_of(const HistoryItem *item) {
    return item->title[0] ? item->text : L"";
}

static int text_column_width(void) {
    int card = s_panel.canvas.w - 2 * scaled(PANEL_PAD);
    return card - 2 * scaled(CARD_PAD_X) - scaled(ICON_PX) - scaled(ICON_GAP);
}

static void measure_body(const wchar_t *body, Row *row) {
    row->body_lines = 0;
    int len   = (int)wcslen(body);
    int start = hist_skip_spaces((const HistChar *)body, len, 0);
    if (start >= len) return;

    int span = len - start;
    if (span > MEASURE_CAP) span = MEASURE_CAP;

    int     extents[MEASURE_CAP];
    SIZE    size;
    HDC     dc  = s_panel.canvas.dc;
    HGDIOBJ old = SelectObject(dc, s_panel.fonts.body);
    GetTextExtentExPointW(dc, body + start, span, 0, NULL, extents, &size);
    SelectObject(dc, old);

    int first = hist_line_break((const HistChar *)body + start, span, extents, text_column_width());
    row->line1      = start;
    row->line1_len  = first;
    row->body_lines = 1;

    int next = hist_skip_spaces((const HistChar *)body, len, start + first);
    if (next < len) {
        row->line2      = next;
        row->body_lines = 2;
    }
}

static void measure_card(int item, Row *row) {
    memset(row, 0, sizeof *row);
    row->kind = ROW_CARD;
    row->item = item;
    measure_body(body_of(&s_panel.items[item]), row);

    const Metrics *m = &s_panel.metrics;
    int text = m->app + scaled(LINE_GAP) + m->title;
    if (row->body_lines) text += scaled(LINE_GAP) + row->body_lines * m->body;
    int icon = scaled(ICON_PX);
    row->height = 2 * scaled(CARD_PAD_Y) + (text > icon ? text : icon);
}

static void build_rows(void) {
    int       y   = 0;
    long long day = 0;
    s_panel.row_count = 0;
    s_panel.shown     = 0;

    for (int i = 0; i < s_panel.count; i++) {
        if (!matches(&s_panel.items[i])) continue;

        if (!s_panel.shown || s_panel.days[i] != day) {
            day = s_panel.days[i];
            Row *section = &s_panel.rows[s_panel.row_count++];
            memset(section, 0, sizeof *section);
            section->kind   = ROW_SECTION;
            section->item   = i;
            section->top    = y;
            section->height = scaled(SECTION_HEIGHT);
            y += section->height;
        }

        Row *card = &s_panel.rows[s_panel.row_count];
        measure_card(i, card);
        card->top = y;
        y += card->height + scaled(CARD_GAP);
        s_panel.cards[s_panel.shown++] = s_panel.row_count++;
    }

    s_panel.content = s_panel.shown ? y - scaled(CARD_GAP) + scaled(LIST_BOTTOM_PAD) : 0;
}

static void reveal(int card) {
    int index = s_panel.cards[card];
    const Row *row = &s_panel.rows[index];
    int top    = index > 0 && s_panel.rows[index - 1].kind == ROW_SECTION ? s_panel.rows[index - 1].top
                                                                           : row->top;
    int bottom = card == s_panel.shown - 1 ? s_panel.content : row->top + row->height;
    int view   = view_height();
    s_panel.scroll = hist_clamp_scroll(hist_reveal(s_panel.scroll, top, bottom, view),
                                       s_panel.content, view);
}

static void move_selection(int delta) {
    int next = hist_step(s_panel.selected, s_panel.shown, delta);
    if (next < 0) return;
    s_panel.selected = next;
    reveal(next);
    InvalidateRect(s_panel.hwnd, NULL, FALSE);
}

static int page_size(void) {
    int cards = view_height() / scaled(PAGE_CARD_HEIGHT);
    return cards > 1 ? cards : 1;
}

static void scroll_by(int delta) {
    int next = hist_clamp_scroll(s_panel.scroll + delta, s_panel.content, view_height());
    if (next == s_panel.scroll) return;
    s_panel.scroll = next;
    InvalidateRect(s_panel.hwnd, NULL, FALSE);
}

static int card_at(POINT pt) {
    const RECT *list = &s_panel.frame.list;
    int pad = scaled(PANEL_PAD);
    if (!PtInRect(list, pt) || pt.x < pad || pt.x >= s_panel.canvas.w - pad) return -1;

    int y = pt.y - list->top + s_panel.scroll;
    for (int c = 0; c < s_panel.shown; c++) {
        const Row *row = &s_panel.rows[s_panel.cards[c]];
        if (y >= row->top && y < row->top + row->height) return c;
    }
    return -1;
}

static bool over_clear(POINT pt) {
    return s_panel.query[0] && PtInRect(&s_panel.frame.clear, pt);
}

static void time_label(const HistoryItem *item, wchar_t *out, int cap) {
    long long minutes = (s_panel.now - item->arrival) / MINUTE_TICKS;
    out[0] = L'\0';
    if (minutes < 1) {
        mnotify_copy_w(out, (size_t)cap, L"Just now");
    } else if (minutes < 60) {
        _snwprintf(out, (size_t)cap, L"%lld min ago", minutes);
        out[cap - 1] = L'\0';
    } else {
        SYSTEMTIME local;
        if (!local_time(item->arrival, &local) ||
            !GetTimeFormatEx(LOCALE_NAME_USER_DEFAULT, TIME_NOSECONDS, &local, NULL, out, cap))
            out[0] = L'\0';
    }
}

static void section_label(const Row *row, wchar_t *out, int cap) {
    const HistoryItem *item = &s_panel.items[row->item];
    HistDay day = hist_day(s_panel.days[row->item], s_panel.today);
    if (day == HIST_DAY_TODAY)     { mnotify_copy_w(out, (size_t)cap, L"Today");     return; }
    if (day == HIST_DAY_YESTERDAY) { mnotify_copy_w(out, (size_t)cap, L"Yesterday"); return; }

    SYSTEMTIME local;
    const wchar_t *format = day == HIST_DAY_THIS_WEEK ? L"dddd" : L"dddd d MMMM";
    if (!local_time(item->arrival, &local) ||
        !GetDateFormatEx(LOCALE_NAME_USER_DEFAULT, 0, &local, format, out, cap, NULL))
        mnotify_copy_w(out, (size_t)cap, L"Earlier");
}

static COLORREF monogram_color(const HistoryItem *item) {
    const wchar_t *key  = item->target.aumid[0] ? item->target.aumid : item->app;
    uint32_t       hash = 2166136261u;
    for (; *key; key++) hash = (hash ^ (uint32_t)towlower(*key)) * 16777619u;
    return MONOGRAM_COLORS[hash % (sizeof MONOGRAM_COLORS / sizeof MONOGRAM_COLORS[0])];
}

static int monogram_letter(const wchar_t *app, wchar_t *out) {
    for (const wchar_t *c = app; *c; c++) {
        if (IS_HIGH_SURROGATE(c[0]) && IS_LOW_SURROGATE(c[1])) {
            out[0] = c[0];
            out[1] = c[1];
            return 2;
        }
        if (IsCharAlphaNumericW(*c)) {
            out[0] = *c;
            CharUpperBuffW(out, 1);
            return 1;
        }
    }
    out[0] = L'?';
    return 1;
}

static void paint_icon(const HistoryItem *item, int left, int top, RECT clip) {
    int            size = scaled(ICON_PX);
    RECT           tile = { left, top, left + size, top + size };
    const AppIcon *icon = item->target.aumid[0] ? icons_find(item->target.aumid, size) : NULL;

    if (icon && icon->pixels) {
        paint_pixels(icon->pixels, size, left, top, clip);
        return;
    }
    if (!icon && item->target.aumid[0]) {
        paint_shape(tile, scaled(ICON_RADIUS), 0, COLOR_RAISED, clip);
        return;
    }

    paint_shape(tile, scaled(ICON_RADIUS), 0, monogram_color(item), clip);
    wchar_t letter[2];
    int     len = monogram_letter(item->app, letter);
    draw_text(s_panel.fonts.monogram, COLOR_MONOGRAM_TEXT, letter, len, tile,
              DT_SINGLELINE | DT_CENTER | DT_VCENTER);
}

static void paint_header(void) {
    const RECT *r = &s_panel.frame.header;
    draw_text(s_panel.fonts.header, COLOR_FG, L"Notifications", -1, *r,
              DT_SINGLELINE | DT_VCENTER | DT_LEFT);
    if (s_panel.count <= 0) return;

    wchar_t label[LABEL_CAP];
    if (s_panel.filtering) _snwprintf(label, LABEL_CAP, L"%d of %d", s_panel.shown, s_panel.count);
    else                   _snwprintf(label, LABEL_CAP, L"%d", s_panel.count);
    label[LABEL_CAP - 1] = L'\0';

    int  w   = text_width(s_panel.fonts.count, label) + 2 * scaled(COUNT_PAD);
    int  h   = scaled(COUNT_HEIGHT);
    int  mid = (r->top + r->bottom) / 2;
    RECT pill = { r->right - w, mid - h / 2, r->right, mid - h / 2 + h };
    paint_shape(pill, h / 2, 0, COLOR_RAISED, *r);
    draw_text(s_panel.fonts.count, COLOR_DIM, label, -1, pill, DT_SINGLELINE | DT_VCENTER | DT_CENTER);
}

static void paint_search(void) {
    const Frame *f = &s_panel.frame;
    RECT all = { 0, 0, s_panel.canvas.w, s_panel.canvas.h };
    paint_shape(f->search, scaled(SEARCH_RADIUS), 0, COLOR_CARD, all);
    paint_shape(f->search, scaled(SEARCH_RADIUS), scaled(1),
                s_panel.filtering ? mix(COLOR_INFO, COLOR_CARD, 150) : COLOR_RAISED, all);

    RECT glyph = { f->search.left + scaled(SEARCH_INSET), f->search.top,
                   f->search.left + scaled(SEARCH_INSET + SEARCH_GLYPH), f->search.bottom };
    draw_text(s_panel.fonts.glyph, s_panel.filtering ? COLOR_INFO : COLOR_MUTED, GLYPH_SEARCH, -1,
              glyph, DT_SINGLELINE | DT_VCENTER | DT_CENTER);

    if (s_panel.query[0])
        draw_text(s_panel.fonts.glyph, s_panel.clear_hot ? COLOR_FG : COLOR_MUTED, GLYPH_CLEAR, -1,
                  f->clear, DT_SINGLELINE | DT_VCENTER | DT_CENTER);
}

static void paint_section(const Row *row, int top) {
    wchar_t label[LABEL_CAP];
    section_label(row, label, LABEL_CAP);
    int  pad = scaled(PANEL_PAD);
    RECT r   = { pad + scaled(SECTION_INSET), top, s_panel.canvas.w - pad,
                 top + row->height - scaled(SECTION_BASELINE) };
    draw_text(s_panel.fonts.section, COLOR_MUTED, label, -1, r,
              DT_SINGLELINE | DT_BOTTOM | DT_LEFT | DT_END_ELLIPSIS);
}

static void paint_card(const Row *row, int top, bool selected, RECT clip) {
    const HistoryItem *item = &s_panel.items[row->item];
    const Metrics     *m    = &s_panel.metrics;
    int  pad  = scaled(PANEL_PAD);
    RECT card = { pad, top, s_panel.canvas.w - pad, top + row->height };
    int  radius = scaled(CARD_RADIUS);

    paint_shape(card, radius, 0, selected ? COLOR_RAISED : COLOR_CARD, clip);
    if (selected) paint_shape(card, radius, scaled(1), mix(COLOR_INFO, COLOR_RAISED, 140), clip);

    int icon_left = card.left + scaled(CARD_PAD_X);
    int y         = card.top + scaled(CARD_PAD_Y);
    paint_icon(item, icon_left, y, clip);

    int left  = icon_left + scaled(ICON_PX) + scaled(ICON_GAP);
    int right = card.right - scaled(CARD_PAD_X);

    wchar_t when[LABEL_CAP];
    time_label(item, when, LABEL_CAP);
    int  when_w = when[0] ? text_width(s_panel.fonts.app, when) : 0;
    RECT when_r = { right - when_w, y, right, y + m->app };
    if (when[0]) draw_text(s_panel.fonts.app, COLOR_MUTED, when, -1, when_r, DT_SINGLELINE | DT_RIGHT);

    RECT app_r = { left, y, when[0] ? when_r.left - scaled(TIME_GAP) : right, y + m->app };
    draw_text(s_panel.fonts.app, COLOR_DIM, item->app, -1, app_r, DT_SINGLELINE | DT_END_ELLIPSIS);
    y += m->app + scaled(LINE_GAP);

    RECT heading = { left, y, right, y + m->title };
    draw_text(s_panel.fonts.title, COLOR_FG, heading_of(item), -1, heading,
              DT_SINGLELINE | DT_END_ELLIPSIS);
    y += m->title + scaled(LINE_GAP);

    const wchar_t *body = body_of(item);
    if (row->body_lines >= 1) {
        RECT line = { left, y, right, y + m->body };
        draw_text(s_panel.fonts.body, COLOR_DIM, body + row->line1, row->line1_len, line,
                  DT_SINGLELINE | DT_END_ELLIPSIS);
    }
    if (row->body_lines == 2) {
        RECT line = { left, y + m->body, right, y + 2 * m->body };
        draw_text(s_panel.fonts.body, COLOR_DIM, body + row->line2, -1, line,
                  DT_SINGLELINE | DT_END_ELLIPSIS);
    }
}

static void paint_empty(void) {
    const Frame *f = &s_panel.frame;
    const wchar_t *glyph, *title;
    wchar_t detail[QUERY_CAP + LABEL_CAP];

    if (s_panel.count < 0) {
        glyph = GLYPH_WARNING;
        title = L"History is unavailable";
        mnotify_copy_w(detail, QUERY_CAP + LABEL_CAP, L"Windows' notification store could not be read");
    } else if (!s_panel.count) {
        glyph = GLYPH_BELL;
        title = L"No notifications";
        mnotify_copy_w(detail, QUERY_CAP + LABEL_CAP, L"Windows keeps them for up to three days");
    } else {
        glyph = GLYPH_SEARCH;
        title = L"No matches";
        _snwprintf(detail, QUERY_CAP + LABEL_CAP, L"Nothing matches “%ls”", s_panel.query);
        detail[QUERY_CAP + LABEL_CAP - 1] = L'\0';
    }

    int glyph_h = line_height(s_panel.fonts.empty_glyph);
    int gap     = scaled(EMPTY_GAP);
    int block   = glyph_h + 2 * gap + s_panel.metrics.title + gap + s_panel.metrics.body;
    int y       = f->list.top + (view_height() - block) / 2 - scaled(EMPTY_LIFT);
    int pad     = 2 * scaled(PANEL_PAD);

    RECT g = { pad, y, s_panel.canvas.w - pad, y + glyph_h };
    draw_text(s_panel.fonts.empty_glyph, COLOR_FAINT, glyph, -1, g, DT_SINGLELINE | DT_CENTER);
    y += glyph_h + 2 * gap;

    RECT t = { pad, y, s_panel.canvas.w - pad, y + s_panel.metrics.title };
    draw_text(s_panel.fonts.title, COLOR_FG, title, -1, t, DT_SINGLELINE | DT_CENTER);
    y += s_panel.metrics.title + gap;

    RECT d = { pad, y, s_panel.canvas.w - pad, y + s_panel.metrics.body };
    draw_text(s_panel.fonts.body, COLOR_MUTED, detail, -1, d,
              DT_SINGLELINE | DT_CENTER | DT_END_ELLIPSIS);
}

static void paint_list(void) {
    const RECT *list = &s_panel.frame.list;
    if (!s_panel.shown) {
        paint_empty();
        return;
    }

    HDC dc = s_panel.canvas.dc;
    IntersectClipRect(dc, list->left, list->top, list->right, list->bottom);

    int card = 0;
    for (int i = 0; i < s_panel.row_count; i++) {
        const Row *row = &s_panel.rows[i];
        int top = list->top + row->top - s_panel.scroll;
        bool visible = top < list->bottom && top + row->height > list->top;

        if (row->kind == ROW_SECTION) {
            if (visible) paint_section(row, top);
            continue;
        }
        if (visible) paint_card(row, top, card == s_panel.selected, *list);
        card++;
    }
    SelectClipRgn(dc, NULL);

    int thumb_top, thumb_len;
    if (hist_thumb(s_panel.scroll, s_panel.content, view_height(), scaled(THUMB_MIN),
                   &thumb_top, &thumb_len)) {
        int  right = s_panel.canvas.w - scaled(THUMB_INSET);
        RECT thumb = { right - scaled(THUMB_WIDTH), list->top + thumb_top,
                       right, list->top + thumb_top + thumb_len };
        paint_shape(thumb, scaled(THUMB_WIDTH) / 2, 0, COLOR_BORDER, *list);
    }
}

static int paint_hint(int x, int mid, const wchar_t *key, const wchar_t *action) {
    RECT all = { 0, 0, s_panel.canvas.w, s_panel.canvas.h };
    int  h   = scaled(KEYCAP_HEIGHT);
    RECT cap = { x, mid - h / 2, x + text_width(s_panel.fonts.hint, key) + 2 * scaled(KEYCAP_PAD),
                 mid - h / 2 + h };
    paint_shape(cap, scaled(KEYCAP_RADIUS), 0, COLOR_RAISED, all);
    draw_text(s_panel.fonts.hint, COLOR_DIM, key, -1, cap, DT_SINGLELINE | DT_VCENTER | DT_CENTER);

    int  label_left = cap.right + scaled(HINT_GAP);
    RECT label = { label_left, cap.top, label_left + text_width(s_panel.fonts.hint, action), cap.bottom };
    draw_text(s_panel.fonts.hint, COLOR_MUTED, action, -1, label, DT_SINGLELINE | DT_VCENTER);
    return label.right + scaled(HINT_SPACING);
}

static void paint_footer(void) {
    const RECT *f = &s_panel.frame.footer;
    RECT line = { f->left, f->top, f->right, f->top + (scaled(1) > 0 ? scaled(1) : 1) };
    paint_rect(line, COLOR_RAISED);

    int mid = (f->top + f->bottom) / 2;
    int x   = scaled(PANEL_PAD) + scaled(SECTION_INSET);
    if (s_panel.shown) {
        x = paint_hint(x, mid, L"↑↓", L"Select");
        x = paint_hint(x, mid, L"Enter", L"Open");
    }
    paint_hint(x, mid, L"Esc", s_panel.query[0] ? L"Clear" : L"Close");
}

static void render(void) {
    RECT all = { 0, 0, s_panel.canvas.w, s_panel.canvas.h };
    paint_rect(all, COLOR_PANEL);
    paint_header();
    paint_search();
    paint_list();
    paint_footer();
    GdiFlush();
}

static void present(HDC dc) {
    render();
    BitBlt(dc, 0, 0, s_panel.canvas.w, s_panel.canvas.h, s_panel.canvas.dc, 0, 0, SRCCOPY);
}

static void paint_placeholder(HWND edit, HDC dc) {
    RECT r;
    GetClientRect(edit, &r);
    HGDIOBJ old = SelectObject(dc, s_panel.fonts.search);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, COLOR_FAINT);
    DrawTextW(dc, L"Search notifications", -1, &r,
              DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX | DT_END_ELLIPSIS);
    SelectObject(dc, old);
}

static void query_changed(void) {
    GetWindowTextW(s_panel.edit, s_panel.query, QUERY_CAP);
    int pos = 0, start = 0, term = 0;
    s_panel.filtering = hist_next_term((const HistChar *)s_panel.query, (int)wcslen(s_panel.query),
                                       &pos, &start, &term);
    build_rows();
    s_panel.selected = s_panel.shown ? 0 : -1;
    s_panel.scroll   = 0;
    InvalidateRect(s_panel.hwnd, NULL, FALSE);
    InvalidateRect(s_panel.edit, NULL, TRUE);
}

static void delete_word(HWND edit) {
    wchar_t text[QUERY_CAP];
    GetWindowTextW(edit, text, QUERY_CAP);

    DWORD from = 0, to = 0;
    SendMessageW(edit, EM_GETSEL, (WPARAM)&from, (LPARAM)&to);
    if (from == to) from = (DWORD)hist_word_start((const HistChar *)text, (int)to);
    SendMessageW(edit, EM_SETSEL, from, to);
    SendMessageW(edit, EM_REPLACESEL, TRUE, (LPARAM)L"");
}

static void close_panel(bool restore_focus) {
    if (!s_panel.hwnd || s_panel.closing) return;
    s_panel.closing = true;

    HWND previous = s_panel.previous;
    if (restore_focus && previous && previous != s_panel.hwnd && IsWindow(previous) &&
        GetForegroundWindow() == s_panel.hwnd)
        SetForegroundWindow(previous);

    log_msg(LOG_DEBUG, L"history: closing");
    DestroyWindow(s_panel.hwnd);
}

static void open_selected(void) {
    if (s_panel.selected < 0 || s_panel.selected >= s_panel.shown) return;

    const HistoryItem *item = item_of_card(s_panel.selected);
    ToastTarget target = item->target;
    log_msg(LOG_DEBUG, L"history: opening a notification from %ls", item->app);
    close_panel(false);
    toast_activate(&target);
}

static bool handle_key(UINT vk) {
    bool ctrl = GetKeyState(VK_CONTROL) < 0;
    switch (vk) {
    case VK_DOWN:  move_selection(1);             return true;
    case VK_UP:    move_selection(-1);            return true;
    case VK_NEXT:  move_selection(page_size());   return true;
    case VK_PRIOR: move_selection(-page_size());  return true;
    case VK_HOME:  if (!ctrl) return false; move_selection(-s_panel.shown); return true;
    case VK_END:   if (!ctrl) return false; move_selection(s_panel.shown);  return true;
    case VK_RETURN:
        open_selected();
        return true;
    case VK_ESCAPE:
        if (s_panel.query[0]) SetWindowTextW(s_panel.edit, L"");
        else                  close_panel(true);
        return true;
    }
    return false;
}

static LRESULT CALLBACK search_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_KEYDOWN:
        if (handle_key((UINT)wp)) return 0;
        break;

    case WM_CHAR:
        if (wp == VK_RETURN || wp == VK_ESCAPE || wp == VK_TAB || wp == L'\n') return 0;
        if (wp == 0x7F) {
            delete_word(hwnd);
            return 0;
        }
        break;

    case WM_MOUSEWHEEL:
        return SendMessageW(GetParent(hwnd), msg, wp, lp);

    case WM_PAINT: {
        LRESULT rc = CallWindowProcW(s_edit_proc, hwnd, msg, wp, lp);
        if (!GetWindowTextLengthW(hwnd)) {
            HDC dc = GetDC(hwnd);
            HideCaret(hwnd);
            paint_placeholder(hwnd, dc);
            ShowCaret(hwnd);
            ReleaseDC(hwnd, dc);
        }
        return rc;
    }

    case WM_PRINTCLIENT: {
        LRESULT rc = CallWindowProcW(s_edit_proc, hwnd, msg, wp, lp);
        if (!GetWindowTextLengthW(hwnd)) paint_placeholder(hwnd, (HDC)wp);
        return rc;
    }
    }
    return CallWindowProcW(s_edit_proc, hwnd, msg, wp, lp);
}

static void track_leave(HWND hwnd) {
    if (s_panel.tracking) return;
    TRACKMOUSEEVENT tme = { .cbSize = sizeof tme, .dwFlags = TME_LEAVE, .hwndTrack = hwnd };
    s_panel.tracking = TrackMouseEvent(&tme);
}

static void release_panel(void) {
    free(s_panel.items);
    free(s_panel.days);
    free(s_panel.rows);
    free(s_panel.cards);
    canvas_free();
    fonts_free();
    if (s_panel.search_brush) DeleteObject(s_panel.search_brush);
    memset(&s_panel, 0, sizeof s_panel);
}

static LRESULT CALLBACK panel_wndproc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (hwnd != s_panel.hwnd) return DefWindowProcW(hwnd, msg, wp, lp);

    switch (msg) {
    case WM_ERASEBKGND:
        return 1;

    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(hwnd, &ps);
        present(dc);
        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_PRINTCLIENT:
        present((HDC)wp);
        return 0;

    case WM_CTLCOLOREDIT:
        SetTextColor((HDC)wp, COLOR_FG);
        SetBkColor((HDC)wp, COLOR_CARD);
        return (LRESULT)s_panel.search_brush;

    case WM_COMMAND:
        if (LOWORD(wp) == SEARCH_ID && HIWORD(wp) == EN_CHANGE) query_changed();
        return 0;

    case WM_SETCURSOR: {
        if ((HWND)wp != hwnd || LOWORD(lp) != HTCLIENT) break;
        POINT pt;
        GetCursorPos(&pt);
        ScreenToClient(hwnd, &pt);
        bool hand = card_at(pt) >= 0 || over_clear(pt);
        SetCursor(LoadCursorW(NULL, hand ? IDC_HAND : IDC_ARROW));
        return TRUE;
    }

    case WM_MOUSEMOVE: {
        POINT pt = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
        track_leave(hwnd);

        bool hot = over_clear(pt);
        if (hot != s_panel.clear_hot) {
            s_panel.clear_hot = hot;
            InvalidateRect(hwnd, &s_panel.frame.search, FALSE);
        }
        if (pt.x == s_panel.mouse.x && pt.y == s_panel.mouse.y) return 0;
        s_panel.mouse = pt;

        int card = card_at(pt);
        if (card >= 0 && card != s_panel.selected) {
            s_panel.selected = card;
            InvalidateRect(hwnd, &s_panel.frame.list, FALSE);
        }
        return 0;
    }

    case WM_MOUSELEAVE:
        s_panel.tracking = false;
        if (s_panel.clear_hot) {
            s_panel.clear_hot = false;
            InvalidateRect(hwnd, &s_panel.frame.search, FALSE);
        }
        return 0;

    case WM_LBUTTONDOWN: {
        POINT pt = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
        SetFocus(s_panel.edit);
        if (over_clear(pt)) {
            SetWindowTextW(s_panel.edit, L"");
            return 0;
        }
        s_panel.pressed = card_at(pt);
        return 0;
    }

    case WM_LBUTTONUP: {
        POINT pt   = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
        int   card = card_at(pt);
        if (card >= 0 && card == s_panel.pressed) {
            s_panel.selected = card;
            open_selected();
            return 0;
        }
        s_panel.pressed = -1;
        return 0;
    }

    case WM_MOUSEWHEEL:
        scroll_by(-GET_WHEEL_DELTA_WPARAM(wp) * scaled(WHEEL_STEP) / WHEEL_DELTA);
        return 0;

    case WM_KEYDOWN:
        if (handle_key((UINT)wp)) return 0;
        break;

    case WM_ACTIVATE:
        if (LOWORD(wp) == WA_INACTIVE) {
            if (!s_panel.closing) PostMessageW(hwnd, WM_CLOSE, 0, 0);
        } else if (s_panel.edit) {
            SetFocus(s_panel.edit);
        }
        return 0;

    case WM_CLOSE:
        close_panel(false);
        return 0;

    case WM_HISTORY_ICON:
        load_next_icon();
        return 0;

    case WM_DPICHANGED:
        return 0;

    case WM_NCDESTROY:
        release_panel();
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

static bool load_items(void) {
    s_panel.items = (HistoryItem *)calloc(MNOTIFY_HISTORY_MAX, sizeof *s_panel.items);
    s_panel.days  = (long long *)calloc(MNOTIFY_HISTORY_MAX, sizeof *s_panel.days);
    s_panel.rows  = (Row *)calloc(2 * MNOTIFY_HISTORY_MAX, sizeof *s_panel.rows);
    s_panel.cards = (int *)calloc(MNOTIFY_HISTORY_MAX, sizeof *s_panel.cards);
    if (!s_panel.items || !s_panel.days || !s_panel.rows || !s_panel.cards) return false;

    FILETIME   now;
    SYSTEMTIME today;
    GetSystemTimeAsFileTime(&now);
    GetLocalTime(&today);
    s_panel.now   = filetime_value(now);
    s_panel.today = day_number(today);

    s_panel.count = toasts_history(s_panel.items, MNOTIFY_HISTORY_MAX);
    for (int i = 0; i < s_panel.count; i++) {
        HistoryItem *item = &s_panel.items[i];
        flatten(item->app);
        flatten(item->title);
        flatten(item->text);
        s_panel.days[i] = arrival_day(item->arrival);
    }
    return true;
}

static bool create_search(void) {
    s_panel.search_brush = CreateSolidBrush(COLOR_CARD);
    s_panel.edit = CreateWindowExW(0, L"EDIT", NULL, WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
                                   0, 0, 0, 0, s_panel.hwnd, (HMENU)(INT_PTR)SEARCH_ID,
                                   mn.hinst, NULL);
    if (!s_panel.edit) return false;

    SendMessageW(s_panel.edit, WM_SETFONT, (WPARAM)s_panel.fonts.search, FALSE);
    SendMessageW(s_panel.edit, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, 0);
    SendMessageW(s_panel.edit, EM_SETLIMITTEXT, QUERY_CAP - 1, 0);
    s_edit_proc = (WNDPROC)SetWindowLongPtrW(s_panel.edit, GWLP_WNDPROC, (LONG_PTR)search_proc);

    const RECT *box  = &s_panel.frame.search;
    int         left = box->left + scaled(SEARCH_INSET + SEARCH_GLYPH + SEARCH_GLYPH_GAP);
    int         h    = s_panel.metrics.search;
    MoveWindow(s_panel.edit, left, box->top + (box->bottom - box->top - h) / 2,
               s_panel.frame.clear.left - left, h, FALSE);
    return true;
}

static void round_corners(HWND hwnd) {
    DWORD corner = DWM_CORNER_ROUND;
    DwmSetWindowAttribute(hwnd, DWM_CORNER_PREFERENCE, &corner, sizeof corner);
    COLORREF border = COLOR_BORDER;
    DwmSetWindowAttribute(hwnd, DWM_BORDER_COLOR, &border, sizeof border);
}

static void build_panel(void) {
    POINT cursor = { 0, 0 };
    GetCursorPos(&cursor);
    HMONITOR    monitor = MonitorFromPoint(cursor, MONITOR_DEFAULTTOPRIMARY);
    MONITORINFO mi      = { .cbSize = sizeof mi };
    if (!GetMonitorInfoW(monitor, &mi)) return;

    UINT dpi_x = 96, dpi_y = 96;
    if (FAILED(GetDpiForMonitor(monitor, MDT_EFFECTIVE_DPI, &dpi_x, &dpi_y)) || !dpi_x) dpi_x = 96;

    memset(&s_panel, 0, sizeof s_panel);
    s_panel.dpi      = dpi_x;
    s_panel.selected = -1;
    s_panel.pressed  = -1;
    if (!load_items()) {
        log_msg(LOG_WARN, L"history: out of memory");
        release_panel();
        return;
    }

    RECT work   = mi.rcWork;
    int  margin = scaled(PANEL_MARGIN);
    int  w      = scaled(PANEL_WIDTH);
    int  h      = scaled(PANEL_MAX_HEIGHT);
    if (h > work.bottom - work.top - 2 * margin) h = work.bottom - work.top - 2 * margin;
    bool top    = mn.corner == CORNER_TOP_RIGHT || mn.corner == CORNER_TOP_LEFT;
    bool right  = mn.corner == CORNER_TOP_RIGHT || mn.corner == CORNER_BOTTOM_RIGHT;
    int  x      = right ? work.right - margin - w : work.left + margin;
    int  y      = top ? work.top + margin : work.bottom - margin - h;

    s_panel.previous = GetForegroundWindow();
    HWND hwnd = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST, HISTORY_CLASS, L"Notifications",
                                WS_POPUP | WS_CLIPCHILDREN, x, y, w, h, NULL, NULL, mn.hinst, NULL);
    if (!hwnd) {
        log_err(L"history: CreateWindowEx failed (%lu)", GetLastError());
        release_panel();
        return;
    }
    s_panel.hwnd = hwnd;

    if (!canvas_create(w, h)) {
        log_err(L"history: could not create the drawing surface");
        DestroyWindow(hwnd);
        return;
    }
    fonts_create();
    frame_layout();
    build_rows();
    if (s_panel.shown) s_panel.selected = 0;
    if (!create_search()) {
        log_err(L"history: could not create the search box (%lu)", GetLastError());
        DestroyWindow(hwnd);
        return;
    }

    if (ScreenToClient(hwnd, &cursor)) s_panel.mouse = cursor;

    round_corners(hwnd);
    ShowWindow(hwnd, SW_SHOW);
    if (!SetForegroundWindow(hwnd))
        log_msg(LOG_WARN, L"history: could not take the foreground; Esc may not reach it");
    SetFocus(s_panel.edit);
    PostMessageW(hwnd, WM_HISTORY_ICON, 0, 0);
    log_msg(LOG_DEBUG, L"history: opened with %d notification(s)", s_panel.count);
}

static void open_panel(void) {
    if (s_opening) return;
    s_opening = true;
    build_panel();
    s_opening = false;
}

void history_show(void) {
    if (!s_panel.hwnd) {
        open_panel();
        return;
    }
    SetForegroundWindow(s_panel.hwnd);
    SetFocus(s_panel.edit);
}

void history_toggle(void) {
    if (s_panel.hwnd) close_panel(true);
    else              open_panel();
}

void history_close(void) {
    close_panel(true);
}

static int CALLBACK font_found(const LOGFONTW *lf, const TEXTMETRICW *tm, DWORD type, LPARAM found) {
    *(bool *)found = true;
    return 0;
}

static void pick_glyph_face(void) {
    LOGFONTW lf;
    memset(&lf, 0, sizeof lf);
    lf.lfCharSet = DEFAULT_CHARSET;
    mnotify_copy_w(lf.lfFaceName, LF_FACESIZE, L"Segoe Fluent Icons");

    bool found = false;
    HDC  dc    = GetDC(NULL);
    EnumFontFamiliesExW(dc, &lf, font_found, (LPARAM)&found, 0);
    ReleaseDC(NULL, dc);
    if (found) s_glyph_face = L"Segoe Fluent Icons";
}

bool history_init(void) {
    WNDCLASSEXW wc = {
        .cbSize        = sizeof wc,
        .lpfnWndProc   = panel_wndproc,
        .hInstance     = mn.hinst,
        .hCursor       = LoadCursorW(NULL, IDC_ARROW),
        .lpszClassName = HISTORY_CLASS,
    };
    if (!RegisterClassExW(&wc)) {
        log_err(L"history: RegisterClassEx failed (%lu)", GetLastError());
        return false;
    }
    s_class_registered = true;
    pick_glyph_face();
    return true;
}

void history_shutdown(void) {
    if (s_panel.hwnd) {
        s_panel.closing = true;
        DestroyWindow(s_panel.hwnd);
    }
    icons_shutdown();
    if (s_class_registered) {
        UnregisterClassW(HISTORY_CLASS, mn.hinst);
        s_class_registered = false;
    }
}
