#include "internal.h"
#include "wp_util.h"
#include <format>
#include <string>

wchar_t g_modsroot[MAX_PATH];
wchar_t g_gamedir[MAX_PATH];
wchar_t g_logpath[MAX_PATH];
wchar_t g_mods[WRG_MAX_MODS][128];
int     g_nmods = 0;
CRITICAL_SECTION g_lock;

CreateFileW_t  realCFW;
CreateFileA_t  realCFA;
CloseHandle_t  realCloseHandle;
ReadFile_t     realReadFile;

void wrg_log(const wchar_t *tag, const wchar_t *subjectPath, const wchar_t *targetPath) {
    FILE *file = _wfopen(g_logpath, L"a, ccs=UTF-8");
    if (!file) {
        return;
    }
    std::wstring line = targetPath ? std::format(L"{} {} -> {}\n", tag, subjectPath, targetPath)
                                   : std::format(L"{} {}\n", tag, subjectPath);
    fputws(line.c_str(), file);
    fclose(file);
}

static void add_mod(const wchar_t *modName) {
    if (g_nmods >= WRG_MAX_MODS || !modName || !modName[0]) {
        return;
    }
    wcsncpy(g_mods[g_nmods], modName, 127);
    g_mods[g_nmods][127] = 0;
    g_nmods++;
}

static int load_order_from_file(void) {
    wchar_t loadOrderPath[MAX_PATH];
    _snwprintf(loadOrderPath, MAX_PATH, L"%ls\\load_order.txt", g_modsroot);
    char *fileBytes = wp::read_file_text(loadOrderPath);
    if (!fileBytes) {
        return 0;
    }
    char *textStart = fileBytes;
    if (strlen(fileBytes) >= 3 && (unsigned char)textStart[0]==0xEF && (unsigned char)textStart[1]==0xBB && (unsigned char)textStart[2]==0xBF) {
        textStart += 3;
    }
    int wideCharCount = MultiByteToWideChar(CP_UTF8, 0, textStart, -1, NULL, 0);
    wchar_t *wideText = (wchar_t*)malloc((size_t)wideCharCount * sizeof(wchar_t));
    if (!wideText) {
        free(fileBytes);
        return 0;
    }
    MultiByteToWideChar(CP_UTF8, 0, textStart, -1, wideText, wideCharCount);
    free(fileBytes);
    wchar_t *lineStart = wideText;
    for (wchar_t *cursor = wideText; ; cursor++) {
        if (*cursor == L'\n' || *cursor == 0) {
            wchar_t savedChar = *cursor;
            *cursor = 0;
            wchar_t *trimmed = lineStart;
            while (*trimmed==L' '||*trimmed==L'\t'||*trimmed==L'\r') {
                trimmed++;
            }
            size_t trimmedLen = wcslen(trimmed);
            while (trimmedLen>0 && (trimmed[trimmedLen-1]==L' '||trimmed[trimmedLen-1]==L'\t'||trimmed[trimmedLen-1]==L'\r')) {
                trimmed[--trimmedLen]=0;
            }
            if (trimmed[0] && trimmed[0] != L'#') {
                add_mod(trimmed);
            }
            if (savedChar == 0) {
                break;
            }
            lineStart = cursor + 1;
        }
    }
    free(wideText);
    return g_nmods;
}

static void create_load_order_with_first_mod(void) {
    wchar_t searchPattern[MAX_PATH];
    _snwprintf(searchPattern, MAX_PATH, L"%ls\\*", g_modsroot);
    WIN32_FIND_DATAW findData;
    HANDLE findHandle = FindFirstFileW(searchPattern, &findData);
    if (findHandle == INVALID_HANDLE_VALUE) {
        return;
    }
    wchar_t firstModName[128] = {0};
    do {
        if ((findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) &&
            wcscmp(findData.cFileName, L".") && wcscmp(findData.cFileName, L"..")) {
            wcsncpy(firstModName, findData.cFileName, 127);
            firstModName[127] = 0;
            break;
        }
    } while (FindNextFileW(findHandle, &findData));
    FindClose(findHandle);
    if (!firstModName[0]) {
        return;
    }

    wchar_t loadOrderPath[MAX_PATH];
    _snwprintf(loadOrderPath, MAX_PATH, L"%ls\\load_order.txt", g_modsroot);
    FILE *file = _wfopen(loadOrderPath, L"w, ccs=UTF-8");
    if (file) {
        fputws(firstModName, file);
        fputws(L"\n", file);
        fclose(file);
    }
    add_mod(firstModName);
}

static volatile LONG g_game_loaded = 0;

