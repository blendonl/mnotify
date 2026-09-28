#include "tests.h"
#include "history_list.h"

#define TEXT_CAP 64

static HistChar text[TEXT_CAP];
static int      text_len;

static void set_text(const char *s) {
    text_len = 0;
    while (s[text_len] && text_len < TEXT_CAP - 1) {
        text[text_len] = (HistChar)(unsigned char)s[text_len];
        text_len++;
    }
    text[text_len] = 0;
}

static void monospace_extents(int len, int advance, int *extents) {
    for (int i = 0; i < len; i++) extents[i] = (i + 1) * advance;
}

static void test_clamp_scroll(void) {
    CHECK(hist_clamp_scroll(50, 100, 200) == 0, "content shorter than the view never scrolls");
    CHECK(hist_clamp_scroll(-10, 500, 200) == 0, "no scrolling above the top");
    CHECK(hist_clamp_scroll(400, 500, 200) == 300, "no scrolling past the bottom");
    CHECK(hist_clamp_scroll(120, 500, 200) == 120, "in range is kept");
}

static void test_reveal(void) {
    CHECK(hist_reveal(100, 40, 80, 200) == 40, "a row above the view scrolls up to its top");
    CHECK(hist_reveal(100, 280, 340, 200) == 140, "a row below the view scrolls until its bottom shows");
    CHECK(hist_reveal(100, 150, 250, 200) == 100, "a visible row does not scroll");
    CHECK(hist_reveal(0, 100, 400, 200) == 100, "a row taller than the view shows its top");
}

static void test_step(void) {
    CHECK(hist_step(3, 0, 1) == -1, "nothing to select in an empty list");
    CHECK(hist_step(-1, 5, 1) == 0, "the first move selects the first row");
    CHECK(hist_step(-1, 5, -1) == 0, "even upwards");
    CHECK(hist_step(2, 5, 1) == 3, "down one");
    CHECK(hist_step(4, 5, 1) == 4, "stops at the last row");
    CHECK(hist_step(1, 5, -4) == 0, "stops at the first row");
    CHECK(hist_step(1, 5, 10) == 4, "a page past the end lands on the last row");
}

static void test_thumb(void) {
    int top = -1, len = -1;
    CHECK(!hist_thumb(0, 200, 200, 20, &top, &len), "no thumb when everything fits");
    CHECK(!hist_thumb(0, 500, 0, 20, &top, &len), "no thumb without a view");

    CHECK(hist_thumb(0, 400, 200, 20, &top, &len) && top == 0 && len == 100,
          "half the content shows: half-length thumb at the top (top %d len %d)", top, len);
    CHECK(hist_thumb(200, 400, 200, 20, &top, &len) && top == 100 && top + len == 200,
          "fully scrolled: thumb touches the bottom (top %d len %d)", top, len);
    CHECK(hist_thumb(0, 100000, 200, 24, &top, &len) && len == 24,
          "long content keeps the thumb grabbable (len %d)", len);
    CHECK(hist_thumb(99800, 100000, 200, 24, &top, &len) && top + len == 200,
          "and it still reaches the bottom (top %d len %d)", top, len);
    CHECK(hist_thumb(9999, 400, 200, 20, &top, &len) && top + len == 200,
          "overscroll is clamped (top %d len %d)", top, len);
}

static void test_day(void) {
    CHECK(hist_day(1000, 1000) == HIST_DAY_TODAY, "same day");
    CHECK(hist_day(1001, 1000) == HIST_DAY_TODAY, "a clock ahead of ours still reads today");
    CHECK(hist_day(999, 1000) == HIST_DAY_YESTERDAY, "one day back");
    CHECK(hist_day(998, 1000) == HIST_DAY_THIS_WEEK, "two days back");
    CHECK(hist_day(994, 1000) == HIST_DAY_THIS_WEEK, "six days back");
    CHECK(hist_day(993, 1000) == HIST_DAY_OLDER, "a week back");
}

static void test_round_coverage(void) {
    HistRect r = { 10, 20, 110, 60 };

    CHECK(hist_round_coverage(60, 40, r, 8) == 255, "centre is solid");
    CHECK(hist_round_coverage(9, 40, r, 8) == 0, "left of the rect is empty");
    CHECK(hist_round_coverage(110, 40, r, 8) == 0, "right edge is exclusive");
    CHECK(hist_round_coverage(60, 60, r, 8) == 0, "bottom edge is exclusive");
    CHECK(hist_round_coverage(10, 40, r, 8) == 255, "straight left edge is solid");
    CHECK(hist_round_coverage(60, 20, r, 8) == 255, "straight top edge is solid");
    CHECK(hist_round_coverage(10, 20, r, 8) == 0, "the very corner is cut away");
    CHECK(hist_round_coverage(10, 20, r, 0) == 255, "radius 0 is square");

    uint8_t partial = hist_round_coverage(11, 23, r, 8);
    CHECK(partial > 0 && partial < 255, "the curve is antialiased (%u)", partial);

    CHECK(hist_round_coverage(10, 20, r, 8) == hist_round_coverage(109, 20, r, 8) &&
          hist_round_coverage(12, 21, r, 8) == hist_round_coverage(107, 21, r, 8) &&
          hist_round_coverage(12, 21, r, 8) == hist_round_coverage(12, 58, r, 8) &&
          hist_round_coverage(12, 21, r, 8) == hist_round_coverage(107, 58, r, 8),
          "all four corners match");

    uint8_t previous = 0;
    bool    rising   = true;
    for (int i = 0; i < 8; i++) {
        uint8_t c = hist_round_coverage(10 + i, 20 + i, r, 8);
        if (c < previous) rising = false;
        previous = c;
    }
    CHECK(rising && previous == 255, "coverage grows along the diagonal into the rect");

    HistRect pill = { 0, 0, 40, 10 };
    CHECK(hist_round_coverage(0, 0, pill, 99) == 0 && hist_round_coverage(20, 5, pill, 99) == 255,
          "a radius larger than the rect makes a pill");
}

