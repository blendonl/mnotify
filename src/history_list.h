#ifndef MNOTIFY_HISTORY_LIST_H
#define MNOTIFY_HISTORY_LIST_H

#include <stdbool.h>
#include <stdint.h>

typedef uint16_t HistChar;

typedef struct {
    int left;
    int top;
    int right;
    int bottom;
} HistRect;

typedef enum {
    HIST_DAY_TODAY = 0,
    HIST_DAY_YESTERDAY,
    HIST_DAY_THIS_WEEK,
    HIST_DAY_OLDER,
} HistDay;

int     hist_clamp_scroll(int scroll, int content, int view);
int     hist_reveal(int scroll, int top, int bottom, int view);
int     hist_step(int selected, int count, int delta);
bool    hist_thumb(int scroll, int content, int view, int min_len, int *top, int *len);
HistDay hist_day(long long day, long long today);

uint8_t hist_round_coverage(int x, int y, HistRect r, int radius);
uint8_t hist_ring_coverage(int x, int y, HistRect r, int radius, int width);

int     hist_line_break(const HistChar *text, int len, const int *extents, int width);
int     hist_skip_spaces(const HistChar *text, int len, int at);
bool    hist_next_term(const HistChar *query, int len, int *pos, int *start, int *term_len);
int     hist_word_start(const HistChar *text, int caret);

#endif
