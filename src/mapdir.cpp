// Mod-provided map packs served without touching the game tree.
//
// The engine discovers authored maps two ways: existence probes on the
// pack path (GetFileAttributes) and directory enumeration of
// Maps\WarGame\PC (FindFirstFile family). CreateFileW opens are already
// covered by the redirect table's mods-tail fallback, so this module adds
// the remaining two surfaces: attribute probes fall through to the
// resolved mod file, and enumerations of the Maps directory have each
// active mod's extra .dat names injected after the real listing.

#include "internal.h"
#include "wp_util.h"
#include <shlwapi.h>
#pragma comment(lib, "shlwapi.lib")

#define WRG_MAX_MAPDIR_FINDS  8
#define WRG_MAX_INJECT        64

typedef struct {
    HANDLE  real;                              // INVALID_HANDLE_VALUE once drained
    int     realDrained;
    int     ansi;                              // WRG_FIND_ANSI when opened via the A surface
    int     next;                              // cursor into names
    int     count;
    int     directories;                       // entries are dirs, stat'd from `dir`
    wchar_t names[WRG_MAX_INJECT][MAX_PATH];   // injected file names (no dir)
    wchar_t dir[MAX_PATH];                     // mods\<mod> dir each name lives in
    wchar_t sub[MAX_PATH];                     // the Maps sub path, for stat
} MapFind;

static MapFind *g_finds[WRG_MAX_MAPDIR_FINDS];

typedef DWORD  (WINAPI *GetFileAttributesW_t)(LPCWSTR);
typedef DWORD  (WINAPI *GetFileAttributesA_t)(LPCSTR);
typedef BOOL   (WINAPI *GetFileAttributesExW_t)(LPCWSTR, GET_FILEEX_INFO_LEVELS, LPVOID);
typedef BOOL   (WINAPI *GetFileAttributesExA_t)(LPCSTR, GET_FILEEX_INFO_LEVELS, LPVOID);
typedef HANDLE (WINAPI *FindFirstFileW_t)(LPCWSTR, LPWIN32_FIND_DATAW);
typedef HANDLE (WINAPI *FindFirstFileA_t)(LPCSTR, LPWIN32_FIND_DATAA);
typedef HANDLE (WINAPI *FindFirstFileExW_t)(LPCWSTR, FINDEX_INFO_LEVELS, LPVOID, FINDEX_SEARCH_OPS, LPVOID, DWORD);
typedef HANDLE (WINAPI *FindFirstFileExA_t)(LPCSTR, FINDEX_INFO_LEVELS, LPVOID, FINDEX_SEARCH_OPS, LPVOID, DWORD);
typedef BOOL   (WINAPI *FindNextFileW_t)(HANDLE, LPWIN32_FIND_DATAW);
typedef BOOL   (WINAPI *FindNextFileA_t)(HANDLE, LPWIN32_FIND_DATAA);
typedef BOOL   (WINAPI *FindClose_t)(HANDLE);

static GetFileAttributesW_t   realGFAW;
static GetFileAttributesA_t   realGFAA;
static GetFileAttributesExW_t realGFAEW;
static GetFileAttributesExA_t realGFAEA;
static FindFirstFileW_t       realFFFW;
static FindFirstFileA_t       realFFFA;
static FindFirstFileExW_t     realFFFEW;
static FindFirstFileExA_t     realFFFEA;
static FindNextFileW_t        realFNFW;
static FindNextFileA_t        realFNFA;
static FindClose_t            realFC;

// A find handle opened through the ANSI surface serves ANSI records.
#define WRG_FIND_ANSI 1

#define WRG_INJECT_MAP       0   // mod-provided map packs
#define WRG_INJECT_TIER_DIR  1   // tier directories, into their parent's listing
#define WRG_INJECT_TIER_PACK 2   // the packs a tier itself holds

