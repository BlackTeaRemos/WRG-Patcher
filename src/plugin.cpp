#include "internal.h"
#include "wp_util.h"
#include <stdarg.h>
#include <span>

typedef struct {
    WrgEventFn fn;
    void *user;
} EvSub;
static EvSub g_subs[WRG_MAX_PLUGINS];
static int   g_nsubs = 0;

void wrg_plugins_dispatch(WrgEvent ev, const void *data) {
    for (int index = 0; index < g_nsubs; ++index) {
        g_subs[index].fn(ev, data, g_subs[index].user);
    }
}

static void api_logf(const char *fmt, ...) {
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    _vsnprintf(buf, sizeof(buf)-1, fmt, ap);
    va_end(ap);
    buf[sizeof(buf)-1]=0;
    wchar_t wideMessage[1024];
    MultiByteToWideChar(CP_UTF8, 0, buf, -1, wideMessage, 1024);
    wrg_log(L"PLUGIN", wideMessage, NULL);
}

static WrgResult api_redirect(const char *tail, const char *real) {
    return wrg_redirect_add(tail, real);
}
static WrgResult api_splice(const char *tail, uint64_t off, const char *inner,
                            const void *data, uint32_t len) {
    WrgResult r = wrg_splice_add(tail, off, inner, data, len);
    if (r == WRG_OK) {
        wrg_install_read_hooks();   // ensure splice path is live
    }
    return r;
}
static WrgResult api_hook(const char *func, void *repl, void **orig) {
    HMODULE k32 = GetModuleHandleA("kernel32.dll");
    if (orig) {
        *orig = reinterpret_cast<void*>(GetProcAddress(k32, func));   // best-effort original
    }
    return wrg_hook_all(func, repl) > 0 ? WRG_OK : WRG_ERR_NOTFOUND;
}
static WrgResult api_read_mem(uintptr_t address, void *dst, size_t size) {
    SIZE_T got = 0;
    return ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<void*>(address), dst, size, &got) && got == size
           ? WRG_OK : WRG_ERR_RANGE;
}
static WrgResult api_write_mem(uintptr_t address, const void *src, size_t size) {
    auto dst = reinterpret_cast<void*>(address);
    std::span<const std::uint8_t> bytes(static_cast<const std::uint8_t*>(src), size);
    return wp::patch_bytes(dst, bytes) ? WRG_OK : WRG_ERR_RANGE;
}
static uintptr_t api_modbase(const char *moduleName) {
    return reinterpret_cast<uintptr_t>(GetModuleHandleA(moduleName));
}
static WrgResult api_on_event(WrgEventFn fn, void *user) {
    if (g_nsubs >= WRG_MAX_PLUGINS) {
        return WRG_ERR_NOMEM;
    }
    g_subs[g_nsubs].fn = fn;
    g_subs[g_nsubs].user = user;
    g_nsubs++;
    return WRG_OK;
}
static const char *api_gamever(void) {
    return wrg_version_tag();
}

static WrgApi g_api = {
    .api_version   = (WRG_API_VERSION_MAJOR << 16) | WRG_API_VERSION_MINOR,
    .log           = api_logf,
    .redirect_pack = api_redirect,
    .splice        = api_splice,
    .hook_import   = api_hook,
    .read_mem      = api_read_mem,
    .write_mem     = api_write_mem,
    .module_base   = api_modbase,
    .on_event      = api_on_event,
    .game_version  = api_gamever,
};

const WrgApi *wrg_api(void) {
    return &g_api;
}

// ---- loader ---------------------------------------------------------------
#ifndef WRG_RELEASE
static void load_dir(const wchar_t *mod) {
    wchar_t pat[MAX_PATH];
    _snwprintf(pat, MAX_PATH, L"%ls\\%ls\\*.dll", g_modsroot, mod);
    WIN32_FIND_DATAW findData;
    HANDLE findHandle = FindFirstFileW(pat, &findData);
    if (findHandle == INVALID_HANDLE_VALUE) {
        return;
    }
    do {
        wchar_t path[MAX_PATH];
        _snwprintf(path, MAX_PATH, L"%ls\\%ls\\%ls", g_modsroot, mod, findData.cFileName);
        HMODULE pluginModule = LoadLibraryW(path);
        if (!pluginModule) {
            wrg_log(L"PLUGIN-FAIL", path, NULL);
            continue;
        }
        auto init = reinterpret_cast<WrgPluginInitFn>(GetProcAddress(pluginModule, "WrgPluginInit"));
        if (!init) {
            wrg_log(L"PLUGIN-NOENTRY", path, NULL);
            continue;
        }
        WrgPlugin self = {};
        int initResult = init(&g_api, &self);
        if (initResult != WRG_OK) {
            wrg_log(L"PLUGIN-REJECT", path, NULL);
            FreeLibrary(pluginModule);
            continue;
        }
        wchar_t nm[128] = L"?";
        if (self.name) {
            MultiByteToWideChar(CP_UTF8, 0, self.name, -1, nm, 128);
        }
        wrg_log(L"PLUGIN-OK", path, nm);
    } while (FindNextFileW(findHandle, &findData));
    FindClose(findHandle);
}

void wrg_plugins_load_all(void) {
    for (int modIndex = 0; modIndex < g_nmods; ++modIndex) {
        load_dir(g_mods[modIndex]);
    }
}
#endif // WRG_RELEASE