static HANDLE WINAPI myCFW(LPCWSTR name, DWORD access, DWORD share, LPSECURITY_ATTRIBUTES secAttrs,
                           DWORD creationDisposition, DWORD flagsAndAttributes, HANDLE templateFile) {
    if (wrg_path_wants(name)) {
        if (InterlockedExchange(&g_game_loaded, 1) == 0) {
            wrg_plugins_dispatch(WRG_EV_GAME_LOADED, NULL);
        }
        // overlay takes precedence: open real file (identity intact), track it
        const wchar_t *overlayTail = wrg_overlay_tail_for(name);
        if (overlayTail) {
            HANDLE handle = realCFW(name, access, share, secAttrs, creationDisposition, flagsAndAttributes, templateFile);
            if (handle != INVALID_HANDLE_VALUE) {
                wrg_track_add(handle, overlayTail);
                wrg_track_set_path(handle, name);   // resolve inner-path patches
                wrg_log(L"OVERLAY-OPEN", name, overlayTail);
            }
            return handle;
        }
        wchar_t redirectPath[MAX_PATH];
        if (wrg_redirect_resolve(name, redirectPath)) {
            wrg_log(L"REDIRECT", name, redirectPath);
            return realCFW(redirectPath, access, share, secAttrs, creationDisposition, flagsAndAttributes, templateFile);
        }
    }
    return realCFW(name, access, share, secAttrs, creationDisposition, flagsAndAttributes, templateFile);
}

static HANDLE WINAPI myCFA(LPCSTR name, DWORD access, DWORD share, LPSECURITY_ATTRIBUTES secAttrs,
                           DWORD creationDisposition, DWORD flagsAndAttributes, HANDLE templateFile) {
    if (name) {
        wchar_t wideName[MAX_PATH];
        MultiByteToWideChar(CP_ACP, 0, name, -1, wideName, MAX_PATH);
        if (wrg_path_wants(wideName)) {
            const wchar_t *overlayTail = wrg_overlay_tail_for(wideName);
            if (overlayTail) {
                HANDLE handle = realCFA(name, access, share, secAttrs, creationDisposition, flagsAndAttributes, templateFile);
                if (handle != INVALID_HANDLE_VALUE) {
                    wrg_track_add(handle, overlayTail);
                    wrg_track_set_path(handle, wideName);
                    wrg_log(L"OVERLAY-OPEN(A)", wideName, overlayTail);
                }
                return handle;
            }
            wchar_t redirectPath[MAX_PATH];
            if (wrg_redirect_resolve(wideName, redirectPath)) {
                wrg_log(L"REDIRECT(A)", wideName, redirectPath);
                return realCFW(redirectPath, access, share, secAttrs, creationDisposition, flagsAndAttributes, templateFile);
            }
        }
    }
    return realCFA(name, access, share, secAttrs, creationDisposition, flagsAndAttributes, templateFile);
}

static BOOL WINAPI myCloseHandle(HANDLE handle) {
    wrg_track_remove(handle);   // evict before close so a reused value can't alias
    return realCloseHandle(handle);
}

static void init(void) {
    InitializeCriticalSection(&g_lock);

    GetModuleFileNameW(NULL, g_gamedir, MAX_PATH);
    wchar_t *slash = wcsrchr(g_gamedir, L'\\');
    if (slash) {
        *slash = 0;
    }
    _snwprintf(g_modsroot, MAX_PATH, L"%ls\\mods", g_gamedir);
    _snwprintf(g_logpath,  MAX_PATH, L"%ls\\patcher.log", g_modsroot);
    CreateDirectoryW(g_modsroot, NULL);
    FILE *logFile = _wfopen(g_logpath, L"w, ccs=UTF-8");
    if (logFile) {
        fclose(logFile);
    }

    if (load_order_from_file() == 0) {
        create_load_order_with_first_mod();
    }

    HMODULE kernel32 = GetModuleHandleA("kernel32.dll");
    realCFW         = reinterpret_cast<CreateFileW_t>(GetProcAddress(kernel32, "CreateFileW"));
    realCFA         = reinterpret_cast<CreateFileA_t>(GetProcAddress(kernel32, "CreateFileA"));
    realCloseHandle = reinterpret_cast<CloseHandle_t>(GetProcAddress(kernel32, "CloseHandle"));
    realReadFile    = reinterpret_cast<ReadFile_t>(GetProcAddress(kernel32, "ReadFile"));

    wrg_version_init();
    wrg_version_anchor_init();

    wrg_overlay_load_from_mods();   // legacy .ovl
    wrg_manifest_load_all();        // declarative manifests

    int cfwHookCount         = wrg_hook_all("CreateFileW", reinterpret_cast<void*>(myCFW));
    int cfaHookCount         = wrg_hook_all("CreateFileA", reinterpret_cast<void*>(myCFA));
    int closeHandleHookCount = wrg_hook_all("CloseHandle", reinterpret_cast<void*>(myCloseHandle));
    wrg_install_loadlib_rehook();
    wrg_install_read_hooks();        // no-op unless patches exist
    wrg_install_mapdir_hooks();      // mod map packs without game-tree writes

#ifndef WRG_RELEASE
    wrg_plugins_load_all();          // code-mod DLL loading; disabled in release
#endif
    wrg_ipc_start();

    wchar_t info[256];
    _snwprintf(info, 256, L"ver=%hs mods=%d CFW=%d CFA=%d Close=%d patches=%d",
               wrg_version_tag(), g_nmods, cfwHookCount, cfaHookCount, closeHandleHookCount, wrg_has_patches());
    wrg_log(L"LOADED", g_modsroot, info);
}

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved) {
    (void)reserved;
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(instance);
        init();
    }
    return TRUE;
}
