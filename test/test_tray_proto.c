#include "tests.h"
#include "tray_proto.h"

#include <stdlib.h>

#define SHELL32_COPYDATA_BYTES 1484

static void put_u32(uint8_t *buf, size_t off, uint32_t v) {
    buf[off]     = (uint8_t)v;
    buf[off + 1] = (uint8_t)(v >> 8);
    buf[off + 2] = (uint8_t)(v >> 16);
    buf[off + 3] = (uint8_t)(v >> 24);
}

static void put_text(uint8_t *buf, size_t off, const char *s) {
    for (size_t i = 0; s[i]; i++) {
        buf[off + i * 2]     = (uint8_t)s[i];
        buf[off + i * 2 + 1] = 0;
    }
}

static bool text_is(const TrayChar *have, const char *want) {
    size_t i = 0;
    for (; want[i]; i++)
        if (have[i] != (TrayChar)(unsigned char)want[i]) return false;
    return have[i] == 0;
}

typedef struct {
    uint8_t bytes[SHELL32_COPYDATA_BYTES];
} Payload;

static void payload_init(Payload *p, uint32_t nim, uint32_t hwnd, uint32_t uid,
                         uint32_t flags) {
    memset(p, 0, sizeof *p);
    put_u32(p->bytes, 0, TRAY_SIGNATURE);
    put_u32(p->bytes, 4, nim);
    put_u32(p->bytes, 8, TRAY_NID_BYTES);
    put_u32(p->bytes, 12, hwnd);
    put_u32(p->bytes, 16, uid);
    put_u32(p->bytes, 20, flags);
}

static TrayMessage parsed(const Payload *p) {
    TrayMessage m;
    tray_parse(p->bytes, sizeof p->bytes, &m);
    return m;
}

static void test_parse_full_layout(void) {
    Payload p;
    payload_init(&p, TRAY_NIM_MODIFY, 0x1234, 7, TRAY_NIF_INFO | TRAY_NIF_TIP);
    put_u32(p.bytes, 24, 0x8001);
    put_u32(p.bytes, 28, 0xABCD);
    put_text(p.bytes, 32, "Steam");
    put_u32(p.bytes, 288, 1);
    put_u32(p.bytes, 292, 3);
    put_text(p.bytes, 296, "balloon probe body text");
    put_u32(p.bytes, 808, TRAY_VERSION_4);
    put_text(p.bytes, 812, "Balloon probe title");
    put_u32(p.bytes, 940, TRAY_NIIF_WARNING);
    p.bytes[944] = 0xAA;
    p.bytes[959] = 0xBB;
    put_u32(p.bytes, 960, 0x55);

    TrayMessage m;
    CHECK(tray_parse(p.bytes, sizeof p.bytes, &m), "full payload parses");
    CHECK(m.message == TRAY_NIM_MODIFY, "message %u", m.message);
    CHECK(m.hwnd == 0x1234 && m.uid == 7, "hwnd %x uid %u", m.hwnd, m.uid);
    CHECK(m.flags == (TRAY_NIF_INFO | TRAY_NIF_TIP), "flags %x", m.flags);
    CHECK(m.callback == 0x8001 && m.hicon == 0xABCD, "callback/icon");
    CHECK(text_is(m.tip, "Steam"), "tip");
    CHECK(m.state == 1 && m.state_mask == 3, "state %u mask %u", m.state, m.state_mask);
    CHECK(text_is(m.info, "balloon probe body text"), "info");
    CHECK(m.timeout_or_version == TRAY_VERSION_4, "version %u", m.timeout_or_version);
    CHECK(text_is(m.info_title, "Balloon probe title"), "info title");
    CHECK(m.info_flags == TRAY_NIIF_WARNING, "info flags %u", m.info_flags);
    CHECK(m.guid[0] == 0xAA && m.guid[15] == 0xBB, "guid bytes");
    CHECK(m.balloon_icon == 0x55, "balloon icon %x", m.balloon_icon);
}

