#include "internal.h"
#include "wp_util.h"


static const uintptr_t WRG_IDA_BASE = 0x400000;

static const uintptr_t WRG_VERSION_GETTER_RVA = 0x50E500 - WRG_IDA_BASE;

static int read_anchor_config(unsigned int *versionOut, uintptr_t *rvaOut) {
    wchar_t configPath[MAX_PATH];
    _snwprintf(configPath, MAX_PATH, L"%ls\\version_anchor.txt", g_modsroot);
    char *buf = wp::read_file_text(configPath);
    if (!buf) {
        return 0;
    }
    char *save = NULL;
    char *versionLine = strtok_s(buf, "\r\n", &save);
    char *rvaLine = versionLine ? strtok_s(NULL, "\r\n", &save) : NULL;
    unsigned int version = versionLine ? (unsigned int)strtoul(versionLine, NULL, 0) : 0;
    unsigned int rva = rvaLine ? (unsigned int)strtoul(rvaLine, NULL, 16) : 0;
    free(buf);
    if (version == 0) {
        return 0;   // no usable version -> stay opt-out
    }
    *versionOut = version;
    *rvaOut = rva ? (uintptr_t)rva : WRG_VERSION_GETTER_RVA;
    return 1;
}

void wrg_version_anchor_init(void) {
    unsigned int version = 0;
    uintptr_t rva = 0;
    if (!read_anchor_config(&version, &rva)) {
        return;   // opt-in only
    }
    HMODULE base = GetModuleHandleW(NULL);
    if (!base) {
        return;
    }
    BYTE *target = reinterpret_cast<BYTE*>(base) + rva;
    // mov eax, imm32 ; ret
    BYTE detour[6];
    detour[0] = 0xB8;
    memcpy(detour + 1, &version, 4);
    detour[5] = 0xC3;
    DWORD oldProtect = 0;
    if (!VirtualProtect(target, sizeof(detour), PAGE_EXECUTE_READWRITE, &oldProtect)) {
        wrg_log(L"VERSION-ANCHOR-FAIL", g_modsroot, L"VirtualProtect denied");
        return;
    }
    memcpy(target, detour, sizeof(detour));
    VirtualProtect(target, sizeof(detour), oldProtect, &oldProtect);
    FlushInstructionCache(GetCurrentProcess(), target, sizeof(detour));
    wchar_t info[128];
    _snwprintf(info, 128, L"version=%u rva=0x%p base=0x%p", version, (void*)rva, (void*)base);
    wrg_log(L"VERSION-ANCHOR", g_modsroot, info);
}
