// IAT import-hook engine + LoadLibrary re-hook

#include "internal.h"
#include "wp_util.h"
#include <tlhelp32.h>

// installed hooks, replayed on LoadLibrary
typedef struct { const char *func; void *repl; } HookRec;
static HookRec g_hooks[16];
static int     g_nhooks = 0;

static int excluded(HMODULE mod) {
    char name[MAX_PATH];
    if (!GetModuleFileNameA(mod, name, MAX_PATH)) {
        return 1;
    }
    const char *baseName = strrchr(name, '\\');
    baseName = baseName ? baseName + 1 : name;
    if (_strnicmp(baseName, "api-ms-win", 10) == 0) {
        return 1;
    }
    // skip CreateFileW implementers: kernel32->apiset->kernelbase import chain -> infinite recursion if patched
    static const char *sys[] = {"kernel32.dll","kernelbase.dll","ntdll.dll",
        "version.dll","sechost.dll","kernel.appcore.dll", NULL};
    for (int index = 0; sys[index]; index++) {
        if (_stricmp(baseName, sys[index]) == 0) {
            return 1;
        }
    }
    return 0;
}

static int hook_module(HMODULE mod, const char *func, void *repl) {
    if (excluded(mod)) {
        return 0;
    }
    auto *base = reinterpret_cast<BYTE*>(mod);
    auto *dos = reinterpret_cast<IMAGE_DOS_HEADER*>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) {
        return 0;
    }
    auto *nt = reinterpret_cast<IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
    DWORD impRVA = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress;
    if (!impRVA) {
        return 0;
    }
    int patchedCount = 0;
    auto *imp = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(base + impRVA);
    for (; imp->Name; imp++) {
        if (!imp->OriginalFirstThunk) {
            continue;
        }
        auto *origThunk = reinterpret_cast<IMAGE_THUNK_DATA*>(base + imp->OriginalFirstThunk);
        auto *firstThunk  = reinterpret_cast<IMAGE_THUNK_DATA*>(base + imp->FirstThunk);
        for (; origThunk->u1.AddressOfData; origThunk++, firstThunk++) {
            if (origThunk->u1.Ordinal & IMAGE_ORDINAL_FLAG32) {
                continue;
            }
            auto *importByName = reinterpret_cast<IMAGE_IMPORT_BY_NAME*>(base + origThunk->u1.AddressOfData);
            if (strcmp(reinterpret_cast<char*>(importByName->Name), func) != 0) {
                continue;
            }
            if (reinterpret_cast<void*>(firstThunk->u1.Function) == repl) {
                continue;   // already ours
            }
            wp::VProtectGuard guard(&firstThunk->u1.Function, sizeof(void*), PAGE_READWRITE);  // restores protection on scope exit
            if (!guard) {
                continue;
            }
            firstThunk->u1.Function = reinterpret_cast<DWORD_PTR>(repl);
            patchedCount++;
        }
    }
    return patchedCount;
}

typedef BOOL (WINAPI *EnumProcessModules_t)(HANDLE, HMODULE*, DWORD, LPDWORD);

static int hook_every_module(const char *func, void *repl) {
    HMODULE k32 = GetModuleHandleA("kernel32.dll");
    EnumProcessModules_t epm =
        (EnumProcessModules_t)GetProcAddress(k32, "K32EnumProcessModules");
    if (!epm) {
        return hook_module(GetModuleHandleW(NULL), func, repl);
    }
    HMODULE mods[512];
    DWORD need = 0;
    if (!epm(GetCurrentProcess(), mods, sizeof(mods), &need)) {
        return 0;
    }
    int count = (int)(need / sizeof(HMODULE));
    if (count > 512) {
        count = 512;
    }
    int total = 0;
    for (int index = 0; index < count; index++) {
        total += hook_module(mods[index], func, repl);
    }
    return total;
}

int wrg_hook_all(const char *func, void *repl) {
    int known = 0;
    for (int index = 0; index < g_nhooks; index++) {
        if (strcmp(g_hooks[index].func, func) == 0) {
            g_hooks[index].repl = repl;
            known = 1;
            break;
        }
    }
    if (!known && g_nhooks < 16) {
        g_hooks[g_nhooks].func = func;
        g_hooks[g_nhooks].repl = repl;
        g_nhooks++;
    }
    return hook_every_module(func, repl);
}

void wrg_rehook_known(void) {
    wp::CritLock lock(g_lock);   // serialize concurrent IAT-page toggles
    for (int index = 0; index < g_nhooks; index++) {
        hook_every_module(g_hooks[index].func, g_hooks[index].repl);
    }
}

// LoadLibrary re-hook: new module's IAT is unpatched, re-run all known hooks
typedef HMODULE (WINAPI *LoadLibraryW_t)(LPCWSTR);
typedef HMODULE (WINAPI *LoadLibraryA_t)(LPCSTR);
typedef HMODULE (WINAPI *LoadLibraryExW_t)(LPCWSTR, HANDLE, DWORD);
typedef HMODULE (WINAPI *LoadLibraryExA_t)(LPCSTR, HANDLE, DWORD);
static LoadLibraryW_t   realLLW;
static LoadLibraryA_t   realLLA;
static LoadLibraryExW_t realLLEW;
static LoadLibraryExA_t realLLEA;

static HMODULE WINAPI myLLW(LPCWSTR libraryName) {
    HMODULE handle = realLLW(libraryName);
    wrg_rehook_known();
    return handle;
}

static HMODULE WINAPI myLLA(LPCSTR libraryName) {
    HMODULE handle = realLLA(libraryName);
    wrg_rehook_known();
    return handle;
}

static HMODULE WINAPI myLLEW(LPCWSTR libraryName, HANDLE fileHandle, DWORD flags) {
    HMODULE handle = realLLEW(libraryName,fileHandle,flags);
    wrg_rehook_known();
    return handle;
}

static HMODULE WINAPI myLLEA(LPCSTR libraryName, HANDLE fileHandle, DWORD flags) {
    HMODULE handle = realLLEA(libraryName,fileHandle,flags);
    wrg_rehook_known();
    return handle;
}

void wrg_install_loadlib_rehook(void) {
    HMODULE k32 = GetModuleHandleA("kernel32.dll");
    realLLW  = (LoadLibraryW_t)  GetProcAddress(k32, "LoadLibraryW");
    realLLA  = (LoadLibraryA_t)  GetProcAddress(k32, "LoadLibraryA");
    realLLEW = (LoadLibraryExW_t)GetProcAddress(k32, "LoadLibraryExW");
    realLLEA = (LoadLibraryExA_t)GetProcAddress(k32, "LoadLibraryExA");
    wrg_hook_all("LoadLibraryW",  (void*)myLLW);
    wrg_hook_all("LoadLibraryA",  (void*)myLLA);
    wrg_hook_all("LoadLibraryExW",(void*)myLLEW);
    wrg_hook_all("LoadLibraryExA",(void*)myLLEA);
}