static void test_parse_rejects_garbage(void) {
    Payload p;
    TrayMessage m;

    payload_init(&p, TRAY_NIM_ADD, 1, 1, 0);
    put_u32(p.bytes, 0, 0xDEADBEEF);
    CHECK(!tray_parse(p.bytes, sizeof p.bytes, &m), "wrong signature rejected");

    payload_init(&p, TRAY_NIM_ADD, 1, 1, 0);
    CHECK(!tray_parse(p.bytes, 11, &m), "truncated header rejected");
    CHECK(!tray_parse(p.bytes, TRAY_HEADER_BYTES + TRAY_NID_V1_BYTES - 1, &m),
          "shorter than the smallest NOTIFYICONDATA rejected");
    CHECK(!tray_parse(NULL, 0, &m), "null rejected");

    payload_init(&p, TRAY_NIM_ADD, 1, 1, 0);
    put_u32(p.bytes, 8, 40);
    CHECK(!tray_parse(p.bytes, sizeof p.bytes, &m), "tiny cbSize rejected");
}

static void test_parse_v1_ignores_later_fields(void) {
    Payload p;
    payload_init(&p, TRAY_NIM_ADD, 9, 2, TRAY_NIF_TIP | TRAY_NIF_INFO);
    put_u32(p.bytes, 8, TRAY_NID_V1_BYTES);
    put_text(p.bytes, 32, "old app");
    put_text(p.bytes, 296, "stale bytes past cbSize");

    TrayMessage m;
    CHECK(tray_parse(p.bytes, sizeof p.bytes, &m), "v1 payload parses");
    CHECK(text_is(m.tip, "old app"), "v1 tip");
    CHECK(m.info[0] == 0, "fields past cbSize are not read");
}

static void test_parse_cbdata_bounds_cbsize(void) {
    Payload p;
    payload_init(&p, TRAY_NIM_ADD, 9, 2, TRAY_NIF_INFO);
    put_text(p.bytes, 296, "cut");

    TrayMessage m;
    CHECK(tray_parse(p.bytes, TRAY_HEADER_BYTES + TRAY_NID_V2_BYTES - 2, &m),
          "a payload shorter than its cbSize still parses what it has");
    CHECK(m.info[0] == 0, "info is not read past the end of the payload");
}

static void test_parse_terminates_full_strings(void) {
    Payload p;
    payload_init(&p, TRAY_NIM_ADD, 1, 1, TRAY_NIF_TIP);
    for (int i = 0; i < TRAY_TIP_CAP; i++) {
        p.bytes[32 + i * 2] = 'x';
        p.bytes[32 + i * 2 + 1] = 0;
    }
    TrayMessage m = parsed(&p);
    CHECK(m.tip[TRAY_TIP_CAP - 1] == 0, "unterminated tip is terminated");
    CHECK(m.tip[TRAY_TIP_CAP - 2] == 'x', "tip keeps cap-1 chars");
}

static void test_add_modify_delete(void) {
    TrayTable *t = calloc(1, sizeof *t);
    TrayEffect e;
    Payload p;

    payload_init(&p, TRAY_NIM_MODIFY, 5, 1, TRAY_NIF_TIP);
    TrayMessage m = parsed(&p);
    CHECK(!tray_apply(t, &m, &e), "modify of an unknown icon fails so the app re-adds");

    payload_init(&p, TRAY_NIM_ADD, 5, 1, TRAY_NIF_TIP | TRAY_NIF_MESSAGE | TRAY_NIF_ICON);
    put_u32(p.bytes, 24, 0x8010);
    put_u32(p.bytes, 28, 0x77);
    put_text(p.bytes, 32, "Discord");
    m = parsed(&p);
    CHECK(tray_apply(t, &m, &e), "add succeeds");
    CHECK(t->count == 1, "one icon, have %d", t->count);
    CHECK(e.kind == TRAY_EFFECT_NONE, "no balloon on plain add");
    CHECK(t->icons[0].callback == 0x8010 && t->icons[0].hicon == 0x77, "fields stored");
    CHECK(text_is(t->icons[0].tip, "Discord"), "tip stored");

    CHECK(tray_apply(t, &m, &e), "re-add of a known icon is accepted");
    CHECK(t->count == 1, "re-add does not duplicate");

    payload_init(&p, TRAY_NIM_MODIFY, 5, 1, TRAY_NIF_ICON);
    put_u32(p.bytes, 28, 0x99);
    put_text(p.bytes, 32, "ignored without NIF_TIP");
    m = parsed(&p);
    CHECK(tray_apply(t, &m, &e), "modify succeeds");
    CHECK(t->icons[0].hicon == 0x99, "icon updated");
    CHECK(text_is(t->icons[0].tip, "Discord"), "tip untouched without NIF_TIP");

    payload_init(&p, TRAY_NIM_ADD, 5, 2, 0);
    m = parsed(&p);
    tray_apply(t, &m, &e);
    CHECK(t->count == 2, "same hwnd, other uID is another icon");

    payload_init(&p, TRAY_NIM_DELETE, 5, 1, 0);
    m = parsed(&p);
    CHECK(tray_apply(t, &m, &e), "delete succeeds");
    CHECK(t->count == 1 && t->icons[0].id.uid == 2, "the right icon went");
    CHECK(!tray_apply(t, &m, &e), "second delete fails");
    free(t);
}

