#ifndef MNOTIFY_CONFIG_TYPES_H
#define MNOTIFY_CONFIG_TYPES_H

#include <stdbool.h>
#include <stdint.h>

#define CONFIG_FONT_CAP     64
#define CONFIG_MAX_VISIBLE  10

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
    CORNER_TOP_CENTER,
    CORNER_BOTTOM_CENTER,
} Corner;

typedef enum {
    MONITOR_CURSOR = 0,
    MONITOR_PRIMARY,
} MonitorPick;

typedef enum {
    CORNERS_ROUND = 0,
    CORNERS_SMALL,
    CORNERS_SQUARE,
} CornerStyle;

typedef enum {
    ACCENT_LEFT = 0,
    ACCENT_NONE,
} AccentStyle;

typedef enum {
    ANIM_NONE = 0,
    ANIM_FADE,
    ANIM_SLIDE,
} AnimStyle;

typedef enum {
    EASE_LINEAR = 0,
    EASE_OUT,
    EASE_IN_OUT,
} Easing;

typedef enum {
    CONFIG_LOG_ERROR = 0,
    CONFIG_LOG_WARN,
    CONFIG_LOG_INFO,
    CONFIG_LOG_DEBUG,
    CONFIG_LOG_TRACE,
} ConfigLogLevel;

typedef struct {
    int  ms;
    bool forever;
} Timeout;

typedef struct {
    Timeout timeout;
    Timeout long_timeout;
    bool    pause_on_hover;
    int     hover_grace;
    int     max_visible;
    bool    hold_when_busy;
    bool    catch_up;
} BehaviorConfig;

typedef struct {
    Corner      corner;
    MonitorPick monitor;
    int         margin;
    int         spacing;
} PositionConfig;

typedef struct {
    uint32_t    bg;
    uint32_t    fg;
    uint32_t    dim;
    uint32_t    border;
    bool        border_none;
    uint32_t    info;
    uint32_t    warn;
    uint32_t    error;
    int         opacity;
    CornerStyle corners;
    char        font[CONFIG_FONT_CAP];
    int         font_size;
    int         title_size;
    int         app_size;
    int         width;
    int         padding;
    int         line_spacing;
    int         body_lines;
    AccentStyle accent;
    int         accent_width;
} ThemeConfig;

typedef struct {
    AnimStyle open;
    AnimStyle close;
    int       duration;
    Easing    easing;
} AnimationConfig;

typedef struct {
    BehaviorConfig  behavior;
    PositionConfig  position;
    ThemeConfig     theme;
    AnimationConfig animation;
    ConfigLogLevel  log_level;
    bool            auto_reload;
    int             notify_ref;
} Config;

void config_defaults(Config *cfg);

#endif
