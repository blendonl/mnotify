#define COBJMACROS
#include "mnotify.h"

#include <appmodel.h>
#include <knownfolders.h>
#include <propsys.h>
#include <shlobj.h>
#include <shobjidl.h>

#define SHORTCUT_MAX_DEPTH 4

typedef struct NotificationActivationCallback NotificationActivationCallback;

typedef struct {
    LPCWSTR Key;
    LPCWSTR Value;
} NotificationUserInput;

typedef struct {
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(NotificationActivationCallback *, REFIID, void **);
    ULONG   (STDMETHODCALLTYPE *AddRef)(NotificationActivationCallback *);
    ULONG   (STDMETHODCALLTYPE *Release)(NotificationActivationCallback *);
    HRESULT (STDMETHODCALLTYPE *Activate)(NotificationActivationCallback *, LPCWSTR app_id,
                                          LPCWSTR arguments, const NotificationUserInput *data,
                                          ULONG count);
} NotificationActivationCallbackVtbl;

struct NotificationActivationCallback {
    const NotificationActivationCallbackVtbl *lpVtbl;
};

typedef struct {
    const ToastTarget *target;
    HWND               found;
} WindowSearch;

static ToastTarget *volatile s_focus_request;

static const IID IID_NOTIFICATION_ACTIVATION_CALLBACK = {
    0x53E31837, 0x6600, 0x4A81, { 0x93, 0x95, 0x75, 0xCF, 0xFE, 0x74, 0x6F, 0x94 }
};

static const PROPERTYKEY PKEY_TOAST_ACTIVATOR = {
    { 0x9F4C2855, 0x9F79, 0x4B39, { 0xA8, 0xD0, 0xE1, 0xD4, 0x2D, 0xE1, 0xD5, 0xF3 } }, 26
};

static const PROPERTYKEY PKEY_APP_ID = {
    { 0x9F4C2855, 0x9F79, 0x4B39, { 0xA8, 0xD0, 0xE1, 0xD4, 0x2D, 0xE1, 0xD5, 0xF3 } }, 5
};

static bool is_packaged(const ToastTarget *target) {
    return wcschr(target->aumid, L'!') != NULL;
}

static bool uri_scheme(const wchar_t *uri, wchar_t *scheme, size_t cap) {
    const wchar_t *colon = wcschr(uri, L':');
    size_t len = colon ? (size_t)(colon - uri) : 0;
    if (len < 2 || len >= cap) return false;

    for (size_t i = 0; i < len; i++) {
        wchar_t c = uri[i];
        bool ok = (c >= L'a' && c <= L'z') || (c >= L'A' && c <= L'Z') ||
                  (i > 0 && ((c >= L'0' && c <= L'9') || c == L'+' || c == L'-' || c == L'.'));
        if (!ok) return false;
    }
    memcpy(scheme, uri, len * sizeof(wchar_t));
    scheme[len] = L'\0';
    return true;
}

static bool open_protocol(const ToastTarget *target) {
    wchar_t scheme[32];
    if (!target->launch_complete || !uri_scheme(target->launch, scheme, 32)) {
        log_msg(LOG_WARN, L"toasts: %ls asked to open something that is not a URI",
                target->aumid);
        return false;
    }
    if (_wcsicmp(scheme, L"file") == 0 || _wcsicmp(scheme, L"shell") == 0) {
        log_msg(LOG_WARN, L"toasts: not opening a %ls: link from %ls (it would start Explorer)",
                scheme, target->aumid);
        return true;
    }

    HINSTANCE rc = ShellExecuteW(NULL, L"open", target->launch, NULL, NULL, SW_SHOWNORMAL);
    if ((INT_PTR)rc <= 32) {
        log_msg(LOG_WARN, L"toasts: opening the %ls: link from %ls failed (%lld)",
                scheme, target->aumid, (long long)(INT_PTR)rc);
        return false;
    }
    log_msg(LOG_INFO, L"toasts: opened the %ls: link from %ls", scheme, target->aumid);
    return true;
}

static bool activator_from_registry(const wchar_t *aumid, CLSID *out) {
    wchar_t key[MNOTIFY_AUMID_CAP + 64];
    _snwprintf(key, MNOTIFY_AUMID_CAP + 63, L"Software\\Classes\\AppUserModelId\\%ls", aumid);
    key[MNOTIFY_AUMID_CAP + 63] = L'\0';

    wchar_t value[64];
    DWORD   size = sizeof value;
    return RegGetValueW(HKEY_CURRENT_USER, key, L"CustomActivator", RRF_RT_REG_SZ, NULL,
                        value, &size) == ERROR_SUCCESS &&
           SUCCEEDED(CLSIDFromString(value, out));
}