static void test_state_mask(void) {
    TrayTable *t = calloc(1, sizeof *t);
    TrayEffect e;
    Payload p;

    payload_init(&p, TRAY_NIM_ADD, 3, 3, TRAY_NIF_STATE);
    put_u32(p.bytes, 288, TRAY_NIS_HIDDEN | 0x2);
    put_u32(p.bytes, 292, TRAY_NIS_HIDDEN | 0x2);
    TrayMessage m = parsed(&p);
    tray_apply(t, &m, &e);
    CHECK(t->icons[0].state == (TRAY_NIS_HIDDEN | 0x2), "state set");

    payload_init(&p, TRAY_NIM_MODIFY, 3, 3, TRAY_NIF_STATE);
    put_u32(p.bytes, 288, 0);
    put_u32(p.bytes, 292, TRAY_NIS_HIDDEN);
    m = parsed(&p);
    tray_apply(t, &m, &e);
    CHECK(t->icons[0].state == 0x2, "only masked bits change, have %x", t->icons[0].state);
    free(t);
}

static void test_guid_identity(void) {
    TrayTable *t = calloc(1, sizeof *t);
    TrayEffect e;
    Payload p;

    payload_init(&p, TRAY_NIM_ADD, 10, 1, TRAY_NIF_GUID);
    p.bytes[944] = 0x42;
    TrayMessage m = parsed(&p);
    CHECK(tray_apply(t, &m, &e), "guid add");

    payload_init(&p, TRAY_NIM_MODIFY, 11, 9, TRAY_NIF_GUID | TRAY_NIF_MESSAGE);
    p.bytes[944] = 0x42;
    put_u32(p.bytes, 24, 0x9000);
    m = parsed(&p);
    CHECK(tray_apply(t, &m, &e), "guid modify matches on guid, not hwnd/uID");
    CHECK(t->icons[0].id.hwnd == 11, "callbacks follow the newest hwnd");
    CHECK(t->icons[0].callback == 0x9000, "callback updated");

    payload_init(&p, TRAY_NIM_MODIFY, 11, 9, 0);
    m = parsed(&p);
    CHECK(!tray_apply(t, &m, &e), "hwnd/uID does not reach a guid icon");

    TrayIdentity a = { .hwnd = 1, .uid = 2, .has_guid = true };
    TrayIdentity b = { .hwnd = 3, .uid = 4, .has_guid = true };
    a.guid[5] = b.guid[5] = 9;
    CHECK(tray_same_identity(&a, &b), "guid identities ignore hwnd/uID");
    b.guid[5] = 8;
    CHECK(!tray_same_identity(&a, &b), "different guids differ");
    TrayIdentity c = { .hwnd = 1, .uid = 2 };
    CHECK(!tray_same_identity(&a, &c), "guid and hwnd/uID identities never match");
    free(t);
}

