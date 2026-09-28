#include "tests.h"
#include "toast_xml.h"

#include <stdbool.h>

static ToastContent parsed;

static bool parse(const char *xml) {
    return toast_parse(xml, strlen(xml), &parsed);
}

static bool valid_utf8(const char *s) {
    const unsigned char *p = (const unsigned char *)s;
    while (*p) {
        int tail = *p < 0x80 ? 0 : *p >= 0xF0 ? 3 : *p >= 0xE0 ? 2 : *p >= 0xC0 ? 1 : -1;
        if (tail < 0) return false;
        p++;
        for (int i = 0; i < tail; i++, p++)
            if ((*p & 0xC0) != 0x80) return false;
    }
    return true;
}

static void test_chrome_toast(void) {
    bool ok = parse(
        "<toast launch=\"0|0|Profile 3|Chrome|0|https://claude.ai/|p#https://claude.ai/#010044\" "
        "displayTimestamp=\"2026-09-28T05:42:51Z\">\n"
        " <visual>\n"
        "  <binding template=\"ToastGeneric\">\n"
        "   <text>Claude responded</text>\n"
        "   <text>No new jobs since the last search.</text>\n"
        "   <text placement=\"attribution\">claude.ai</text>\n"
        "   <image placement=\"appLogoOverride\" src=\"C:\\x.tmp\" hint-crop=\"none\"/>\n"
        "  </binding>\n"
        " </visual>\n"
        " <actions>\n"
        "  <action content=\"Go to Chrome notification settings\" placement=\"contextMenu\" "
        "activationType=\"foreground\" arguments=\"2|0|Profile 3\"/>\n"
        " </actions>\n"
        "</toast>");

    CHECK(ok, "Chrome's toast parses");
    CHECK(strcmp(parsed.title, "Claude responded") == 0, "title: '%s'", parsed.title);
    CHECK(strcmp(parsed.body, "No new jobs since the last search.") == 0, "body: '%s'", parsed.body);
    CHECK(strcmp(parsed.attribution, "claude.ai") == 0, "attribution: '%s'", parsed.attribution);
    CHECK(strcmp(parsed.launch, "0|0|Profile 3|Chrome|0|https://claude.ai/|p#https://claude.ai/#010044") == 0,
          "launch: '%s'", parsed.launch);
    CHECK(parsed.activation == TOAST_ACTIVATE_FOREGROUND, "an action's activationType is not the toast's");
    CHECK(!parsed.long_duration, "short by default");
    CHECK(!parsed.launch_truncated, "launch fits");
}

static void test_whatsapp_toast(void) {
    bool ok = parse(
        "<?xml version=\"1.0\" encoding=\"utf-8\"?><toast launch=\"action=Click&amp;key=msg%3Afalse&amp;tag=2165\">"
        "<visual><binding template=\"ToastGeneric\"><text>Nita</text>"
        "<text>Don thirrem per sabah</text>"
        "<image src=\"https://cdn.example/a.jpg?x=1&amp;y=2\" placement=\"appLogoOverride\" hint-crop=\"circle\" />"
        "</binding></visual><audio src=\"ms-appx:///Sounds/pn.m4a\" />"
        "<actions><input id=\"reply\" type=\"text\" placeHolderContent=\"Type a reply\" />"
        "<action content=\"Send\" arguments=\"action=Click\" activationType=\"background\" hint-inputId=\"reply\" />"
        "</actions></toast>");

    CHECK(ok, "WhatsApp's toast parses");
    CHECK(strcmp(parsed.title, "Nita") == 0, "title: '%s'", parsed.title);
    CHECK(strcmp(parsed.body, "Don thirrem per sabah") == 0, "body: '%s'", parsed.body);
    CHECK(strcmp(parsed.launch, "action=Click&key=msg%3Afalse&tag=2165") == 0,
          "entities in launch are decoded: '%s'", parsed.launch);
    CHECK(parsed.attribution[0] == '\0', "no attribution");
}