static void find_data_narrow(const WIN32_FIND_DATAW *wide, LPWIN32_FIND_DATAA narrow) {
    memset(narrow, 0, sizeof(*narrow));
    narrow->dwFileAttributes = wide->dwFileAttributes;
    narrow->ftCreationTime = wide->ftCreationTime;
    narrow->ftLastAccessTime = wide->ftLastAccessTime;
    narrow->ftLastWriteTime = wide->ftLastWriteTime;
    narrow->nFileSizeHigh = wide->nFileSizeHigh;
    narrow->nFileSizeLow = wide->nFileSizeLow;
    WideCharToMultiByte(CP_ACP, 0, wide->cFileName, -1, narrow->cFileName, MAX_PATH, NULL, NULL);
}

static const wchar_t *find_insensitive(const wchar_t *haystack, const wchar_t *needle) {
    size_t needleLen = wcslen(needle);
    for (const wchar_t *cursor = haystack; *cursor; ++cursor) {
        if (_wcsnicmp(cursor, needle, needleLen) == 0) {
            return cursor;
        }
    }
    return NULL;
}

// The game-relative sub path of an enumeration we serve, or 0.
static int mapdir_pattern_split(const wchar_t *pattern, wchar_t *sub, wchar_t *spec,
                                int *directories) {
    if (!pattern) {
        return 0;
    }
    wchar_t normalized[MAX_PATH];
    wp::copy_truncated(normalized, pattern, MAX_PATH);
    wp::normalize_separators(normalized);
    wchar_t *lastSlash = wcsrchr(normalized, L'\\');
    if (!lastSlash) {
        return 0;
    }
    *lastSlash = 0;
    // Absolute patterns carry "...\Maps\..."; a relative pattern (the game's
    // working directory IS the install dir) starts with "Maps\" outright.
    const wchar_t *marker = find_insensitive(normalized, L"\\Maps\\");
    if (marker) {
        marker += 1;
    } else if (_wcsnicmp(normalized, L"Maps\\", 5) == 0) {
        marker = normalized;
    }
    if (marker) {
        *directories = 0;
        wp::copy_truncated(sub, marker, MAX_PATH);
        wp::copy_truncated(spec, lastSlash + 1, MAX_PATH);
        return 1;
    }
    marker = find_insensitive(normalized, L"\\Data\\");
    if (marker) {
        marker += 1;
    } else if (_wcsnicmp(normalized, L"Data\\", 5) == 0) {
        marker = normalized;
    } else {
        return 0;
    }
    *directories = 1;
    wp::copy_truncated(sub, marker, MAX_PATH);
    wp::copy_truncated(spec, lastSlash + 1, MAX_PATH);
    return 1;
}

static const wchar_t *mapdir_parent_revision(const wchar_t *sub) {
    const wchar_t *lastSlash = wcsrchr(sub, L'\\');
    return lastSlash ? lastSlash + 1 : sub;
}

static int mapdir_tier_directory(const wchar_t *sub, wchar_t *out) {
    const wchar_t *lastSlash = wcsrchr(sub, L'\\');
    if (!lastSlash) {
        return 0;
    }
    const wchar_t *leaf = lastSlash + 1;
    wchar_t head[MAX_PATH];
    wp::copy_truncated(head, sub, MAX_PATH);
    head[lastSlash - sub] = 0;
    const wchar_t *parentSlash = wcsrchr(head, L'\\');
    const wchar_t *parent = parentSlash ? parentSlash + 1 : head;
    if (wrg_tier_dir_of(parent, leaf, out)) {
        return 1;
    }

    return wrg_tier_dir_by_id(leaf, out);
}