static void test_balloons(void) {
    TrayTable *t = calloc(1, sizeof *t);
    TrayEffect e;
    Payload p;

    payload_init(&p, TRAY_NIM_ADD, 8, 1, TRAY_NIF_MESSAGE);
    put_u32(p.bytes, 24, 0x8123);
    TrayMessage m = parsed(&p);
    tray_apply(t, &m, &e);

    payload_init(&p, TRAY_NIM_MODIFY, 8, 1, TRAY_NIF_INFO);
    put_text(p.bytes, 296, "You have a new message");
    put_text(p.bytes, 812, "Chat");
    put_u32(p.bytes, 940, TRAY_NIIF_ERROR);
    m = parsed(&p);
    CHECK(tray_apply(t, &m, &e), "balloon modify");
    CHECK(e.kind == TRAY_EFFECT_SHOW_BALLOON, "balloon shown, kind %d", e.kind);
    CHECK(text_is(e.title, "Chat") && text_is(e.text, "You have a new message"), "balloon text");
    CHECK(e.info_flags == TRAY_NIIF_ERROR, "balloon flags");
    CHECK(e.icon.callback == 0x8123 && e.icon.id.hwnd == 8, "effect carries the callback target");
    CHECK(t->icons[0].balloon_up, "balloon marked up");

    payload_init(&p, TRAY_NIM_MODIFY, 8, 1, TRAY_NIF_INFO);
    m = parsed(&p);
    tray_apply(t, &m, &e);
    CHECK(e.kind == TRAY_EFFECT_HIDE_BALLOON, "empty szInfo hides the balloon");
    CHECK(!t->icons[0].balloon_up, "balloon marked down");

    tray_apply(t, &m, &e);
    CHECK(e.kind == TRAY_EFFECT_NONE, "empty szInfo with no balloon up does nothing");

    payload_init(&p, TRAY_NIM_ADD, 8, 2, TRAY_NIF_INFO);
    put_text(p.bytes, 296, "shown on add");
    m = parsed(&p);
    tray_apply(t, &m, &e);
    CHECK(e.kind == TRAY_EFFECT_SHOW_BALLOON, "NIM_ADD can carry a balloon");

    TrayIdentity id = { .hwnd = 8, .uid = 2 };
    CHECK(tray_balloon_closed(t, &id), "closing marks the balloon down");
    CHECK(!tray_balloon_closed(t, &id), "closing twice reports nothing to close");

    payload_init(&p, TRAY_NIM_MODIFY, 8, 2, TRAY_NIF_INFO);
    put_text(p.bytes, 296, "again");
    m = parsed(&p);
    tray_apply(t, &m, &e);
    payload_init(&p, TRAY_NIM_DELETE, 8, 2, 0);
    m = parsed(&p);
    tray_apply(t, &m, &e);
    CHECK(e.kind == TRAY_EFFECT_HIDE_BALLOON, "deleting an icon hides its balloon");
    CHECK(e.icon.id.uid == 2, "hide effect names the deleted icon");
    free(t);
}

static void test_versions_and_full_table(void) {
    TrayTable *t = calloc(1, sizeof *t);
    TrayEffect e;
    Payload p;

    payload_init(&p, TRAY_NIM_SETVERSION, 4, 4, 0);
    put_u32(p.bytes, 808, TRAY_VERSION_4);
    TrayMessage m = parsed(&p);
    CHECK(!tray_apply(t, &m, &e), "setversion on unknown icon fails");

    payload_init(&p, TRAY_NIM_ADD, 4, 4, 0);
    m = parsed(&p);
    tray_apply(t, &m, &e);
    payload_init(&p, TRAY_NIM_SETVERSION, 4, 4, 0);
    put_u32(p.bytes, 808, TRAY_VERSION_4);
    m = parsed(&p);
    CHECK(tray_apply(t, &m, &e), "setversion");
    CHECK(t->icons[0].version == TRAY_VERSION_4, "version stored");

    payload_init(&p, TRAY_NIM_SETFOCUS, 4, 4, 0);
    m = parsed(&p);
    CHECK(tray_apply(t, &m, &e), "setfocus on a known icon");

    payload_init(&p, 99, 4, 4, 0);
    m = parsed(&p);
    CHECK(!tray_apply(t, &m, &e), "unknown NIM rejected");

    for (uint32_t uid = 100; t->count < TRAY_MAX_ICONS; uid++) {
        payload_init(&p, TRAY_NIM_ADD, 4, uid, 0);
        m = parsed(&p);
        tray_apply(t, &m, &e);
    }
    payload_init(&p, TRAY_NIM_ADD, 4, 5000, 0);
    m = parsed(&p);
    CHECK(!tray_apply(t, &m, &e), "add into a full table fails");
    CHECK(t->count == TRAY_MAX_ICONS, "count capped");
    free(t);
}

