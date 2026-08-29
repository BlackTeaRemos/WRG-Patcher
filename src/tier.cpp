#include "internal.h"
#include "wp_util.h"

typedef struct {
    wchar_t modName[128];            // the mod that owns this tier
    wchar_t parent[32];              // revision this tier layers over
    wchar_t id[32];                  // this tier's revision directory name
    wchar_t dir[MAX_PATH];           // mods\<mod>\<id>
} Tier;

static Tier g_tier[WRG_MAX_TIERS];
static int  g_ntier = 0;

static int tier_dir_of_mod(const wchar_t *modDir, wchar_t *out) {
    wchar_t pattern[MAX_PATH];
    int written = _snwprintf(pattern, MAX_PATH, L"%ls\\*", modDir);
    if (written < 0 || written >= MAX_PATH) {
        return 0;
    }
    WIN32_FIND_DATAW data;
    HANDLE handle = FindFirstFileW(pattern, &data);
    if (handle == INVALID_HANDLE_VALUE) {
        return 0;
    }
    int found = 0;
    do {
        if (!(data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
            continue;
        }
        if (wcscmp(data.cFileName, L".") == 0 || wcscmp(data.cFileName, L"..") == 0) {
            continue;
        }
        if (data.cFileName[0] == L'.') {
            continue;   // .cache and friends are not pack directories
        }
        wchar_t candidate[MAX_PATH];
        written = _snwprintf(candidate, MAX_PATH, L"%ls\\%ls\\NDF_Win.dat", modDir, data.cFileName);
        if (written < 0 || written >= MAX_PATH) {
            continue;
        }
        if (GetFileAttributesW(candidate) == INVALID_FILE_ATTRIBUTES) {
            continue;   // no pack roster here: not the tier directory
        }
        written = _snwprintf(out, MAX_PATH, L"%ls\\%ls", modDir, data.cFileName);
        if (written < 0 || written >= MAX_PATH) {
            continue;
        }
        found = 1;
        break;
    } while (FindNextFileW(handle, &data));
    FindClose(handle);
    return found;
}

// Whether a mod states that it delivers by taking a revision of its own.
static int mod_owns_tier(const wchar_t *modDir) {
    static const wchar_t *MANIFEST_NAMES[] = { L"mod.json", L"wrd_mod.json" };
    for (int nameIndex = 0; nameIndex < 2; ++nameIndex) {
        wchar_t manifestPath[MAX_PATH];
        int written = _snwprintf(manifestPath, MAX_PATH, L"%ls\\%ls",
                                 modDir, MANIFEST_NAMES[nameIndex]);
        if (written < 0 || written >= MAX_PATH) {
            continue;
        }
        char *text = wp::read_file_text(manifestPath);
        if (!text) {
            continue;
        }
        int owns = 0;
        const char *found = strstr(text, "\"delivery\"");
        if (found) {
            const char *cursor = found + strlen("\"delivery\"");
            while (*cursor == ' ' || *cursor == ':' || *cursor == '\t' || *cursor == '"') {
                cursor++;
            }
            owns = (strncmp(cursor, "append", 6) == 0);
        }
        free(text);
        if (owns) {
            return 1;
        }
    }
    return 0;
}

unsigned int wrg_mods_pin_revision(void) {
    static const wchar_t *MANIFEST_NAMES[] = { L"mod.json", L"wrd_mod.json" };
    for (int modIndex = 0; modIndex < g_nmods; ++modIndex) {
        for (int nameIndex = 0; nameIndex < 2; ++nameIndex) {
            wchar_t manifestPath[MAX_PATH];
            int written = _snwprintf(manifestPath, MAX_PATH, L"%ls\\%ls\\%ls",
                                     g_modsroot, g_mods[modIndex], MANIFEST_NAMES[nameIndex]);
            if (written < 0 || written >= MAX_PATH) {
                continue;
            }
            char *text = wp::read_file_text(manifestPath);
            if (!text) {
                continue;
            }
            unsigned int pinned = 0;
            const char *found = strstr(text, "\"pin_revision\"");
            if (found) {
                const char *cursor = found + strlen("\"pin_revision\"");
                while (*cursor == ' ' || *cursor == ':' || *cursor == '\t' || *cursor == '"') {
                    cursor++;
                }
                while (*cursor >= '0' && *cursor <= '9') {
                    pinned = pinned * 10 + (unsigned int)(*cursor - '0');
                    cursor++;
                }
            }
            free(text);
            if (pinned != 0) {
                wrg_log(L"REVISION-PIN-MOD", g_mods[modIndex], NULL);
                return pinned;
            }
        }
    }
    return 0;
}

void wrg_tier_resolve_all(unsigned int baseRevision) {
    wp::CritLock lock(g_lock);
    g_ntier = 0;
    unsigned int parent = baseRevision;
    for (int modIndex = 0; modIndex < g_nmods && g_ntier < WRG_MAX_TIERS; ++modIndex) {
        wchar_t modDir[MAX_PATH];
        int written = _snwprintf(modDir, MAX_PATH, L"%ls\\%ls", g_modsroot, g_mods[modIndex]);
        if (written < 0 || written >= MAX_PATH) {
            continue;
        }
        if (!mod_owns_tier(modDir)) {
            continue;   // ships in place; not every mod owns a revision
        }
        wrg_log(L"TIER-OWNER", g_mods[modIndex], NULL);
        Tier *tier = &g_tier[g_ntier];
        if (!tier_dir_of_mod(modDir, tier->dir)) {
            wrg_log(L"TIER-NO-PACKS", g_mods[modIndex], NULL);
            continue;
        }
        wp::copy_truncated(tier->modName, g_mods[modIndex], 128);
        _snwprintf(tier->parent, 32, L"%u", parent);
        _snwprintf(tier->id, 32, L"%u", parent + 1);
        g_ntier++;
        wchar_t detail[128];
        _snwprintf(detail, 128, L"%ls over %ls -> %ls", g_mods[modIndex], tier->parent, tier->id);
        wrg_log(L"TIER-ADD", tier->dir, detail);
        parent = parent + 1;
    }
}

int wrg_tier_count(void) {
    wp::CritLock lock(g_lock);
    return g_ntier;
}

int wrg_tier_children_of(const wchar_t *parent, const wchar_t **out, int capacity) {
    if (!parent || !out || capacity <= 0) {
        return 0;
    }
    int found = 0;
    wp::CritLock lock(g_lock);
    for (int index = 0; index < g_ntier && found < capacity; ++index) {
        if (_wcsicmp(g_tier[index].parent, parent) == 0) {
            out[found++] = g_tier[index].id;
        }
    }
    return found;
}

int wrg_tier_all(const wchar_t **out, int capacity) {
    if (!out || capacity <= 0) {
        return 0;
    }
    int found = 0;
    wp::CritLock lock(g_lock);
    for (int index = 0; index < g_ntier && found < capacity; ++index) {
        out[found++] = g_tier[index].id;
    }
    return found;
}

int wrg_tier_dir_by_id(const wchar_t *id, wchar_t *out) {
    if (!id || !out) {
        return 0;
    }
    wp::CritLock lock(g_lock);
    for (int index = 0; index < g_ntier; ++index) {
        if (_wcsicmp(g_tier[index].id, id) == 0) {
            wp::copy_truncated(out, g_tier[index].dir, MAX_PATH);
            return 1;
        }
    }
    return 0;
}

// Serve a pack the engine opens inside a tier, by that tier's revision.
int wrg_tier_resolve_open(const wchar_t *name, wchar_t *out) {
    if (!name || !out) {
        return 0;
    }
    size_t nameLen = wcslen(name);
    if (nameLen < 5) {
        return 0;
    }
    wchar_t normalized[MAX_PATH];
    wp::copy_truncated(normalized, name, MAX_PATH);
    wp::normalize_separators(normalized);
    wchar_t *leafSlash = wcsrchr(normalized, L'\\');
    if (!leafSlash) {
        return 0;
    }
    *leafSlash = 0;
    const wchar_t *leaf = leafSlash + 1;
    wchar_t *idSlash = wcsrchr(normalized, L'\\');
    const wchar_t *id = idSlash ? idSlash + 1 : normalized;

    wchar_t tierDir[MAX_PATH];
    if (!wrg_tier_dir_by_id(id, tierDir)) {
        return 0;
    }
    int written = _snwprintf(out, MAX_PATH, L"%ls\\%ls", tierDir, leaf);
    if (written < 0 || written >= MAX_PATH) {
        return 0;
    }
    out[MAX_PATH-1] = 0;
    return GetFileAttributesW(out) != INVALID_FILE_ATTRIBUTES;
}

int wrg_tier_dir_of(const wchar_t *parent, const wchar_t *id, wchar_t *out) {
    if (!parent || !id || !out) {
        return 0;
    }
    wp::CritLock lock(g_lock);
    for (int index = 0; index < g_ntier; ++index) {
        if (_wcsicmp(g_tier[index].parent, parent) == 0
            && _wcsicmp(g_tier[index].id, id) == 0) {
            wp::copy_truncated(out, g_tier[index].dir, MAX_PATH);
            return 1;
        }
    }
    return 0;
}