// Fill `find` with the packs a declared tier holds that the game tree lacks.
static void mapdir_collect_tier_packs(MapFind *find, const wchar_t *tierDir,
                                      const wchar_t *sub, const wchar_t *spec) {
    find->count = 0;
    wp::copy_truncated(find->sub, sub, MAX_PATH);
    wp::copy_truncated(find->dir, tierDir, MAX_PATH);
    wchar_t searchPattern[MAX_PATH];
    int written = _snwprintf(searchPattern, MAX_PATH, L"%ls\\*", tierDir);
    if (written < 0 || written >= MAX_PATH) {
        return;
    }
    WIN32_FIND_DATAW data;
    HANDLE handle = realFFFW(searchPattern, &data);
    if (handle == INVALID_HANDLE_VALUE) {
        return;
    }
    do {
        if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            continue;
        }
        if (!PathMatchSpecW(data.cFileName, spec)) {
            continue;
        }
        wp::copy_truncated(find->names[find->count], data.cFileName, MAX_PATH);
        find->count++;
    } while (find->count < WRG_MAX_INJECT && realFNFW(handle, &data));
    realFC(handle);
}

static void mapdir_collect_tiers(MapFind *find, const wchar_t *sub, const wchar_t *spec) {
    find->count = 0;
    wp::copy_truncated(find->sub, sub, MAX_PATH);
    const wchar_t *parent = mapdir_parent_revision(sub);
    const wchar_t *children[WRG_MAX_INJECT];
    int childCount = wrg_tier_children_of(parent, children, WRG_MAX_INJECT);

    if (childCount == 0 && _wcsicmp(parent, L"PC") == 0) {
        childCount = wrg_tier_all(children, WRG_MAX_INJECT);
    }
    for (int childIndex = 0; childIndex < childCount && find->count < WRG_MAX_INJECT; ++childIndex) {
        if (!PathMatchSpecW(children[childIndex], spec)) {
            continue;
        }
        wchar_t gamePath[MAX_PATH];
        int written = _snwprintf(gamePath, MAX_PATH, L"%ls\\%ls\\%ls",
                                 g_gamedir, sub, children[childIndex]);
        if (written < 0 || written >= MAX_PATH) {
            continue;
        }
        if (realGFAW(gamePath) != INVALID_FILE_ATTRIBUTES) {
            continue;   // the game carries this revision; the real listing serves it
        }
        wp::copy_truncated(find->names[find->count], children[childIndex], MAX_PATH);
        find->count++;
    }
}

// Fill `find` with every mod-provided name under `sub` matching `spec`
// that the real game directory does not carry.
static void mapdir_collect(MapFind *find, const wchar_t *sub, const wchar_t *spec) {
    find->count = 0;
    wp::copy_truncated(find->sub, sub, MAX_PATH);
    for (int modIndex = 0; modIndex < g_nmods && find->count < WRG_MAX_INJECT; ++modIndex) {
        wchar_t modDir[MAX_PATH];
        _snwprintf(modDir, MAX_PATH, L"%ls\\%ls", g_modsroot, g_mods[modIndex]);
        wchar_t searchPattern[MAX_PATH];
        int written = _snwprintf(searchPattern, MAX_PATH, L"%ls\\%ls\\*", modDir, sub);
        if (written < 0 || written >= MAX_PATH) {
            continue;
        }
        WIN32_FIND_DATAW data;
        HANDLE handle = realFFFW(searchPattern, &data);
        if (handle == INVALID_HANDLE_VALUE) {
            continue;
        }
        do {
            if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                continue;
            }
            if (!PathMatchSpecW(data.cFileName, spec)) {
                continue;
            }
            wchar_t gamePath[MAX_PATH];
            written = _snwprintf(gamePath, MAX_PATH, L"%ls\\%ls\\%ls", g_gamedir, sub, data.cFileName);
            if (written < 0 || written >= MAX_PATH) {
                continue;
            }
            if (realGFAW(gamePath) != INVALID_FILE_ATTRIBUTES) {
                continue;   // the game carries it; the real listing serves it
            }
            int duplicate = 0;
            for (int nameIndex = 0; nameIndex < find->count; ++nameIndex) {
                if (_wcsicmp(find->names[nameIndex], data.cFileName) == 0) {
                    duplicate = 1;
                    break;
                }
            }
            if (duplicate) {
                continue;
            }
            wp::copy_truncated(find->names[find->count], data.cFileName, MAX_PATH);
            wp::copy_truncated(find->dir, modDir, MAX_PATH);
            find->count++;
        } while (find->count < WRG_MAX_INJECT && realFNFW(handle, &data));
        realFC(handle);
    }
}