static void test_callbacks(void) {
    TrayCallback cb = tray_pack_callback(TRAY_VERSION_4, 7, TRAY_NIN_BALLOONUSERCLICK, 100, 200);
    CHECK(cb.wparam == (100 | 200u << 16), "v4 wParam is the anchor, have %llx",
          (unsigned long long)cb.wparam);
    CHECK(cb.lparam == (TRAY_NIN_BALLOONUSERCLICK | 7 << 16), "v4 lParam is event|uID<<16");

    cb = tray_pack_callback(TRAY_VERSION_4, 0x12345, TRAY_WM_CONTEXTMENU, -5, 3);
    CHECK(((cb.lparam >> 16) & 0xFFFF) == 0x2345, "v4 uID is truncated to 16 bits");
    CHECK((cb.wparam & 0xFFFF) == 0xFFFB, "negative x wraps like MAKEWPARAM");
    CHECK(cb.lparam >= 0, "lParam is zero-extended");

    cb = tray_pack_callback(0, 7, TRAY_NIN_BALLOONTIMEOUT, 100, 200);
    CHECK(cb.wparam == 7 && cb.lparam == TRAY_NIN_BALLOONTIMEOUT, "legacy packing");

    uint32_t ev[TRAY_MAX_CLICK_EVENTS];
    int n = tray_click_events(TRAY_VERSION_4, TRAY_CLICK_LEFT, ev, TRAY_MAX_CLICK_EVENTS);
    CHECK(n == 3 && ev[0] == TRAY_WM_LBUTTONDOWN && ev[1] == TRAY_WM_LBUTTONUP &&
          ev[2] == TRAY_NIN_SELECT, "v4 left click");
    n = tray_click_events(0, TRAY_CLICK_LEFT, ev, TRAY_MAX_CLICK_EVENTS);
    CHECK(n == 2 && ev[1] == TRAY_WM_LBUTTONUP, "legacy left click has no NIN_SELECT");
    n = tray_click_events(TRAY_VERSION_4, TRAY_CLICK_RIGHT, ev, TRAY_MAX_CLICK_EVENTS);
    CHECK(n == 3 && ev[2] == TRAY_WM_CONTEXTMENU, "v4 right click ends in WM_CONTEXTMENU");
    n = tray_click_events(0, TRAY_CLICK_RIGHT, ev, TRAY_MAX_CLICK_EVENTS);
    CHECK(n == 2 && ev[0] == TRAY_WM_RBUTTONDOWN && ev[1] == TRAY_WM_RBUTTONUP, "legacy right click");
    n = tray_click_events(0, TRAY_CLICK_DOUBLE, ev, TRAY_MAX_CLICK_EVENTS);
    CHECK(n == 4 && ev[0] == TRAY_WM_LBUTTONDOWN && ev[1] == TRAY_WM_LBUTTONUP &&
          ev[2] == TRAY_WM_LBUTTONDBLCLK && ev[3] == TRAY_WM_LBUTTONUP,
          "double click follows the real mouse sequence");
    n = tray_click_events(TRAY_VERSION_4, TRAY_CLICK_LEFT, ev, 1);
    CHECK(n == 1 && ev[0] == TRAY_WM_LBUTTONDOWN, "cap respected");
}

static void test_first_line(void) {
    TrayChar in[32] = { ' ', '\n', 'S', 'l', 'a', 'c', 'k', ' ', '\r', '\n', 'x', 0 };
    TrayChar out[16];
    CHECK(tray_first_line(in, out, 16) == 5 && text_is(out, "Slack"), "first line trimmed");

    TrayChar long_in[32];
    for (int i = 0; i < 31; i++) long_in[i] = 'a';
    long_in[31] = 0;
    CHECK(tray_first_line(long_in, out, 4) == 3 && out[3] == 0, "first line capped");

    TrayChar empty[2] = { '\n', 0 };
    CHECK(tray_first_line(empty, out, 16) == 0 && out[0] == 0, "blank tip is empty");
}

int main(void) {
    test_parse_full_layout();
    test_parse_rejects_garbage();
    test_parse_v1_ignores_later_fields();
    test_parse_cbdata_bounds_cbsize();
    test_parse_terminates_full_strings();
    test_add_modify_delete();
    test_state_mask();
    test_guid_identity();
    test_balloons();
    test_versions_and_full_table();
    test_callbacks();
    test_first_line();
    return tests_report("tray_proto");
}
