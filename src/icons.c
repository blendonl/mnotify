#define COBJMACROS
#include "mnotify.h"

#include <knownfolders.h>
#include <shlobj.h>
#include <shobjidl.h>

#define ICON_CACHE 48

static AppIcon s_icons[ICON_CACHE];
static int     s_count;
static int     s_next;

static uint32_t *normalized_pixels(HBITMAP bitmap, int size) {
    BITMAP bm;
    if (!GetObjectW(bitmap, sizeof bm, &bm) || bm.bmWidth <= 0 || bm.bmHeight <= 0) return NULL;

    int w = bm.bmWidth, h = bm.bmHeight;
    BITMAPINFO bi;
    memset(&bi, 0, sizeof bi);
    bi.bmiHeader.biSize        = sizeof bi.bmiHeader;
    bi.bmiHeader.biWidth       = w;
    bi.bmiHeader.biHeight      = -h;
    bi.bmiHeader.biPlanes      = 1;
    bi.bmiHeader.biBitCount    = 32;
    bi.bmiHeader.biCompression = BI_RGB;

    uint32_t *src = (uint32_t *)malloc((size_t)w * (size_t)h * sizeof *src);
    uint32_t *out = (uint32_t *)calloc((size_t)size * (size_t)size, sizeof *out);
    HDC       dc  = GetDC(NULL);
    bool      ok  = src && out && GetDIBits(dc, bitmap, 0, (UINT)h, src, &bi, DIB_RGB_COLORS) == h;
    ReleaseDC(NULL, dc);
    if (!ok) {
        free(src);
        free(out);
        return NULL;
    }

    size_t n = (size_t)w * (size_t)h;
    bool   has_alpha = false, straight = false;
    for (size_t i = 0; i < n; i++) {
        unsigned a = src[i] >> 24;
        if (a) has_alpha = true;
        if ((src[i] >> 16 & 0xFF) > a || (src[i] >> 8 & 0xFF) > a || (src[i] & 0xFF) > a)
            straight = true;
    }
    for (size_t i = 0; i < n; i++) {
        uint32_t p = src[i];
        unsigned a = p >> 24;
        if (!has_alpha) {
            src[i] = p | 0xFF000000u;
        } else if (straight) {
            src[i] = (uint32_t)a << 24 | ((p >> 16 & 0xFF) * a / 255) << 16 |
                     ((p >> 8 & 0xFF) * a / 255) << 8 | (p & 0xFF) * a / 255;
        }
    }

    int left = (size - w) / 2, top = (size - h) / 2;
    for (int y = 0; y < h; y++) {
        int oy = top + y;
        if (oy < 0 || oy >= size) continue;
        for (int x = 0; x < w; x++) {
            int ox = left + x;
            if (ox >= 0 && ox < size) out[(size_t)oy * (size_t)size + (size_t)ox] = src[(size_t)y * (size_t)w + (size_t)x];
        }
    }
    free(src);
    return out;
}

static uint32_t *shell_icon(const ToastTarget *target, int size) {
    IShellItemImageFactory *factory = NULL;
    HRESULT hr = SHCreateItemInKnownFolder(&FOLDERID_AppsFolder, KF_FLAG_DONT_VERIFY, target->aumid,
                                           &IID_IShellItemImageFactory, (void **)&factory);
    if (FAILED(hr) && target->exe[0])
        hr = SHCreateItemFromParsingName(target->exe, NULL, &IID_IShellItemImageFactory,
                                         (void **)&factory);
    if (FAILED(hr) || !factory) return NULL;

    HBITMAP bitmap = NULL;
    SIZE    want   = { size, size };
    hr = IShellItemImageFactory_GetImage(factory, want, SIIGBF_ICONONLY, &bitmap);
    IShellItemImageFactory_Release(factory);
    if (FAILED(hr) || !bitmap) {
        log_msg(LOG_DEBUG, L"icons: no icon for %ls (0x%08lx)", target->aumid, (unsigned long)hr);
        return NULL;
    }

    uint32_t *pixels = normalized_pixels(bitmap, size);
    DeleteObject(bitmap);
    return pixels;
}

const AppIcon *icons_find(const wchar_t *aumid, int size) {
    for (int i = 0; i < s_count; i++)
        if (s_icons[i].size == size && _wcsicmp(s_icons[i].aumid, aumid) == 0) return &s_icons[i];
    return NULL;
}

void icons_load(const ToastTarget *target, int size) {
    uint32_t *pixels = shell_icon(target, size);

    AppIcon *icon = &s_icons[s_next];
    s_next = (s_next + 1) % ICON_CACHE;
    if (s_count < ICON_CACHE) s_count++;

    free(icon->pixels);
    mnotify_copy_w(icon->aumid, MNOTIFY_AUMID_CAP, target->aumid);
    icon->size   = size;
    icon->pixels = pixels;
}

void icons_shutdown(void) {
    for (int i = 0; i < s_count; i++) free(s_icons[i].pixels);
    memset(s_icons, 0, sizeof s_icons);
    s_count = 0;
    s_next  = 0;
}