// Produce the injected entry at `find->next` into `out`. 0 when drained.
static int mapdir_serve(MapFind *find, LPWIN32_FIND_DATAW out) {
    while (find->next < find->count) {
        const wchar_t *name = find->names[find->next++];
        wchar_t fullPath[MAX_PATH];
        int written;
        if (find->directories == WRG_INJECT_TIER_DIR) {
            const wchar_t *parent = mapdir_parent_revision(find->sub);
            if (!wrg_tier_dir_of(parent, name, fullPath)) {
                continue;
            }
            written = 0;
        } else if (find->directories == WRG_INJECT_TIER_PACK) {
            written = _snwprintf(fullPath, MAX_PATH, L"%ls\\%ls", find->dir, name);
        } else {
            written = _snwprintf(fullPath, MAX_PATH, L"%ls\\%ls\\%ls", find->dir, find->sub, name);
        }
        if (written < 0 || written >= MAX_PATH) {
            continue;
        }
        WIN32_FILE_ATTRIBUTE_DATA attributes;
        if (!realGFAEW(fullPath, GetFileExInfoStandard, &attributes)) {
            continue;
        }
        memset(out, 0, sizeof(*out));
        out->dwFileAttributes = attributes.dwFileAttributes;
        out->ftCreationTime = attributes.ftCreationTime;
        out->ftLastAccessTime = attributes.ftLastAccessTime;
        out->ftLastWriteTime = attributes.ftLastWriteTime;
        out->nFileSizeHigh = attributes.nFileSizeHigh;
        out->nFileSizeLow = attributes.nFileSizeLow;
        wp::copy_truncated(out->cFileName, name, MAX_PATH);
        wrg_log(L"MAPDIR-INJECT", name, NULL);
        return 1;
    }
    return 0;
}

static MapFind *mapfind_lookup(HANDLE handle) {
    for (int slot = 0; slot < WRG_MAX_MAPDIR_FINDS; ++slot) {
        if (g_finds[slot] == (MapFind*)handle) {
            return g_finds[slot];
        }
    }
    return NULL;
}

static HANDLE mapfind_wrap(HANDLE real, const wchar_t *sub, const wchar_t *spec, int ansi,
                           int directories) {
    MapFind *find = (MapFind*)calloc(1, sizeof(MapFind));
    if (!find) {
        return real;
    }
    find->real = real;
    find->realDrained = (real == INVALID_HANDLE_VALUE);
    find->ansi = ansi;
    find->directories = directories;
    if (directories) {
        wchar_t tierDir[MAX_PATH];
        if (mapdir_tier_directory(sub, tierDir)) {
            find->directories = WRG_INJECT_TIER_PACK;
            mapdir_collect_tier_packs(find, tierDir, sub, spec);
        } else {
            find->directories = WRG_INJECT_TIER_DIR;
            mapdir_collect_tiers(find, sub, spec);
        }
    } else {
        mapdir_collect(find, sub, spec);
    }
    if (find->count == 0) {
        if (directories) {
            wrg_log(L"TIER-SCAN-EMPTY", sub, spec);
        }
        free(find);
        return real;   // nothing to inject: hand the real handle through
    }
    wrg_log(directories ? L"TIER-SCAN" : L"MAPDIR-SCAN", sub, spec);
    wp::CritLock lock(g_lock);
    for (int slot = 0; slot < WRG_MAX_MAPDIR_FINDS; ++slot) {
        if (!g_finds[slot]) {
            g_finds[slot] = find;
            return (HANDLE)find;
        }
    }
    free(find);
    return real;
}

