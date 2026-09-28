#include "history_list.h"

#include <math.h>

static bool is_space(HistChar c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == 0x00A0 || c == 0x3000;
}

static bool is_low_surrogate(HistChar c) {
    return c >= 0xDC00 && c <= 0xDFFF;
}

static double clamp_between(double v, double lo, double hi) {
    return v < lo ? lo : v > hi ? hi : v;
}

int hist_clamp_scroll(int scroll, int content, int view) {
    int max = content - view;
    if (max < 0) max = 0;
    if (scroll > max) scroll = max;
    return scroll < 0 ? 0 : scroll;
}

int hist_reveal(int scroll, int top, int bottom, int view) {
    if (top < scroll) return top;
    if (bottom > scroll + view) {
        int shown = bottom - view;
        return shown > top ? top : shown;
    }
    return scroll;
}

int hist_step(int selected, int count, int delta) {
    if (count <= 0) return -1;
    if (selected < 0) return 0;

    int next = selected + delta;
    if (next < 0) return 0;
    return next >= count ? count - 1 : next;
}

bool hist_thumb(int scroll, int content, int view, int min_len, int *top, int *len) {
    if (view <= 0 || content <= view) return false;

    int length = (int)((long long)view * view / content);
    if (length < min_len) length = min_len;
    if (length > view)    length = view;

    int travel     = view - length;
    int max_scroll = content - view;
    int clamped    = hist_clamp_scroll(scroll, content, view);

    *len = length;
    *top = (int)((long long)clamped * travel / max_scroll);
    return true;
}

HistDay hist_day(long long day, long long today) {
    long long ago = today - day;
    if (ago <= 0) return HIST_DAY_TODAY;
    if (ago == 1) return HIST_DAY_YESTERDAY;
    return ago < 7 ? HIST_DAY_THIS_WEEK : HIST_DAY_OLDER;
}

uint8_t hist_round_coverage(int x, int y, HistRect r, int radius) {
    if (x < r.left || x >= r.right || y < r.top || y >= r.bottom) return 0;

    int w = r.right - r.left;
    int h = r.bottom - r.top;
    int limit = (w < h ? w : h) / 2;
    if (radius > limit) radius = limit;
    if (radius <= 0) return 255;

    double px = x + 0.5;
    double py = y + 0.5;
    double dx = px - clamp_between(px, r.left + radius, r.right - radius);
    double dy = py - clamp_between(py, r.top + radius, r.bottom - radius);
    if (dx == 0.0 || dy == 0.0) return 255;

    double inside = radius + 0.5 - sqrt(dx * dx + dy * dy);
    if (inside <= 0.0) return 0;
    if (inside >= 1.0) return 255;
    return (uint8_t)(inside * 255.0 + 0.5);
}

uint8_t hist_ring_coverage(int x, int y, HistRect r, int radius, int width) {
    uint8_t  outer = hist_round_coverage(x, y, r, radius);
    HistRect hole  = { r.left + width, r.top + width, r.right - width, r.bottom - width };
    if (!outer || hole.left >= hole.right || hole.top >= hole.bottom) return outer;

    int     hole_radius = radius > width ? radius - width : 0;
    uint8_t inner       = hist_round_coverage(x, y, hole, hole_radius);
    return outer > inner ? (uint8_t)(outer - inner) : 0;
}

int hist_line_break(const HistChar *text, int len, const int *extents, int width) {
    if (len <= 0) return 0;

    int fit = 0;
    while (fit < len && extents[fit] <= width) fit++;
    if (fit >= len) return len;

    for (int i = fit; i > 0; i--)
        if (is_space(text[i])) return i;

    if (fit == 0) fit = 1;
    if (fit < len && is_low_surrogate(text[fit])) fit = fit > 1 ? fit - 1 : fit + 1;
    return fit;
}

int hist_skip_spaces(const HistChar *text, int len, int at) {
    while (at < len && is_space(text[at])) at++;
    return at;
}

bool hist_next_term(const HistChar *query, int len, int *pos, int *start, int *term_len) {
    int at = hist_skip_spaces(query, len, *pos);
    if (at >= len) {
        *pos = len;
        return false;
    }

    *start = at;
    while (at < len && !is_space(query[at])) at++;
    *term_len = at - *start;
    *pos      = at;
    return true;
}

int hist_word_start(const HistChar *text, int caret) {
    int at = caret;
    while (at > 0 && is_space(text[at - 1])) at--;
    while (at > 0 && !is_space(text[at - 1])) at--;
    return at;
}
