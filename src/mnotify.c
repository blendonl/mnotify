#include "mnotify.h"

Mnotify mn;

typedef enum {
    ACTION_RUN = 0,
    ACTION_SEND,
    ACTION_TRAY,
    ACTION_HISTORY,
    ACTION_DISMISS,
    ACTION_QUIT,
} Action;

typedef struct {
    Action   action;
    wchar_t  title[MNOTIFY_TITLE_CAP];
    wchar_t  text[MNOTIFY_TEXT_CAP];
    NoteKind kind;
    int      timeout_ms;
    Corner   corner;
    LogLevel level;
    bool     level_set;
    bool     version;
    bool     usage;
    bool     invalid;
} Options;

void mnotify_copy_w(wchar_t *out, size_t cap, const wchar_t *src) {
    if (!out || cap == 0) return;
    if (!src) { out[0] = L'\0'; return; }

    size_t len = wcslen(src);
    if (len >= cap) len = cap - 1;
    memcpy(out, src, len * sizeof(wchar_t));
    out[len] = L'\0';
}

int mnotify_scale(int px, UINT dpi) {
    if (dpi == 0) dpi = 96;
    return MulDiv(px, (int)dpi, 96);
}

static void console_print(const char *s) {
    if (!AttachConsole(ATTACH_PARENT_PROCESS)) return;

    HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
    if (out && out != INVALID_HANDLE_VALUE) {
        DWORD written = 0;
        WriteFile(out, s, (DWORD)strlen(s), &written, NULL);
        WriteFile(out, "\r\n", 2, &written, NULL);
    }
    FreeConsole();
}

static bool match(const wchar_t *arg, const wchar_t *name) {
    return _wcsicmp(arg, name) == 0;
}

static bool is_flag(const wchar_t *arg) {
    return arg && arg[0] == L'-' && arg[1] == L'-';
}

static bool parse_kind(const wchar_t *name, NoteKind *out) {
    if (match(name, L"info"))  { *out = NOTE_INFO;  return true; }
    if (match(name, L"warn"))  { *out = NOTE_WARN;  return true; }
    if (match(name, L"error")) { *out = NOTE_ERROR; return true; }
    return false;
}

static bool parse_corner(const wchar_t *name, Corner *out) {
    if (match(name, L"bottom-right")) { *out = CORNER_BOTTOM_RIGHT; return true; }
    if (match(name, L"top-right"))    { *out = CORNER_TOP_RIGHT;    return true; }
    if (match(name, L"bottom-left"))  { *out = CORNER_BOTTOM_LEFT;  return true; }
    if (match(name, L"top-left"))     { *out = CORNER_TOP_LEFT;     return true; }
    return false;
}

static void parse_args(Options *opt) {
    memset(opt, 0, sizeof *opt);
    opt->timeout_ms = MNOTIFY_DEFAULT_TIMEOUT_MS;
    opt->corner     = CORNER_BOTTOM_RIGHT;
    opt->level      = LOG_INFO;

    int       argc = 0;
    wchar_t **argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (!argv) return;

    for (int i = 1; i < argc; i++) {
        const wchar_t *a    = argv[i];
        const wchar_t *next = (i + 1 < argc) ? argv[i + 1] : NULL;

        if (match(a, L"--send") && next) {
            opt->action = ACTION_SEND;
            mnotify_copy_w(opt->title, MNOTIFY_TITLE_CAP, next);
            i++;
            if (i + 1 < argc && !is_flag(argv[i + 1])) {
                mnotify_copy_w(opt->text, MNOTIFY_TEXT_CAP, argv[i + 1]);
                i++;
            }
        } else if (match(a, L"--kind") && next) {
            if (!parse_kind(next, &opt->kind)) opt->invalid = true;
            i++;
        } else if (match(a, L"--corner") && next) {
            if (!parse_corner(next, &opt->corner)) opt->invalid = true;
            i++;
        } else if (match(a, L"--timeout") && next) {
            int ms = _wtoi(next);
            if (ms > 0) opt->timeout_ms = ms;
            else        opt->invalid = true;
            i++;
        } else if (match(a, L"--log-level") && next) {
            char name[32];
            WideCharToMultiByte(CP_UTF8, 0, next, -1, name, (int)sizeof name, NULL, NULL);
            if (log_level_from_name(name, &opt->level)) opt->level_set = true;
            else                                        opt->invalid = true;
            i++;
        }
        else if (match(a, L"--tray"))    opt->action  = ACTION_TRAY;
        else if (match(a, L"--history")) opt->action  = ACTION_HISTORY;
        else if (match(a, L"--dismiss")) opt->action  = ACTION_DISMISS;
        else if (match(a, L"--quit"))    opt->action  = ACTION_QUIT;
        else if (match(a, L"--version")) opt->version = true;
        else if (match(a, L"--help") || match(a, L"-h")) opt->usage = true;
        else opt->invalid = true;
    }

    LocalFree(argv);
}