static HANDLE WINAPI myFFFW(LPCWSTR pattern, LPWIN32_FIND_DATAW data) {
    wchar_t sub[MAX_PATH], spec[MAX_PATH];
    int directories = 0;
    if (!mapdir_pattern_split(pattern, sub, spec, &directories)) {
        return realFFFW(pattern, data);
    }
    HANDLE real = realFFFW(pattern, data);
    HANDLE wrapped = mapfind_wrap(real, sub, spec, 0, directories);
    if (wrapped == real) {
        return real;
    }
    MapFind *find = (MapFind*)wrapped;
    if (find->realDrained && !mapdir_serve(find, data)) {
        // real empty and nothing injectable after all: unwind
        wp::CritLock lock(g_lock);
        for (int slot = 0; slot < WRG_MAX_MAPDIR_FINDS; ++slot) {
            if (g_finds[slot] == find) {
                g_finds[slot] = NULL;
            }
        }
        free(find);
        SetLastError(ERROR_FILE_NOT_FOUND);
        return INVALID_HANDLE_VALUE;
    }
    return wrapped;   // real's first entry already in `data` when not drained
}

static HANDLE WINAPI myFFFEW(LPCWSTR pattern, FINDEX_INFO_LEVELS level, LPVOID data,
                             FINDEX_SEARCH_OPS op, LPVOID filter, DWORD flags) {
    wchar_t sub[MAX_PATH], spec[MAX_PATH];
    int directories = 0;
    if (!mapdir_pattern_split(pattern, sub, spec, &directories) || level == FindExInfoMaxInfoLevel) {
        return realFFFEW(pattern, level, data, op, filter, flags);
    }
    HANDLE real = realFFFEW(pattern, level, data, op, filter, flags);
    HANDLE wrapped = mapfind_wrap(real, sub, spec, 0, directories);
    if (wrapped == real) {
        return real;
    }
    MapFind *find = (MapFind*)wrapped;
    if (find->realDrained && !mapdir_serve(find, (LPWIN32_FIND_DATAW)data)) {
        wp::CritLock lock(g_lock);
        for (int slot = 0; slot < WRG_MAX_MAPDIR_FINDS; ++slot) {
            if (g_finds[slot] == find) {
                g_finds[slot] = NULL;
            }
        }
        free(find);
        SetLastError(ERROR_FILE_NOT_FOUND);
        return INVALID_HANDLE_VALUE;
    }
    return wrapped;
}

static BOOL WINAPI myFNFW(HANDLE handle, LPWIN32_FIND_DATAW data) {
    MapFind *find = mapfind_lookup(handle);
    if (!find) {
        return realFNFW(handle, data);
    }
    if (!find->realDrained) {
        if (realFNFW(find->real, data)) {
            return TRUE;
        }
        find->realDrained = 1;
    }
    if (mapdir_serve(find, data)) {
        return TRUE;
    }
    SetLastError(ERROR_NO_MORE_FILES);
    return FALSE;
}

static HANDLE WINAPI myFFFA(LPCSTR pattern, LPWIN32_FIND_DATAA data) {
    wchar_t widePattern[MAX_PATH];
    if (!pattern || !MultiByteToWideChar(CP_ACP, 0, pattern, -1, widePattern, MAX_PATH)) {
        return realFFFA(pattern, data);
    }
    wchar_t sub[MAX_PATH], spec[MAX_PATH];
    int directories = 0;
    if (!mapdir_pattern_split(widePattern, sub, spec, &directories)) {
        return realFFFA(pattern, data);
    }
    HANDLE real = realFFFA(pattern, data);
    HANDLE wrapped = mapfind_wrap(real, sub, spec, WRG_FIND_ANSI, directories);
    if (wrapped == real) {
        return real;
    }
    MapFind *find = (MapFind*)wrapped;
    if (find->realDrained) {
        WIN32_FIND_DATAW wide;
        if (!mapdir_serve(find, &wide)) {
            wp::CritLock lock(g_lock);
            for (int slot = 0; slot < WRG_MAX_MAPDIR_FINDS; ++slot) {
                if (g_finds[slot] == find) {
                    g_finds[slot] = NULL;
                }
            }
            free(find);
            SetLastError(ERROR_FILE_NOT_FOUND);
            return INVALID_HANDLE_VALUE;
        }
        find_data_narrow(&wide, data);
    }
    return wrapped;
}