static bool shortcut_activator(const wchar_t *path, const wchar_t *aumid, CLSID *out) {
    IPropertyStore *store = NULL;
    if (FAILED(SHGetPropertyStoreFromParsingName(path, NULL, GPS_DEFAULT, &IID_IPropertyStore,
                                                 (void **)&store)))
        return false;

    bool found = false;
    PROPVARIANT id;
    PropVariantInit(&id);
    if (SUCCEEDED(IPropertyStore_GetValue(store, &PKEY_APP_ID, &id)) &&
        id.vt == VT_LPWSTR && id.pwszVal && _wcsicmp(id.pwszVal, aumid) == 0) {
        PROPVARIANT clsid;
        PropVariantInit(&clsid);
        if (SUCCEEDED(IPropertyStore_GetValue(store, &PKEY_TOAST_ACTIVATOR, &clsid)) &&
            clsid.vt == VT_CLSID && clsid.puuid) {
            *out  = *clsid.puuid;
            found = true;
        }
        PropVariantClear(&clsid);
    }
    PropVariantClear(&id);
    IPropertyStore_Release(store);
    return found;
}

static bool activator_in_folder(const wchar_t *dir, const wchar_t *aumid, CLSID *out, int depth) {
    wchar_t pattern[MAX_PATH];
    if (_snwprintf(pattern, MAX_PATH, L"%ls\\*", dir) < 0) return false;
    pattern[MAX_PATH - 1] = L'\0';

    WIN32_FIND_DATAW fd;
    HANDLE find = FindFirstFileW(pattern, &fd);
    if (find == INVALID_HANDLE_VALUE) return false;

    bool found = false;
    do {
        if (fd.cFileName[0] == L'.') continue;

        wchar_t path[MAX_PATH];
        int n = _snwprintf(path, MAX_PATH, L"%ls\\%ls", dir, fd.cFileName);
        if (n < 0 || n >= MAX_PATH) continue;

        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            if (depth < SHORTCUT_MAX_DEPTH && !(fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT))
                found = activator_in_folder(path, aumid, out, depth + 1);
        } else {
            const wchar_t *ext = wcsrchr(fd.cFileName, L'.');
            found = ext && _wcsicmp(ext, L".lnk") == 0 && shortcut_activator(path, aumid, out);
        }
    } while (!found && FindNextFileW(find, &fd));

    FindClose(find);
    return found;
}

static bool activator_from_shortcuts(const wchar_t *aumid, CLSID *out) {
    const KNOWNFOLDERID *folders[] = { &FOLDERID_Programs, &FOLDERID_CommonPrograms };

    for (size_t i = 0; i < sizeof folders / sizeof folders[0]; i++) {
        PWSTR dir = NULL;
        if (FAILED(SHGetKnownFolderPath(folders[i], 0, NULL, &dir))) continue;
        bool found = activator_in_folder(dir, aumid, out, 0);
        CoTaskMemFree(dir);
        if (found) return true;
    }
    return false;
}

static bool run_activator(const ToastTarget *target) {
    if (is_packaged(target)) return false;

    CLSID clsid;
    if (!activator_from_registry(target->aumid, &clsid) &&
        !activator_from_shortcuts(target->aumid, &clsid)) {
        log_msg(LOG_DEBUG, L"toasts: %ls has no toast activator", target->aumid);
        return false;
    }

    NotificationActivationCallback *callback = NULL;
    HRESULT hr = CoCreateInstance(&clsid, NULL, CLSCTX_LOCAL_SERVER,
                                  &IID_NOTIFICATION_ACTIVATION_CALLBACK, (void **)&callback);
    if (FAILED(hr)) {
        log_msg(LOG_WARN, L"toasts: starting the toast activator of %ls failed (0x%08lx)",
                target->aumid, (unsigned long)hr);
        return false;
    }

    CoAllowSetForegroundWindow((IUnknown *)callback, NULL);
    hr = callback->lpVtbl->Activate(callback, target->aumid,
                                    target->launch_complete ? target->launch : L"", NULL, 0);
    callback->lpVtbl->Release(callback);

    if (FAILED(hr)) {
        log_msg(LOG_WARN, L"toasts: %ls refused the activation (0x%08lx)",
                target->aumid, (unsigned long)hr);
        return false;
    }
    log_msg(LOG_INFO, L"toasts: handed the click to %ls", target->aumid);
    return true;
}

static DWORD WINAPI activate_worker(void *arg) {
    ToastTarget *target = (ToastTarget *)arg;
    HRESULT init = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);

    bool handled = target->activation == TOAST_ACTIVATE_PROTOCOL
                   ? open_protocol(target)
                   : run_activator(target);

    if (SUCCEEDED(init)) CoUninitialize();

    if (handled) {
        free(target);
        return 0;
    }
    free(InterlockedExchangePointer((void *volatile *)&s_focus_request, target));
    PostMessageW(mn.control, WM_MNOTIFY_TOAST_FOCUS, 0, 0);
    return 0;
}