static void test_legacy_template(void) {
    bool ok = parse(
        "<toast><visual><binding template=\"ToastText04\">"
        "<text id=\"1\">Backup</text><text id=\"2\">42 files</text><text id=\"3\">3 skipped</text>"
        "</binding></visual></toast>");

    CHECK(ok, "a legacy template parses");
    CHECK(strcmp(parsed.title, "Backup") == 0, "title: '%s'", parsed.title);
    CHECK(strcmp(parsed.body, "42 files\n3 skipped") == 0, "lines join with a newline: '%s'", parsed.body);
}

static void test_protocol_activation(void) {
    parse("<toast launch=\"https://example.com/?a=1&amp;b=2\" activationType=\"protocol\">"
          "<visual><binding template=\"ToastGeneric\"><text>Open</text></binding></visual></toast>");
    CHECK(parsed.activation == TOAST_ACTIVATE_PROTOCOL, "protocol activation");
    CHECK(strcmp(parsed.launch, "https://example.com/?a=1&b=2") == 0, "launch: '%s'", parsed.launch);

    parse("<toast activationType='background' launch='x'>"
          "<visual><binding template='ToastGeneric'><text>Quiet</text></binding></visual></toast>");
    CHECK(parsed.activation == TOAST_ACTIVATE_BACKGROUND, "single-quoted background activation");
    CHECK(strcmp(parsed.launch, "x") == 0, "single-quoted launch");
}

static void test_long_duration(void) {
    parse("<toast duration=\"long\"><visual><binding template=\"ToastGeneric\"><text>a</text></binding></visual></toast>");
    CHECK(parsed.long_duration, "duration=long");

    parse("<toast scenario=\"reminder\"><visual><binding template=\"ToastGeneric\"><text>a</text></binding></visual></toast>");
    CHECK(parsed.long_duration, "scenario=reminder stays up");

    parse("<toast scenario=\"incomingCall\"><visual><binding template=\"ToastGeneric\"><text>a</text></binding></visual></toast>");
    CHECK(parsed.long_duration, "scenario=incomingCall stays up");

    parse("<toast duration=\"short\"><visual><binding template=\"ToastGeneric\"><text>a</text></binding></visual></toast>");
    CHECK(!parsed.long_duration, "duration=short");
}

static void test_entities(void) {
    parse("<toast><visual><binding template=\"ToastGeneric\">"
          "<text>&lt;b&gt; &quot;q&quot; &apos;a&apos; &amp; &#233;&#xE9; &#x1F600; &bogus; & x</text>"
          "</binding></visual></toast>");
    CHECK(strcmp(parsed.title, "<b> \"q\" 'a' & \xC3\xA9\xC3\xA9 \xF0\x9F\x98\x80 &bogus; & x") == 0,
          "entities decode: '%s'", parsed.title);

    parse("<toast><visual><binding template=\"ToastGeneric\"><text>&#0;&#xD800;&#x110000;</text>"
          "</binding></visual></toast>");
    CHECK(strcmp(parsed.title, "\xEF\xBF\xBD\xEF\xBF\xBD\xEF\xBF\xBD") == 0,
          "invalid code points become U+FFFD: '%s'", parsed.title);
}

static void test_cdata_and_comments(void) {
    parse("<toast><!-- a comment <text>no</text> --><visual><binding template=\"ToastGeneric\">"
          "<text><![CDATA[a < b & c]]></text><!-- <text>hidden</text> --><text>two</text>"
          "</binding></visual></toast>");
    CHECK(strcmp(parsed.title, "a < b & c") == 0, "CDATA is literal: '%s'", parsed.title);
    CHECK(strcmp(parsed.body, "two") == 0, "comments are skipped: '%s'", parsed.body);
}

static void test_groups_and_whitespace(void) {
    parse("<toast><visual><binding template=\"ToastGeneric\">"
          "<text>\n   Title with space   \n</text>"
          "<text>   </text><text/>"
          "<group><subgroup><text hint-style=\"base\">In a group</text></subgroup>"
          "<subgroup><text>Second column</text></subgroup></group>"
          "<text>line one\r\nline two</text>"
          "</binding></visual></toast>");
    CHECK(strcmp(parsed.title, "Title with space") == 0, "trimmed title: '%s'", parsed.title);
    CHECK(strcmp(parsed.body, "In a group\nSecond column\nline one\nline two") == 0,
          "group texts join the body, CR dropped: '%s'", parsed.body);
}