static HANDLE WINAPI myFFFEA(LPCSTR pattern, FINDEX_INFO_LEVELS level, LPVOID data,
                             FINDEX_SEARCH_OPS op, LPVOID filter, DWORD flags) {
    wchar_t widePattern[MAX_PATH];
    if (!pattern || !MultiByteToWideChar(CP_ACP, 0, pattern, -1, widePattern, MAX_PATH)) {
        return realFFFEA(pattern, level, data, op, filter, flags);
    }
    wchar_t sub[MAX_PATH], spec[MAX_PATH];
    int directories = 0;
    if (!mapdir_pattern_split(widePattern, sub, spec, &directories) || level == FindExInfoMaxInfoLevel) {
        return realFFFEA(pattern, level, data, op, filter, flags);
    }
    HANDLE real = realFFFEA(pattern, level, data, op, filter, flags);
    HANDLE wrapped = mapfind_wrap(real, sub, spec, WRG_FIND_ANSI, directories);
    if (wrapped == real) {
        return real;
    }
    MapFind *find = (MapFind*)wrapped;
    if (find->realDrained) {
        WIN32_FIND_DATAW wide;
        if (!mapdir_serve(find, &wide)) {
            wp::CritLock lock(g_lock);
            for (int slot = 0; slot < WRG_MAX_MAPDIR_FINDS; ++slot) {
                if (g_finds[slot] == find) {
                    g_finds[slot] = NULL;
                }
            }
            free(find);
            SetLastError(ERROR_FILE_NOT_FOUND);
            return INVALID_HANDLE_VALUE;
        }
        find_data_narrow(&wide, (LPWIN32_FIND_DATAA)data);
    }
    return wrapped;
}

static BOOL WINAPI myFNFA(HANDLE handle, LPWIN32_FIND_DATAA data) {
    MapFind *find = mapfind_lookup(handle);
    if (!find) {
        return realFNFA(handle, data);
    }
    if (!find->realDrained) {
        if (realFNFA(find->real, data)) {
            return TRUE;
        }
        find->realDrained = 1;
    }
    WIN32_FIND_DATAW wide;
    if (mapdir_serve(find, &wide)) {
        find_data_narrow(&wide, data);
        return TRUE;
    }
    SetLastError(ERROR_NO_MORE_FILES);
    return FALSE;
}

static BOOL WINAPI myFC(HANDLE handle) {
    MapFind *find = mapfind_lookup(handle);
    if (!find) {
        return realFC(handle);
    }
    {
        wp::CritLock lock(g_lock);
        for (int slot = 0; slot < WRG_MAX_MAPDIR_FINDS; ++slot) {
            if (g_finds[slot] == find) {
                g_finds[slot] = NULL;
            }
        }
    }
    BOOL result = TRUE;
    if (find->real != INVALID_HANDLE_VALUE) {
        result = realFC(find->real);
    }
    free(find);
    return result;
}

static DWORD WINAPI myGFAW(LPCWSTR name) {
    DWORD attributes = realGFAW(name);
    if (attributes == INVALID_FILE_ATTRIBUTES && wrg_path_wants(name)) {
        wchar_t resolved[MAX_PATH];
        if (wrg_redirect_resolve(name, resolved)) {
            DWORD viaMod = realGFAW(resolved);
            if (viaMod != INVALID_FILE_ATTRIBUTES) {
                wrg_log(L"ATTR-SERVE", name, resolved);
            }
            return viaMod;
        }
        wrg_log_miss(name);
    }
    return attributes;
}

