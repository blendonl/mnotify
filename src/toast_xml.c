#include "toast_xml.h"

#include <stdint.h>
#include <string.h>

#define TEXT_SCRATCH_CAP 2048

typedef struct {
    char  *s;
    size_t cap;
    size_t len;
    bool   truncated;
} Buf;

typedef struct {
    const char *name;
    size_t      name_len;
    const char *attrs;
    const char *attrs_end;
    bool        closing;
    bool        self_closing;
} Tag;

static void buf_init(Buf *b, char *s, size_t cap) {
    b->s         = s;
    b->cap       = cap;
    b->len       = 0;
    b->truncated = false;
    if (cap) s[0] = '\0';
}

static size_t utf8_tail_length(unsigned char lead) {
    if (lead >= 0xF0) return 3;
    if (lead >= 0xE0) return 2;
    if (lead >= 0xC0) return 1;
    return 0;
}

static void buf_drop_partial_sequence(Buf *b) {
    size_t start = b->len;
    size_t tail  = 0;
    while (start > 0 && ((unsigned char)b->s[start - 1] & 0xC0) == 0x80) {
        start--;
        tail++;
    }
    if (start == 0) return;

    if (tail < utf8_tail_length((unsigned char)b->s[start - 1])) {
        b->len = start - 1;
        b->s[b->len] = '\0';
    }
}

static void buf_byte(Buf *b, char c) {
    if (b->truncated || !b->cap) return;
    if (b->len + 1 >= b->cap) {
        b->truncated = true;
        buf_drop_partial_sequence(b);
        return;
    }
    b->s[b->len++] = c;
    b->s[b->len]   = '\0';
}

static void buf_bytes(Buf *b, const char *s, size_t n) {
    for (size_t i = 0; i < n; i++) buf_byte(b, s[i]);
}

static void buf_codepoint(Buf *b, uint32_t cp) {
    if (cp == 0 || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) cp = 0xFFFD;

    if (cp < 0x80) {
        buf_byte(b, (char)cp);
    } else if (cp < 0x800) {
        buf_byte(b, (char)(0xC0 | (cp >> 6)));
        buf_byte(b, (char)(0x80 | (cp & 0x3F)));
    } else if (cp < 0x10000) {
        buf_byte(b, (char)(0xE0 | (cp >> 12)));
        buf_byte(b, (char)(0x80 | ((cp >> 6) & 0x3F)));
        buf_byte(b, (char)(0x80 | (cp & 0x3F)));
    } else {
        buf_byte(b, (char)(0xF0 | (cp >> 18)));
        buf_byte(b, (char)(0x80 | ((cp >> 12) & 0x3F)));
        buf_byte(b, (char)(0x80 | ((cp >> 6) & 0x3F)));
        buf_byte(b, (char)(0x80 | (cp & 0x3F)));
    }
}

static bool starts_with(const char *p, const char *end, const char *prefix) {
    size_t n = strlen(prefix);
    return (size_t)(end - p) >= n && memcmp(p, prefix, n) == 0;
}

static const char *find(const char *p, const char *end, const char *needle) {
    size_t n = strlen(needle);
    for (; (size_t)(end - p) >= n; p++)
        if (memcmp(p, needle, n) == 0) return p;
    return NULL;
}

static const char *skip_past(const char *p, const char *end, const char *terminator) {
    const char *at = find(p, end, terminator);
    return at ? at + strlen(terminator) : end;
}