static void test_first_binding_only(void) {
    parse("<toast><visual>"
          "<binding template=\"ToastGeneric\"><text>first</text></binding>"
          "<binding template=\"ToastText01\"><text>second</text></binding>"
          "</visual><text>outside</text></toast>");
    CHECK(strcmp(parsed.title, "first") == 0, "title: '%s'", parsed.title);
    CHECK(parsed.body[0] == '\0', "later bindings and stray texts are ignored: '%s'", parsed.body);
}

static void test_truncation_keeps_utf8(void) {
    char xml[4096];
    size_t n = (size_t)snprintf(xml, sizeof xml,
        "<toast><visual><binding template=\"ToastGeneric\"><text>");
    for (int i = 0; i < 200; i++) n += (size_t)snprintf(xml + n, sizeof xml - n, "\xE2\x82\xAC");
    snprintf(xml + n, sizeof xml - n, "</text></binding></visual></toast>");

    bool ok = parse(xml);
    CHECK(ok, "a long title still parses");
    CHECK(strlen(parsed.title) < TOAST_TITLE_CAP, "title fits its buffer (%zu)", strlen(parsed.title));
    CHECK(strlen(parsed.title) % 3 == 0, "no partial euro sign (%zu bytes)", strlen(parsed.title));
    CHECK(valid_utf8(parsed.title), "title is valid UTF-8");

    char launch_xml[TOAST_LAUNCH_CAP + 256];
    n = (size_t)snprintf(launch_xml, sizeof launch_xml, "<toast launch=\"");
    for (int i = 0; i < TOAST_LAUNCH_CAP + 10; i++) launch_xml[n++] = 'a';
    snprintf(launch_xml + n, sizeof launch_xml - n,
             "\"><visual><binding template=\"ToastGeneric\"><text>t</text></binding></visual></toast>");
    parse(launch_xml);
    CHECK(parsed.launch_truncated, "an oversized launch string is flagged");
}

static void test_rejects(void) {
    CHECK(!parse(""), "empty input");
    CHECK(!parse("not xml at all"), "plain text");
    CHECK(!parse("<tile><visual><binding template=\"TileSmall\"><text>x</text></binding></visual></tile>"),
          "a tile is not a toast");
    CHECK(!parse("<toast><visual><binding template=\"ToastGeneric\"></binding></visual></toast>"),
          "a toast with no text");
    CHECK(!parse("<toast><visual><binding template=\"ToastGeneric\"><text placement=\"attribution\">a</text>"
                 "</binding></visual></toast>"),
          "attribution alone is nothing to show");
    CHECK(!toast_parse(NULL, 0, &parsed), "NULL input");

    bool ok = parse("<toast><visual><binding template=\"ToastGeneric\"><text>cut off");
    CHECK(ok && strcmp(parsed.title, "cut off") == 0, "a truncated payload keeps what it has: '%s'", parsed.title);
    CHECK(!parse("<toast launch=\"never closed"), "an unterminated tag");
}

static void test_bom(void) {
    bool ok = parse("\xEF\xBB\xBF<toast><visual><binding template=\"ToastGeneric\"><text>bom</text>"
                    "</binding></visual></toast>");
    CHECK(ok && strcmp(parsed.title, "bom") == 0, "a UTF-8 BOM is skipped");
}

int main(void) {
    test_chrome_toast();
    test_whatsapp_toast();
    test_legacy_template();
    test_protocol_activation();
    test_long_duration();
    test_entities();
    test_cdata_and_comments();
    test_groups_and_whitespace();
    test_first_binding_only();
    test_truncation_keeps_utf8();
    test_rejects();
    test_bom();
    return tests_report("toast_xml");
}
