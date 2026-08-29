#include "internal.h"
#include "wp_util.h"

// The build stamp
static const char SVN_MARKER[] = "####SVNREVISION####";
static const size_t SVN_MARKER_LEN = sizeof(SVN_MARKER) - 1;
static const size_t SVN_DIGITS = 10;

static int g_revisionPatched = 0;
static unsigned int g_revision = 0;
static unsigned int g_baseRevision = 0;

// The image's mapped extent, so the scan stays inside it.
static int image_extent(BYTE **baseOut, size_t *sizeOut) {
    HMODULE base = GetModuleHandleW(NULL);
    if (!base) {
        return 0;
    }
    BYTE *bytes = reinterpret_cast<BYTE*>(base);
    IMAGE_DOS_HEADER *dos = reinterpret_cast<IMAGE_DOS_HEADER*>(bytes);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) {
        return 0;
    }
    IMAGE_NT_HEADERS *nt = reinterpret_cast<IMAGE_NT_HEADERS*>(bytes + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) {
        return 0;
    }
    *baseOut = bytes;
    *sizeOut = nt->OptionalHeader.SizeOfImage;
    return 1;
}

static char *find_revision_field(void) {
    BYTE *base = NULL;
    size_t size = 0;
    if (!image_extent(&base, &size)) {
        return NULL;
    }
    if (size < SVN_MARKER_LEN + SVN_DIGITS) {
        return NULL;
    }
    size_t last = size - (SVN_MARKER_LEN + SVN_DIGITS);
    for (size_t offset = 0; offset < last; ++offset) {
        if (base[offset] != '#') {
            continue;
        }
        if (memcmp(base + offset, SVN_MARKER, SVN_MARKER_LEN) != 0) {
            continue;
        }
        char *digits = reinterpret_cast<char*>(base + offset + SVN_MARKER_LEN);
        int allDigits = 1;
        for (size_t index = 0; index < SVN_DIGITS; ++index) {
            if (digits[index] < '0' || digits[index] > '9') {
                allDigits = 0;
                break;
            }
        }
        if (allDigits) {
            return digits;
        }
    }
    return NULL;
}

// The revision the exe states it was built as.
static unsigned int stamped_revision(const char *field) {
    unsigned int stamped = 0;
    for (size_t index = 0; index < SVN_DIGITS; ++index) {
        stamped = stamped * 10 + (unsigned int)(field[index] - '0');
    }
    return stamped;
}

void wrg_revision_read_base(void) {
    char *field = find_revision_field();
    if (!field) {
        wrg_log(L"REVISION-NO-STAMP", g_modsroot, NULL);
        return;
    }
    g_baseRevision = stamped_revision(field);
}

void wrg_revision_init(void) {
    char *field = find_revision_field();
    if (!field) {
        return;   // already reported by wrg_revision_read_base
    }
    if (g_baseRevision == 0) {
        g_baseRevision = stamped_revision(field);
    }

    unsigned int pinned = wrg_mods_pin_revision();

    int tierCount = wrg_tier_count();
    if (pinned == 0 && tierCount <= 0) {
        return;
    }

    unsigned int top = pinned != 0
        ? pinned
        : g_baseRevision + (unsigned int)tierCount;
    char replacement[SVN_DIGITS + 1];
    _snprintf(replacement, sizeof(replacement), "%010u", top);
    if (strlen(replacement) != SVN_DIGITS) {
        wrg_log(L"REVISION-TOO-WIDE", g_modsroot, NULL);
        return;
    }
    DWORD oldProtect = 0;
    if (!VirtualProtect(field, SVN_DIGITS, PAGE_READWRITE, &oldProtect)) {
        wrg_log(L"REVISION-FAIL", g_modsroot, L"VirtualProtect denied");
        return;
    }
    memcpy(field, replacement, SVN_DIGITS);
    VirtualProtect(field, SVN_DIGITS, oldProtect, &oldProtect);
    g_revisionPatched = 1;
    g_revision = top;
    wchar_t detail[128];
    if (pinned != 0) {
        _snwprintf(detail, 128, L"base=%u pinned -> %u", g_baseRevision, top);
        wrg_log(L"REVISION-PIN", g_modsroot, detail);
    } else {
        _snwprintf(detail, 128, L"base=%u tiers=%d -> %u", g_baseRevision, tierCount, top);
        wrg_log(L"REVISION-BUMP", g_modsroot, detail);
    }
}

unsigned int wrg_revision_declared(void) {
    return g_revisionPatched ? g_revision : 0;
}

unsigned int wrg_revision_base(void) {
    return g_baseRevision;
}