static bool start_worker(LPTHREAD_START_ROUTINE routine, const ToastTarget *target) {
    ToastTarget *copy = (ToastTarget *)malloc(sizeof *copy);
    if (!copy) return false;
    *copy = *target;

    HANDLE thread = CreateThread(NULL, 0, routine, copy, 0, NULL);
    if (!thread) {
        log_msg(LOG_WARN, L"toasts: could not start a thread for the click (%lu)", GetLastError());
        free(copy);
        return false;
    }
    CloseHandle(thread);
    return true;
}

void toast_activate(const ToastTarget *target) {
    AllowSetForegroundWindow(ASFW_ANY);
    start_worker(activate_worker, target);
}

static bool same_app_image(const wchar_t *path, const wchar_t *target) {
    const wchar_t *path_name   = wcsrchr(path, L'\\');
    const wchar_t *target_name = wcsrchr(target, L'\\');
    if (!path_name || !target_name || _wcsicmp(path_name, target_name) != 0) return false;

    size_t folder = (size_t)(target_name - target) + 1;
    return _wcsnicmp(path, target, folder) == 0;
}

static bool process_matches(DWORD pid, const ToastTarget *target) {
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!process) return false;

    bool match = false;
    if (is_packaged(target)) {
        wchar_t aumid[MNOTIFY_AUMID_CAP];
        UINT32  len = MNOTIFY_AUMID_CAP;
        match = GetApplicationUserModelId(process, &len, aumid) == ERROR_SUCCESS &&
                _wcsicmp(aumid, target->aumid) == 0;
    } else if (target->exe[0]) {
        wchar_t path[MAX_PATH];
        DWORD   len = MAX_PATH;
        match = QueryFullProcessImageNameW(process, 0, path, &len) &&
                same_app_image(path, target->exe);
    }

    CloseHandle(process);
    return match;
}

static BOOL CALLBACK find_app_window(HWND hwnd, LPARAM lp) {
    WindowSearch *search = (WindowSearch *)lp;

    if (!IsWindowVisible(hwnd) || GetWindow(hwnd, GW_OWNER) ||
        (GetWindowLongW(hwnd, GWL_EXSTYLE) & WS_EX_TOOLWINDOW) ||
        GetWindowTextLengthW(hwnd) == 0)
        return TRUE;

    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (!pid || !process_matches(pid, search->target)) return TRUE;

    search->found = hwnd;
    return FALSE;
}

static bool focus_window(const ToastTarget *target) {
    WindowSearch search = { .target = target };
    EnumWindows(find_app_window, (LPARAM)&search);
    if (!search.found) return false;

    if (IsIconic(search.found)) ShowWindow(search.found, SW_RESTORE);
    bool ok = SetForegroundWindow(search.found);
    log_msg(LOG_INFO, L"toasts: %ls the window of %ls", ok ? L"focused" : L"could not focus",
            target->aumid);
    return true;
}

static bool click_tray_icon(const ToastTarget *target) {
    tray_host_prune();
    for (int i = 0; i < mn.table.count; i++) {
        const TrayIcon *icon = &mn.table.icons[i];
        if (icon->state & TRAY_NIS_HIDDEN) continue;

        DWORD pid = 0;
        GetWindowThreadProcessId(hwnd_of(icon->id.hwnd), &pid);
        if (!pid || !process_matches(pid, target)) continue;

        log_msg(LOG_INFO, L"toasts: clicked the tray icon of %ls to bring it back", target->aumid);
        tray_host_click(icon, TRAY_CLICK_LEFT);
        return true;
    }
    return false;
}

static DWORD WINAPI launch_packaged_worker(void *arg) {
    ToastTarget *target = (ToastTarget *)arg;
    HRESULT init = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);

    IApplicationActivationManager *manager = NULL;
    HRESULT hr = CoCreateInstance(&CLSID_ApplicationActivationManager, NULL, CLSCTX_LOCAL_SERVER,
                                  &IID_IApplicationActivationManager, (void **)&manager);
    if (SUCCEEDED(hr)) {
        DWORD pid = 0;
        hr = IApplicationActivationManager_ActivateApplication(manager, target->aumid, NULL,
                                                               AO_NONE, &pid);
        IApplicationActivationManager_Release(manager);
    }
    log_msg(SUCCEEDED(hr) ? LOG_INFO : LOG_WARN, L"toasts: starting %ls %ls (0x%08lx)",
            target->aumid, SUCCEEDED(hr) ? L"worked" : L"failed", (unsigned long)hr);

    if (SUCCEEDED(init)) CoUninitialize();
    free(target);
    return 0;
}

void toast_focus_pending(void) {
    ToastTarget *target = InterlockedExchangePointer((void *volatile *)&s_focus_request, NULL);
    if (!target) return;

    if (!focus_window(target) && !click_tray_icon(target)) {
        if (is_packaged(target)) start_worker(launch_packaged_worker, target);
        else log_msg(LOG_INFO, L"toasts: %ls has no window to bring forward", target->aumid);
    }
    free(target);
}
