#include <windows.h>
#include <stdio.h>
#include <wchar.h>

typedef DWORD (WINAPI *GetFileVersionInfoSizeA_t)(LPCSTR, LPDWORD);
typedef BOOL  (WINAPI *GetFileVersionInfoA_t)(LPCSTR, DWORD, DWORD, LPVOID);
typedef BOOL  (WINAPI *VerQueryValueA_t)(LPCVOID, LPCSTR, LPVOID*, PUINT);
typedef DWORD (WINAPI *GetFileVersionInfoSizeW_t)(LPCWSTR, LPDWORD);
typedef BOOL  (WINAPI *GetFileVersionInfoW_t)(LPCWSTR, DWORD, DWORD, LPVOID);
typedef BOOL  (WINAPI *VerQueryValueW_t)(LPCVOID, LPCWSTR, LPVOID*, PUINT);
typedef BOOL  (WINAPI *GetFileVersionInfoExW_t)(DWORD, LPCWSTR, DWORD, DWORD, LPVOID);
typedef DWORD (WINAPI *GetFileVersionInfoSizeExW_t)(DWORD, LPCWSTR, LPDWORD);

static HMODULE g_realVersion = NULL;

static HMODULE __LoadRealVersion(void) {
    if (g_realVersion) {
        return g_realVersion;
    }

    wchar_t systemPath[MAX_PATH];
    UINT systemLen = GetSystemDirectoryW(systemPath, MAX_PATH);
    if (systemLen > 0 && systemLen < MAX_PATH) {
        wchar_t candidate[MAX_PATH];
        _snwprintf(candidate, MAX_PATH, L"%ls\\version.dll", systemPath);
        candidate[MAX_PATH - 1] = 0;
        HMODULE loaded = LoadLibraryW(candidate);
        if (loaded) {
            g_realVersion = loaded;
            return g_realVersion;
        }
    }

    wchar_t modulePath[MAX_PATH];
    if (GetModuleFileNameW(NULL, modulePath, MAX_PATH)) {
        wchar_t *slash = wcsrchr(modulePath, L'\\');
        if (slash) {
            *slash = 0;
            wchar_t candidate[MAX_PATH];
            _snwprintf(candidate, MAX_PATH, L"%ls\\version_real.dll", modulePath);
            candidate[MAX_PATH - 1] = 0;
            HMODULE loaded = LoadLibraryW(candidate);
            if (loaded) {
                g_realVersion = loaded;
                return g_realVersion;
            }
        }
    }

    return NULL;
}

static FARPROC __RealProc(FARPROC *cache, const char *name) {
    if (*cache) {
        return *cache;
    }
    HMODULE real = __LoadRealVersion();
    if (!real) {
        return NULL;
    }
    *cache = GetProcAddress(real, name);
    return *cache;
}

extern "C" {

DWORD WINAPI GetFileVersionInfoSizeA(LPCSTR fileName, LPDWORD handle) {
    static FARPROC cache = NULL;
    FARPROC proc = __RealProc(&cache, "GetFileVersionInfoSizeA");
    if (!proc) {
        return 0;
    }
    return reinterpret_cast<GetFileVersionInfoSizeA_t>(proc)(fileName, handle);
}

BOOL WINAPI GetFileVersionInfoA(LPCSTR fileName, DWORD handle, DWORD len, LPVOID data) {
    static FARPROC cache = NULL;
    FARPROC proc = __RealProc(&cache, "GetFileVersionInfoA");
    if (!proc) {
        return FALSE;
    }
    return reinterpret_cast<GetFileVersionInfoA_t>(proc)(fileName, handle, len, data);
}

BOOL WINAPI VerQueryValueA(LPCVOID block, LPCSTR subBlock, LPVOID *buffer, PUINT len) {
    static FARPROC cache = NULL;
    FARPROC proc = __RealProc(&cache, "VerQueryValueA");
    if (!proc) {
        return FALSE;
    }
    return reinterpret_cast<VerQueryValueA_t>(proc)(block, subBlock, buffer, len);
}

DWORD WINAPI GetFileVersionInfoSizeW(LPCWSTR fileName, LPDWORD handle) {
    static FARPROC cache = NULL;
    FARPROC proc = __RealProc(&cache, "GetFileVersionInfoSizeW");
    if (!proc) {
        return 0;
    }
    return reinterpret_cast<GetFileVersionInfoSizeW_t>(proc)(fileName, handle);
}

BOOL WINAPI GetFileVersionInfoW(LPCWSTR fileName, DWORD handle, DWORD len, LPVOID data) {
    static FARPROC cache = NULL;
    FARPROC proc = __RealProc(&cache, "GetFileVersionInfoW");
    if (!proc) {
        return FALSE;
    }
    return reinterpret_cast<GetFileVersionInfoW_t>(proc)(fileName, handle, len, data);
}

BOOL WINAPI VerQueryValueW(LPCVOID block, LPCWSTR subBlock, LPVOID *buffer, PUINT len) {
    static FARPROC cache = NULL;
    FARPROC proc = __RealProc(&cache, "VerQueryValueW");
    if (!proc) {
        return FALSE;
    }
    return reinterpret_cast<VerQueryValueW_t>(proc)(block, subBlock, buffer, len);
}

BOOL WINAPI GetFileVersionInfoExW(DWORD flags, LPCWSTR fileName, DWORD handle, DWORD len, LPVOID data) {
    static FARPROC cache = NULL;
    FARPROC proc = __RealProc(&cache, "GetFileVersionInfoExW");
    if (!proc) {
        return FALSE;
    }
    return reinterpret_cast<GetFileVersionInfoExW_t>(proc)(flags, fileName, handle, len, data);
}

DWORD WINAPI GetFileVersionInfoSizeExW(DWORD flags, LPCWSTR fileName, LPDWORD handle) {
    static FARPROC cache = NULL;
    FARPROC proc = __RealProc(&cache, "GetFileVersionInfoSizeExW");
    if (!proc) {
        return 0;
    }
    return reinterpret_cast<GetFileVersionInfoSizeExW_t>(proc)(flags, fileName, handle);
}

}