static void test_ring_coverage(void) {
    HistRect r = { 0, 0, 100, 40 };

    CHECK(hist_ring_coverage(50, 0, r, 8, 1) == 255, "the top edge is on the ring");
    CHECK(hist_ring_coverage(0, 20, r, 8, 1) == 255, "the left edge is on the ring");
    CHECK(hist_ring_coverage(50, 1, r, 8, 1) == 0, "just inside a 1px ring is empty");
    CHECK(hist_ring_coverage(50, 1, r, 8, 2) == 255, "a 2px ring covers it");
    CHECK(hist_ring_coverage(50, 20, r, 8, 1) == 0, "the middle is hollow");
    CHECK(hist_ring_coverage(0, 0, r, 8, 1) == 0, "the cut-away corner stays empty");

    uint8_t curve = hist_ring_coverage(2, 2, r, 8, 1);
    CHECK(curve > 0, "the ring follows the curve (%u)", curve);

    HistRect thin = { 0, 0, 2, 2 };
    CHECK(hist_ring_coverage(1, 1, thin, 0, 1) == 255, "a ring thicker than the hole is solid");
}

static void test_line_break(void) {
    int extents[TEXT_CAP];

    set_text("are we still on for tonight");
    monospace_extents(text_len, 10, extents);
    CHECK(hist_line_break(text, text_len, extents, 1000) == text_len, "short text fits on one line");
    CHECK(hist_line_break(text, text_len, extents, 130) == 12,
          "breaks at the last space that fits ('are we still')");
    CHECK(hist_line_break(text, text_len, extents, 120) == 12,
          "a space exactly at the edge breaks there");
    CHECK(hist_skip_spaces(text, text_len, 12) == 13, "the next line starts after the space");

    set_text("https://example.com/a/very/long/path");
    monospace_extents(text_len, 10, extents);
    CHECK(hist_line_break(text, text_len, extents, 95) == 9, "no space: hard break where it stops fitting");
    CHECK(hist_line_break(text, text_len, extents, 5) == 1, "always makes progress");
    CHECK(hist_line_break(text, 0, extents, 100) == 0, "empty text has no line");

    HistChar pair[5] = { 'a', 0xD83D, 0xDE00, 'b', 0 };
    int pair_extents[4] = { 10, 15, 20, 30 };
    CHECK(hist_line_break(pair, 4, pair_extents, 16) == 1, "does not split a surrogate pair");
    CHECK(hist_line_break(pair + 1, 2, pair_extents + 1, 5) == 2,
          "a lone wide surrogate pair still moves forward whole");
}

static void test_terms(void) {
    int pos = 0, start = 0, len = 0;

    set_text("  discord   alice ");
    CHECK(hist_next_term(text, text_len, &pos, &start, &len) && start == 2 && len == 7, "first term");
    CHECK(hist_next_term(text, text_len, &pos, &start, &len) && start == 12 && len == 5, "second term");
    CHECK(!hist_next_term(text, text_len, &pos, &start, &len), "trailing blanks end it");

    pos = 0;
    set_text("   ");
    CHECK(!hist_next_term(text, text_len, &pos, &start, &len), "a blank query has no terms");

    pos = 0;
    HistChar nbsp[4] = { 'a', 0x00A0, 'b', 0 };
    CHECK(hist_next_term(nbsp, 3, &pos, &start, &len) && len == 1, "no-break space separates terms");
}

static void test_word_start(void) {
    set_text("from alice  ");
    CHECK(hist_word_start(text, text_len) == 5, "skips trailing blanks, then the word");
    CHECK(hist_word_start(text, 10) == 5, "from the end of a word");
    CHECK(hist_word_start(text, 4) == 0, "back to the start");
    CHECK(hist_word_start(text, 0) == 0, "nothing before the start");
}

int main(void) {
    test_clamp_scroll();
    test_reveal();
    test_step();
    test_thumb();
    test_day();
    test_round_coverage();
    test_ring_coverage();
    test_line_break();
    test_terms();
    test_word_start();
    return tests_report("history_list");
}
