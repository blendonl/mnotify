#ifndef MNOTIFY_TRAY_PROTO_H
#define MNOTIFY_TRAY_PROTO_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define TRAY_SIGNATURE            0x34753423u

#define TRAY_COPY_APPBAR          0
#define TRAY_COPY_NOTIFYICON      1
#define TRAY_COPY_ICONRECT        3

#define TRAY_NIM_ADD              0
#define TRAY_NIM_MODIFY           1
#define TRAY_NIM_DELETE           2
#define TRAY_NIM_SETFOCUS         3
#define TRAY_NIM_SETVERSION       4

#define TRAY_NIF_MESSAGE          0x01
#define TRAY_NIF_ICON             0x02
#define TRAY_NIF_TIP              0x04
#define TRAY_NIF_STATE            0x08
#define TRAY_NIF_INFO             0x10
#define TRAY_NIF_GUID             0x20

#define TRAY_NIS_HIDDEN           0x01

#define TRAY_NIIF_NONE            0
#define TRAY_NIIF_INFO            1
#define TRAY_NIIF_WARNING         2
#define TRAY_NIIF_ERROR           3
#define TRAY_NIIF_ICON_MASK       0x0F

#define TRAY_NIN_SELECT           0x0400
#define TRAY_NIN_BALLOONSHOW      0x0402
#define TRAY_NIN_BALLOONHIDE      0x0403
#define TRAY_NIN_BALLOONTIMEOUT   0x0404
#define TRAY_NIN_BALLOONUSERCLICK 0x0405

#define TRAY_WM_CONTEXTMENU       0x007B
#define TRAY_WM_LBUTTONDOWN       0x0201
#define TRAY_WM_LBUTTONUP         0x0202
#define TRAY_WM_LBUTTONDBLCLK     0x0203
#define TRAY_WM_RBUTTONDOWN       0x0204
#define TRAY_WM_RBUTTONUP         0x0205

#define TRAY_VERSION_4            4

#define TRAY_TIP_CAP              128
#define TRAY_INFO_CAP             256
#define TRAY_TITLE_CAP            64
#define TRAY_MAX_ICONS            96
#define TRAY_MAX_CLICK_EVENTS     5

#define TRAY_HEADER_BYTES         8
#define TRAY_NID_V1_BYTES         152
#define TRAY_NID_V2_BYTES         936
#define TRAY_NID_V3_BYTES         952
#define TRAY_NID_BYTES            956

typedef uint16_t TrayChar;

typedef struct {
    uint32_t message;
    uint32_t hwnd;
    uint32_t uid;
    uint32_t flags;
    uint32_t callback;
    uint32_t hicon;
    uint32_t state;
    uint32_t state_mask;
    uint32_t timeout_or_version;
    uint32_t info_flags;
    uint32_t balloon_icon;
    uint8_t  guid[16];
    TrayChar tip[TRAY_TIP_CAP];
    TrayChar info[TRAY_INFO_CAP];
    TrayChar info_title[TRAY_TITLE_CAP];
} TrayMessage;

typedef struct {
    uint32_t hwnd;
    uint32_t uid;
    uint8_t  guid[16];
    bool     has_guid;
} TrayIdentity;

typedef struct {
    TrayIdentity id;
    uint32_t     callback;
    uint32_t     hicon;
    uint32_t     state;
    uint32_t     version;
    TrayChar     tip[TRAY_TIP_CAP];
    bool         balloon_up;
} TrayIcon;

typedef struct {
    TrayIcon icons[TRAY_MAX_ICONS];
    int      count;
} TrayTable;

typedef enum {
    TRAY_EFFECT_NONE = 0,
    TRAY_EFFECT_SHOW_BALLOON,
    TRAY_EFFECT_HIDE_BALLOON,
} TrayEffectKind;

typedef struct {
    TrayEffectKind kind;
    TrayIcon       icon;
    TrayChar       title[TRAY_TITLE_CAP];
    TrayChar       text[TRAY_INFO_CAP];
    uint32_t       info_flags;
} TrayEffect;

typedef struct {
    uint64_t wparam;
    int64_t  lparam;
} TrayCallback;

typedef enum {
    TRAY_CLICK_LEFT = 0,
    TRAY_CLICK_DOUBLE,
    TRAY_CLICK_RIGHT,
} TrayClick;

bool         tray_parse(const uint8_t *data, size_t len, TrayMessage *out);

bool         tray_apply(TrayTable *table, const TrayMessage *msg, TrayEffect *effect);
bool         tray_same_identity(const TrayIdentity *a, const TrayIdentity *b);
int          tray_find(const TrayTable *table, const TrayIdentity *id);
void         tray_remove_at(TrayTable *table, int index);
bool         tray_balloon_closed(TrayTable *table, const TrayIdentity *id);

TrayCallback tray_pack_callback(uint32_t version, uint32_t uid, uint32_t event,
                                int x, int y);
int          tray_click_events(uint32_t version, TrayClick click,
                               uint32_t *events, int cap);

size_t       tray_first_line(const TrayChar *text, TrayChar *out, size_t cap);

#endif