static const char *USAGE =
    "mnotify " MNOTIFY_VERSION " - tray and notification host for shells without Explorer\n"
    "\n"
    "  mnotify                         host the tray (nothing happens if it already is)\n"
    "  mnotify --send <title> [text]   show a notification\n"
    "          --kind info|warn|error    its accent colour\n"
    "  mnotify --tray                  open a menu of tray icons at the cursor\n"
    "  mnotify --history               open or close the notification history; type to search\n"
    "  mnotify --dismiss               close every notification on screen\n"
    "  mnotify --quit                  stop the running instance\n"
    "\n"
    "  Read when the host starts:\n"
    "  --corner <where>                bottom-right|top-right|bottom-left|top-left\n"
    "  --timeout <ms>                  how long a notification stays up (6000)\n"
    "  --log-level <lvl>               error|warn|info|debug|trace\n"
    "\n"
    "  mnotify --version               print the version and exit";

static HWND find_resident(void) {
    for (int attempt = 0; attempt < 40; attempt++) {
        HWND hwnd = FindWindowW(MNOTIFY_CLASS, NULL);
        if (hwnd) return hwnd;
        Sleep(50);
    }
    return NULL;
}

static void fill_payload(const Options *opt, SendPayload *payload) {
    memset(payload, 0, sizeof *payload);
    payload->kind = (uint32_t)opt->kind;
    mnotify_copy_w(payload->title, MNOTIFY_TITLE_CAP, opt->title);
    mnotify_copy_w(payload->text,  MNOTIFY_TEXT_CAP,  opt->text);
}

static int run_as_client(const Options *opt) {
    HWND resident = find_resident();
    if (!resident) {
        console_print("error: mnotify is starting up or wedged; try again");
        return 1;
    }

    switch (opt->action) {
    case ACTION_SEND: {
        SendPayload payload;
        fill_payload(opt, &payload);
        COPYDATASTRUCT cd = {
            .dwData = MNOTIFY_COPY_SEND,
            .cbData = sizeof payload,
            .lpData = &payload,
        };
        DWORD_PTR accepted = 0;
        SendMessageTimeoutW(resident, WM_COPYDATA, 0, (LPARAM)&cd,
                            SMTO_ABORTIFHUNG, 2000, &accepted);
        return accepted ? 0 : 1;
    }
    case ACTION_TRAY:
    case ACTION_HISTORY: {
        DWORD pid = 0;
        GetWindowThreadProcessId(resident, &pid);
        if (pid) AllowSetForegroundWindow(pid);
        PostMessageW(resident, opt->action == ACTION_TRAY ? WM_MNOTIFY_TRAY_MENU : WM_MNOTIFY_HISTORY,
                     MNOTIFY_HISTORY_TOGGLE, 0);
        return 0;
    }
    case ACTION_DISMISS:
        PostMessageW(resident, WM_MNOTIFY_DISMISS, 0, 0);
        return 0;
    case ACTION_QUIT:
        PostMessageW(resident, WM_MNOTIFY_QUIT, 0, 0);
        return 0;
    default:
        return 0;
    }
}

static void show_sent(const SendPayload *payload) {
    Note note;
    memset(&note, 0, sizeof note);
    note.kind = payload->kind <= NOTE_ERROR ? (NoteKind)payload->kind : NOTE_INFO;
    mnotify_copy_w(note.title, MNOTIFY_TITLE_CAP, payload->title);
    mnotify_copy_w(note.text,  MNOTIFY_TEXT_CAP,  payload->text);
    popup_show(&note);
}