static bool is_space(char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

static int digit_value(char c, bool hex) {
    if (c >= '0' && c <= '9') return c - '0';
    if (!hex) return -1;
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static bool numeric_reference(const char *name, const char *semi, uint32_t *out) {
    bool hex = name + 1 < semi && (name[1] == 'x' || name[1] == 'X');
    const char *d = name + (hex ? 2 : 1);
    if (d == semi) return false;

    uint32_t cp = 0;
    for (; d < semi; d++) {
        int v = digit_value(*d, hex);
        if (v < 0) return false;
        cp = cp * (hex ? 16u : 10u) + (uint32_t)v;
        if (cp > 0x10FFFF) cp = 0x110000;
    }
    *out = cp;
    return true;
}

static const char *decode_entity(const char *amp, const char *end, Buf *b) {
    size_t      window = (size_t)(end - amp) < 12 ? (size_t)(end - amp) : 12;
    const char *semi   = memchr(amp, ';', window);
    const char *name   = amp + 1;
    size_t      n      = semi ? (size_t)(semi - name) : 0;
    uint32_t    cp     = 0;

    if (!semi)                                     { buf_byte(b, '&'); return amp + 1; }
    if (n == 3 && memcmp(name, "amp", 3) == 0)       buf_byte(b, '&');
    else if (n == 2 && memcmp(name, "lt", 2) == 0)   buf_byte(b, '<');
    else if (n == 2 && memcmp(name, "gt", 2) == 0)   buf_byte(b, '>');
    else if (n == 4 && memcmp(name, "quot", 4) == 0) buf_byte(b, '"');
    else if (n == 4 && memcmp(name, "apos", 4) == 0) buf_byte(b, '\'');
    else if (n >= 2 && name[0] == '#' && numeric_reference(name, semi, &cp))
        buf_codepoint(b, cp);
    else { buf_byte(b, '&'); return amp + 1; }

    return semi + 1;
}

static void decode_run(Buf *b, const char *p, const char *end) {
    while (p < end) {
        if (*p == '&')       p = decode_entity(p, end, b);
        else if (*p == '\r') p++;
        else                 buf_byte(b, *p++);
    }
}

static const char *read_tag(const char *p, const char *end, Tag *tag) {
    memset(tag, 0, sizeof *tag);
    const char *q = p + 1;

    if (q < end && *q == '/') { tag->closing = true; q++; }

    tag->name = q;
    while (q < end && !is_space(*q) && *q != '>' && *q != '/') q++;
    tag->name_len = (size_t)(q - tag->name);
    tag->attrs = q;

    char quote = 0;
    for (; q < end; q++) {
        if (quote)                       { if (*q == quote) quote = 0; }
        else if (*q == '"' || *q == '\'')  quote = *q;
        else if (*q == '>')                break;
    }
    if (q >= end) return NULL;

    tag->attrs_end = q;
    if (tag->attrs_end > tag->attrs && tag->attrs_end[-1] == '/') {
        tag->self_closing = true;
        tag->attrs_end--;
    }
    return q + 1;
}

static bool tag_is(const Tag *tag, const char *name) {
    size_t n = strlen(name);
    return tag->name_len == n && memcmp(tag->name, name, n) == 0;
}

static bool tag_attr(const Tag *tag, const char *name, const char **value, size_t *len) {
    size_t      want = strlen(name);
    const char *p    = tag->attrs;
    const char *end  = tag->attrs_end;

    while (p < end) {
        while (p < end && is_space(*p)) p++;
        const char *key = p;
        while (p < end && !is_space(*p) && *p != '=') p++;
        size_t key_len = (size_t)(p - key);

        while (p < end && is_space(*p)) p++;
        if (p >= end || *p != '=') return false;
        p++;
        while (p < end && is_space(*p)) p++;
        if (p >= end || (*p != '"' && *p != '\'')) return false;

        char        quote = *p++;
        const char *val   = p;
        while (p < end && *p != quote) p++;
        if (p >= end) return false;

        if (key_len == want && memcmp(key, name, want) == 0) {
            *value = val;
            *len   = (size_t)(p - val);
            return true;
        }
        p++;
    }
    return false;
}

static bool attr_equals(const Tag *tag, const char *name, const char *expected) {
    const char *value;
    size_t      len;
    return tag_attr(tag, name, &value, &len) &&
           len == strlen(expected) && memcmp(value, expected, len) == 0;
}

static void read_toast_attributes(const Tag *tag, ToastContent *out, Buf *launch) {
    const char *value;
    size_t      len;

    if (tag_attr(tag, "launch", &value, &len)) decode_run(launch, value, value + len);

    if (attr_equals(tag, "activationType", "protocol"))
        out->activation = TOAST_ACTIVATE_PROTOCOL;
    else if (attr_equals(tag, "activationType", "background"))
        out->activation = TOAST_ACTIVATE_BACKGROUND;

    out->long_duration = attr_equals(tag, "duration", "long") ||
                         attr_equals(tag, "scenario", "reminder") ||
                         attr_equals(tag, "scenario", "alarm") ||
                         attr_equals(tag, "scenario", "incomingCall") ||
                         attr_equals(tag, "scenario", "urgent");
}

static const char *read_text_content(const char *p, const char *end, Buf *text) {
    int nested = 0;

    while (p < end) {
        const char *lt = memchr(p, '<', (size_t)(end - p));
        if (!lt) { decode_run(text, p, end); return end; }
        decode_run(text, p, lt);
        p = lt;

        if (starts_with(p, end, "<![CDATA[")) {
            const char *body  = p + 9;
            const char *close = find(body, end, "]]>");
            buf_bytes(text, body, (size_t)((close ? close : end) - body));
            p = close ? close + 3 : end;
            continue;
        }
        if (starts_with(p, end, "<!--")) { p = skip_past(p + 4, end, "-->"); continue; }

        Tag tag;
        const char *after = read_tag(p, end, &tag);
        if (!after) return end;
        p = after;

        if (tag.closing) {
            if (nested == 0) return p;
            nested--;
        } else if (!tag.self_closing) {
            nested++;
        }
    }
    return end;
}

static void trimmed(const char *s, size_t len, const char **start, size_t *n) {
    while (len && is_space(*s))          { s++; len--; }
    while (len && is_space(s[len - 1]))  len--;
    *start = s;
    *n     = len;
}

static void place_text(const Tag *tag, const Buf *text, Buf *title, Buf *body, Buf *attribution) {
    const char *s;
    size_t      n;
    trimmed(text->s, text->len, &s, &n);
    if (!n) return;

    if (attr_equals(tag, "placement", "attribution")) {
        if (!attribution->len) buf_bytes(attribution, s, n);
    } else if (!title->len) {
        buf_bytes(title, s, n);
    } else {
        if (body->len) buf_byte(body, '\n');
        buf_bytes(body, s, n);
    }
}

bool toast_parse(const char *xml, size_t len, ToastContent *out) {
    memset(out, 0, sizeof *out);
    if (!xml) return false;

    const char *p   = xml;
    const char *end = xml + len;
    if (len >= 3 && (unsigned char)p[0] == 0xEF && (unsigned char)p[1] == 0xBB &&
        (unsigned char)p[2] == 0xBF)
        p += 3;

    Buf title, body, attribution, launch;
    buf_init(&title,       out->title,       TOAST_TITLE_CAP);
    buf_init(&body,        out->body,        TOAST_BODY_CAP);
    buf_init(&attribution, out->attribution, TOAST_ATTRIBUTION_CAP);
    buf_init(&launch,      out->launch,      TOAST_LAUNCH_CAP);

    char scratch[TEXT_SCRATCH_CAP];
    int  depth         = 0;
    int  visual_level  = -1;
    int  binding_level = -1;
    bool binding_done  = false;
    bool saw_toast     = false;

    while (p < end) {
        const char *lt = memchr(p, '<', (size_t)(end - p));
        if (!lt) break;
        p = lt;

        if (starts_with(p, end, "<!--"))      { p = skip_past(p + 4, end, "-->"); continue; }
        if (starts_with(p, end, "<![CDATA[")) { p = skip_past(p + 9, end, "]]>"); continue; }
        if (starts_with(p, end, "<?"))        { p = skip_past(p + 2, end, "?>");  continue; }
        if (starts_with(p, end, "<!"))        { p = skip_past(p + 2, end, ">");   continue; }

        Tag tag;
        const char *after = read_tag(p, end, &tag);
        if (!after) break;
        p = after;

        if (tag.closing) {
            if (depth > 0) depth--;
            if (binding_level >= 0 && depth == binding_level) {
                binding_level = -1;
                binding_done  = true;
            }
            if (visual_level >= 0 && depth == visual_level) visual_level = -1;
            continue;
        }

        int level = depth;

        if (tag_is(&tag, "toast") && !saw_toast) {
            saw_toast = true;
            read_toast_attributes(&tag, out, &launch);
        } else if (tag_is(&tag, "visual") && saw_toast && visual_level < 0) {
            if (!tag.self_closing) visual_level = level;
        } else if (tag_is(&tag, "binding") && visual_level >= 0 &&
                   binding_level < 0 && !binding_done) {
            if (!tag.self_closing) binding_level = level;
        } else if (tag_is(&tag, "text") && binding_level >= 0 && !tag.self_closing) {
            Buf text;
            buf_init(&text, scratch, sizeof scratch);
            p = read_text_content(p, end, &text);
            place_text(&tag, &text, &title, &body, &attribution);
            continue;
        }

        if (!tag.self_closing) depth++;
    }

    out->launch_truncated = launch.truncated;
    return saw_toast && (out->title[0] || out->body[0]);
}