static DWORD WINAPI myGFAA(LPCSTR name) {
    DWORD attributes = realGFAA(name);
    if (attributes == INVALID_FILE_ATTRIBUTES && name) {
        wchar_t wideName[MAX_PATH];
        if (MultiByteToWideChar(CP_ACP, 0, name, -1, wideName, MAX_PATH) && wrg_path_wants(wideName)) {
            wchar_t resolved[MAX_PATH];
            if (wrg_redirect_resolve(wideName, resolved)) {
                return realGFAW(resolved);
            }
        }
    }
    return attributes;
}

static BOOL WINAPI myGFAEA(LPCSTR name, GET_FILEEX_INFO_LEVELS level, LPVOID info) {
    BOOL ok = realGFAEA(name, level, info);
    if (!ok && name) {
        wchar_t wideName[MAX_PATH];
        if (MultiByteToWideChar(CP_ACP, 0, name, -1, wideName, MAX_PATH) && wrg_path_wants(wideName)) {
            wchar_t resolved[MAX_PATH];
            if (wrg_redirect_resolve(wideName, resolved)) {
                return realGFAEW(resolved, level, info);
            }
        }
    }
    return ok;
}

static BOOL WINAPI myGFAEW(LPCWSTR name, GET_FILEEX_INFO_LEVELS level, LPVOID info) {
    BOOL ok = realGFAEW(name, level, info);
    if (!ok && wrg_path_wants(name)) {
        wchar_t resolved[MAX_PATH];
        if (wrg_redirect_resolve(name, resolved)) {
            return realGFAEW(resolved, level, info);
        }
    }
    return ok;
}

void wrg_install_mapdir_hooks(void) {
    HMODULE kernel32 = GetModuleHandleA("kernel32.dll");
    realGFAW  = (GetFileAttributesW_t)  GetProcAddress(kernel32, "GetFileAttributesW");
    realGFAA  = (GetFileAttributesA_t)  GetProcAddress(kernel32, "GetFileAttributesA");
    realGFAEW = (GetFileAttributesExW_t)GetProcAddress(kernel32, "GetFileAttributesExW");
    realGFAEA = (GetFileAttributesExA_t)GetProcAddress(kernel32, "GetFileAttributesExA");
    realFFFW  = (FindFirstFileW_t)      GetProcAddress(kernel32, "FindFirstFileW");
    realFFFA  = (FindFirstFileA_t)      GetProcAddress(kernel32, "FindFirstFileA");
    realFFFEW = (FindFirstFileExW_t)    GetProcAddress(kernel32, "FindFirstFileExW");
    realFFFEA = (FindFirstFileExA_t)    GetProcAddress(kernel32, "FindFirstFileExA");
    realFNFW  = (FindNextFileW_t)       GetProcAddress(kernel32, "FindNextFileW");
    realFNFA  = (FindNextFileA_t)       GetProcAddress(kernel32, "FindNextFileA");
    realFC    = (FindClose_t)           GetProcAddress(kernel32, "FindClose");
    int foundW = wrg_hook_all("FindFirstFileW",  (void*)myFFFW);
    int foundA = wrg_hook_all("FindFirstFileA",  (void*)myFFFA);
    wrg_hook_all("GetFileAttributesW",   (void*)myGFAW);
    wrg_hook_all("GetFileAttributesA",   (void*)myGFAA);
    wrg_hook_all("GetFileAttributesExW", (void*)myGFAEW);
    wrg_hook_all("GetFileAttributesExA", (void*)myGFAEA);
    wrg_hook_all("FindFirstFileExW",     (void*)myFFFEW);
    wrg_hook_all("FindFirstFileExA",     (void*)myFFFEA);
    wrg_hook_all("FindNextFileW",        (void*)myFNFW);
    wrg_hook_all("FindNextFileA",        (void*)myFNFA);
    wrg_hook_all("FindClose",            (void*)myFC);
    wchar_t info[64];
    _snwprintf(info, 64, L"FFF W=%d A=%d", foundW, foundA);
    wrg_log(L"MAPDIR-HOOKS", info, NULL);
}