static LRESULT CALLBACK control_wndproc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (mn.taskbar_created && msg == mn.taskbar_created) {
        if (tray_host_other_tray_exists()) {
            log_msg(LOG_WARN, L"another tray appeared (Explorer started?); stepping aside");
            PostQuitMessage(0);
        }
        return 0;
    }

    switch (msg) {
    case WM_COPYDATA: {
        const COPYDATASTRUCT *cds = (const COPYDATASTRUCT *)lp;
        if (!cds || cds->dwData != MNOTIFY_COPY_SEND || cds->cbData != sizeof(SendPayload))
            return FALSE;
        SendPayload payload;
        memcpy(&payload, cds->lpData, sizeof payload);
        payload.title[MNOTIFY_TITLE_CAP - 1] = L'\0';
        payload.text[MNOTIFY_TEXT_CAP - 1]   = L'\0';
        show_sent(&payload);
        return TRUE;
    }
    case WM_MNOTIFY_TRAY_MENU:
        menu_show_tray();
        return 0;
    case WM_MNOTIFY_HISTORY:
        if (wp == MNOTIFY_HISTORY_OPEN) history_show();
        else                            history_toggle();
        return 0;
    case WM_MNOTIFY_DISMISS:
        EndMenu();
        history_close();
        popup_dismiss_all();
        return 0;
    case WM_MNOTIFY_QUIT:
        EndMenu();
        history_close();
        PostQuitMessage(0);
        return 0;
    case WM_MNOTIFY_TOAST_FOCUS:
        toast_focus_pending();
        return 0;
    case WM_ENDSESSION:
        if (wp) PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

static bool control_init(void) {
    WNDCLASSEXW wc = {
        .cbSize        = sizeof wc,
        .lpfnWndProc   = control_wndproc,
        .hInstance     = mn.hinst,
        .lpszClassName = MNOTIFY_CLASS,
    };
    if (!RegisterClassExW(&wc)) {
        log_err(L"mnotify: RegisterClassEx failed (%lu)", GetLastError());
        return false;
    }

    mn.control = CreateWindowExW(WS_EX_TOOLWINDOW, MNOTIFY_CLASS, L"mnotify", WS_POPUP,
                                 0, 0, 0, 0, NULL, NULL, mn.hinst, NULL);
    if (!mn.control) {
        log_err(L"mnotify: CreateWindowEx failed (%lu)", GetLastError());
        return false;
    }

    static const UINT allowed[] = {
        WM_COPYDATA, WM_MNOTIFY_TRAY_MENU, WM_MNOTIFY_DISMISS, WM_MNOTIFY_QUIT,
        WM_MNOTIFY_HISTORY,
    };
    for (size_t i = 0; i < sizeof allowed / sizeof allowed[0]; i++)
        ChangeWindowMessageFilterEx(mn.control, allowed[i], MSGFLT_ALLOW, NULL);
    return true;
}

static void control_shutdown(void) {
    if (mn.control) {
        DestroyWindow(mn.control);
        mn.control = NULL;
    }
    UnregisterClassW(MNOTIFY_CLASS, mn.hinst);
}

static int run(HINSTANCE hinst) {
    mn.hinst = hinst;

    Options opt;
    parse_args(&opt);

    if (opt.invalid) { console_print(USAGE);           return 1; }
    if (opt.usage)   { console_print(USAGE);           return 0; }
    if (opt.version) { console_print(MNOTIFY_VERSION); return 0; }

    HANDLE once = CreateMutexW(NULL, TRUE, MNOTIFY_MUTEX);
    if (once && GetLastError() == ERROR_ALREADY_EXISTS) {
        int rc = run_as_client(&opt);
        CloseHandle(once);
        return rc;
    }

    if (opt.action == ACTION_QUIT || opt.action == ACTION_DISMISS) {
        if (once) CloseHandle(once);
        return 0;
    }
    if (opt.action == ACTION_TRAY || opt.action == ACTION_HISTORY) {
        console_print("error: mnotify is not running; start it with `mnotify`");
        if (once) CloseHandle(once);
        return 1;
    }

    log_init(L"mnotify", opt.level_set ? opt.level : LOG_INFO);
    log_msg(LOG_INFO, L"mnotify %hs starting", MNOTIFY_VERSION);

    mn.timeout_ms = opt.timeout_ms;
    mn.corner     = opt.corner;

    HRESULT com = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);

    int rc = 1;
    if (!control_init() || !popup_init() || !history_init()) {
        console_print("error: mnotify could not create its windows; see %LOCALAPPDATA%\\mnotify\\mnotify.log");
    } else if (!tray_host_init()) {
        console_print(tray_host_other_tray_exists()
                      ? "error: another tray is already running (Explorer?); "
                        "mnotify only stands in when there is none"
                      : "error: mnotify could not host the tray; see %LOCALAPPDATA%\\mnotify\\mnotify.log");
    } else {
        toasts_init();
        if (opt.action == ACTION_SEND) {
            SendPayload payload;
            fill_payload(&opt, &payload);
            show_sent(&payload);
        }

        MSG msg;
        while (GetMessageW(&msg, NULL, 0, 0) > 0) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        rc = 0;
    }

    toasts_shutdown();
    tray_host_shutdown();
    history_shutdown();
    popup_shutdown();
    control_shutdown();
    if (SUCCEEDED(com)) CoUninitialize();
    if (once) CloseHandle(once);
    log_msg(LOG_INFO, L"mnotify: exiting");
    log_shutdown();
    return rc;
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance,
                   LPSTR lpCmdLine, int nCmdShow) {
    (void)hPrevInstance;
    (void)lpCmdLine;
    (void)nCmdShow;
    return run(hInstance);
}
