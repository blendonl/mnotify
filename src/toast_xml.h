#ifndef MNOTIFY_TOAST_XML_H
#define MNOTIFY_TOAST_XML_H

#include <stdbool.h>
#include <stddef.h>

#define TOAST_TITLE_CAP       256
#define TOAST_BODY_CAP        1024
#define TOAST_ATTRIBUTION_CAP 128
#define TOAST_LAUNCH_CAP      2048

typedef enum {
    TOAST_ACTIVATE_FOREGROUND = 0,
    TOAST_ACTIVATE_BACKGROUND,
    TOAST_ACTIVATE_PROTOCOL,
} ToastActivation;

typedef struct {
    char            title[TOAST_TITLE_CAP];
    char            body[TOAST_BODY_CAP];
    char            attribution[TOAST_ATTRIBUTION_CAP];
    char            launch[TOAST_LAUNCH_CAP];
    bool            launch_truncated;
    ToastActivation activation;
    bool            long_duration;
} ToastContent;

bool toast_parse(const char *xml, size_t len, ToastContent *out);

#endif
