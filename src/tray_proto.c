#include "tray_proto.h"

#include <string.h>

#define OFF_SIGNATURE   0
#define OFF_MESSAGE     4
#define NID             TRAY_HEADER_BYTES
#define OFF_CBSIZE      (NID + 0)
#define OFF_HWND        (NID + 4)
#define OFF_UID         (NID + 8)
#define OFF_FLAGS       (NID + 12)
#define OFF_CALLBACK    (NID + 16)
#define OFF_HICON       (NID + 20)
#define OFF_TIP         (NID + 24)
#define OFF_STATE       (NID + 280)
#define OFF_STATE_MASK  (NID + 284)
#define OFF_INFO        (NID + 288)
#define OFF_VERSION     (NID + 800)
#define OFF_INFO_TITLE  (NID + 804)
#define OFF_INFO_FLAGS  (NID + 932)
#define OFF_GUID        (NID + 936)
#define OFF_BALLOON     (NID + 952)

#define TIP_V1_CHARS    64

static uint32_t read_u32(const uint8_t *p) {
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 |
           (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static void read_text(const uint8_t *p, size_t chars, TrayChar *out, size_t cap) {
    size_t limit = chars < cap - 1 ? chars : cap - 1;
    size_t i = 0;
    for (; i < limit; i++) {
        TrayChar c = (TrayChar)(p[i * 2] | p[i * 2 + 1] << 8);
        if (!c) break;
        out[i] = c;
    }
    out[i] = 0;
}

static void copy_text(TrayChar *out, size_t cap, const TrayChar *src) {
    size_t i = 0;
    for (; i + 1 < cap && src[i]; i++) out[i] = src[i];
    out[i] = 0;
}

bool tray_parse(const uint8_t *data, size_t len, TrayMessage *out) {
    if (!data || !out) return false;
    memset(out, 0, sizeof *out);

    if (len < OFF_CBSIZE + 4) return false;
    if (read_u32(data + OFF_SIGNATURE) != TRAY_SIGNATURE) return false;

    size_t size = read_u32(data + OFF_CBSIZE);
    if (size > len - NID) size = len - NID;
    if (size < TRAY_NID_V1_BYTES) return false;

    out->message  = read_u32(data + OFF_MESSAGE);
    out->hwnd     = read_u32(data + OFF_HWND);
    out->uid      = read_u32(data + OFF_UID);
    out->flags    = read_u32(data + OFF_FLAGS);
    out->callback = read_u32(data + OFF_CALLBACK);
    out->hicon    = read_u32(data + OFF_HICON);

    if (size < TRAY_NID_V2_BYTES) {
        read_text(data + OFF_TIP, TIP_V1_CHARS, out->tip, TRAY_TIP_CAP);
        return true;
    }

    read_text(data + OFF_TIP, TRAY_TIP_CAP, out->tip, TRAY_TIP_CAP);
    out->state              = read_u32(data + OFF_STATE);
    out->state_mask         = read_u32(data + OFF_STATE_MASK);
    read_text(data + OFF_INFO, TRAY_INFO_CAP, out->info, TRAY_INFO_CAP);
    out->timeout_or_version = read_u32(data + OFF_VERSION);
    read_text(data + OFF_INFO_TITLE, TRAY_TITLE_CAP, out->info_title, TRAY_TITLE_CAP);
    out->info_flags         = read_u32(data + OFF_INFO_FLAGS);

    if (size >= TRAY_NID_V3_BYTES) memcpy(out->guid, data + OFF_GUID, 16);
    if (size >= TRAY_NID_BYTES)    out->balloon_icon = read_u32(data + OFF_BALLOON);
    return true;
}

static TrayIdentity identity_of(const TrayMessage *msg) {
    TrayIdentity id;
    memset(&id, 0, sizeof id);
    id.hwnd     = msg->hwnd;
    id.uid      = msg->uid;
    id.has_guid = (msg->flags & TRAY_NIF_GUID) != 0;
    if (id.has_guid) memcpy(id.guid, msg->guid, 16);
    return id;
}

bool tray_same_identity(const TrayIdentity *a, const TrayIdentity *b) {
    if (a->has_guid != b->has_guid) return false;
    if (a->has_guid) return memcmp(a->guid, b->guid, 16) == 0;
    return a->hwnd == b->hwnd && a->uid == b->uid;
}

int tray_find(const TrayTable *table, const TrayIdentity *id) {
    for (int i = 0; i < table->count; i++)
        if (tray_same_identity(&table->icons[i].id, id)) return i;
    return -1;
}

void tray_remove_at(TrayTable *table, int index) {
    if (index < 0 || index >= table->count) return;
    memmove(&table->icons[index], &table->icons[index + 1],
            (size_t)(table->count - index - 1) * sizeof table->icons[0]);
    table->count--;
}

bool tray_balloon_closed(TrayTable *table, const TrayIdentity *id) {
    int i = tray_find(table, id);
    if (i < 0 || !table->icons[i].balloon_up) return false;
    table->icons[i].balloon_up = false;
    return true;
}

static void hide_effect(TrayEffect *effect, const TrayIcon *icon) {
    effect->kind = TRAY_EFFECT_HIDE_BALLOON;
    effect->icon = *icon;
}

static void apply_fields(TrayIcon *icon, const TrayMessage *msg, TrayEffect *effect) {
    icon->id.hwnd = msg->hwnd;
    icon->id.uid  = msg->uid;

    if (msg->flags & TRAY_NIF_MESSAGE) icon->callback = msg->callback;
    if (msg->flags & TRAY_NIF_ICON)    icon->hicon    = msg->hicon;
    if (msg->flags & TRAY_NIF_TIP)     copy_text(icon->tip, TRAY_TIP_CAP, msg->tip);
    if (msg->flags & TRAY_NIF_STATE)
        icon->state = (icon->state & ~msg->state_mask) |
                      (msg->state & msg->state_mask);

    if (!(msg->flags & TRAY_NIF_INFO)) return;

    if (msg->info[0]) {
        icon->balloon_up   = true;
        effect->kind       = TRAY_EFFECT_SHOW_BALLOON;
        effect->icon       = *icon;
        effect->info_flags = msg->info_flags;
        copy_text(effect->title, TRAY_TITLE_CAP, msg->info_title);
        copy_text(effect->text,  TRAY_INFO_CAP,  msg->info);
    } else if (icon->balloon_up) {
        icon->balloon_up = false;
        hide_effect(effect, icon);
    }
}

bool tray_apply(TrayTable *table, const TrayMessage *msg, TrayEffect *effect) {
    memset(effect, 0, sizeof *effect);

    TrayIdentity id = identity_of(msg);
    int i = tray_find(table, &id);

    switch (msg->message) {
    case TRAY_NIM_ADD:
        if (i < 0) {
            if (table->count >= TRAY_MAX_ICONS) return false;
            i = table->count++;
            memset(&table->icons[i], 0, sizeof table->icons[i]);
            table->icons[i].id = id;
        }
        apply_fields(&table->icons[i], msg, effect);
        return true;

    case TRAY_NIM_MODIFY:
        if (i < 0) return false;
        apply_fields(&table->icons[i], msg, effect);
        return true;

    case TRAY_NIM_DELETE:
        if (i < 0) return false;
        if (table->icons[i].balloon_up) hide_effect(effect, &table->icons[i]);
        tray_remove_at(table, i);
        return true;

    case TRAY_NIM_SETFOCUS:
        return i >= 0;

    case TRAY_NIM_SETVERSION:
        if (i < 0) return false;
        table->icons[i].version = msg->timeout_or_version;
        return true;

    default:
        return false;
    }
}

TrayCallback tray_pack_callback(uint32_t version, uint32_t uid, uint32_t event,
                                int x, int y) {
    TrayCallback cb;
    if (version >= TRAY_VERSION_4) {
        cb.wparam = (uint64_t)(uint16_t)x | (uint64_t)(uint16_t)y << 16;
        cb.lparam = (int64_t)((uint32_t)(uint16_t)event |
                              (uint32_t)(uint16_t)uid << 16);
    } else {
        cb.wparam = uid;
        cb.lparam = (int64_t)event;
    }
    return cb;
}

int tray_click_events(uint32_t version, TrayClick click, uint32_t *events, int cap) {
    uint32_t seq[TRAY_MAX_CLICK_EVENTS];
    int n = 0;
    bool v4 = version >= TRAY_VERSION_4;

    switch (click) {
    case TRAY_CLICK_RIGHT:
        seq[n++] = TRAY_WM_RBUTTONDOWN;
        seq[n++] = TRAY_WM_RBUTTONUP;
        if (v4) seq[n++] = TRAY_WM_CONTEXTMENU;
        break;
    case TRAY_CLICK_DOUBLE:
        seq[n++] = TRAY_WM_LBUTTONDOWN;
        seq[n++] = TRAY_WM_LBUTTONUP;
        seq[n++] = TRAY_WM_LBUTTONDBLCLK;
        seq[n++] = TRAY_WM_LBUTTONUP;
        break;
    default:
        seq[n++] = TRAY_WM_LBUTTONDOWN;
        seq[n++] = TRAY_WM_LBUTTONUP;
        if (v4) seq[n++] = TRAY_NIN_SELECT;
        break;
    }

    if (n > cap) n = cap;
    memcpy(events, seq, (size_t)n * sizeof seq[0]);
    return n;
}

size_t tray_first_line(const TrayChar *text, TrayChar *out, size_t cap) {
    if (!cap) return 0;
    while (*text == ' ' || *text == '\t' || *text == '\r' || *text == '\n') text++;

    size_t n = 0;
    while (n + 1 < cap && text[n] && text[n] != '\r' && text[n] != '\n') {
        out[n] = text[n];
        n++;
    }
    while (n && (out[n - 1] == ' ' || out[n - 1] == '\t')) n--;
    out[n] = 0;
    return n;
}
