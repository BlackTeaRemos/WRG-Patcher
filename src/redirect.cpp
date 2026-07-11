#include "internal.h"
#include "wp_util.h"

typedef struct {
    wchar_t tail[160];
    wchar_t real[MAX_PATH];
    int prio;
} Redir;
static Redir g_redir[WRG_MAX_REDIR];
static int   g_nredir = 0;

int wrg_path_wants(const wchar_t *path) {
    if (!path) {
        return 0;
    }
    size_t pathLen = wcslen(path);
    if (pathLen < 4) {
        return 0;
    }
    const wchar_t *suffix = path + pathLen - 4;
    return _wcsicmp(suffix, L".dat") == 0 || _wcsicmp(suffix, L".spk") == 0;
}

WrgResult wrg_redirect_add(const char *tail, const char *realpath) {
    if (!tail || !realpath) {
        return WRG_ERR;
    }
    wp::CritLock lock(g_lock);
    if (g_nredir >= WRG_MAX_REDIR) {
        return WRG_ERR_NOMEM;
    }
    Redir *entry = &g_redir[g_nredir++];
    MultiByteToWideChar(CP_UTF8, 0, tail, -1, entry->tail, 160);
    MultiByteToWideChar(CP_UTF8, 0, realpath, -1, entry->real, MAX_PATH);
    wp::normalize_separators(entry->tail);
    entry->prio = g_nredir;   // later add -> higher priority
    return WRG_OK;
}

static int suffix_match(const wchar_t *name, const wchar_t *tail) {
    size_t nameLen = wcslen(name), tailLen = wcslen(tail);
    if (nameLen < tailLen) {
        return 0;
    }
    const wchar_t *suffix = name + nameLen - tailLen;
    wchar_t normalized[MAX_PATH];
    wp::copy_truncated(normalized, suffix, MAX_PATH);
    wp::normalize_separators(normalized);
    return _wcsicmp(normalized, tail) == 0;
}

int wrg_redirect_resolve(const wchar_t *name, wchar_t *out) {
    int best = -1, bestPriority = -1;
    {
        wp::CritLock lock(g_lock);
        for (int index = 0; index < g_nredir; ++index) {
            if (suffix_match(name, g_redir[index].tail) && g_redir[index].prio >= bestPriority) {
                best = index;
                bestPriority = g_redir[index].prio;
            }
        }
        if (best >= 0) {
            wp::copy_truncated(out, g_redir[best].real, MAX_PATH);
        }
    }
    if (best >= 0) {
        return 1;
    }

    const wchar_t *starts[64]; int segmentCount = 0;
    starts[segmentCount++] = name;
    for (const wchar_t *cursor = name; *cursor && segmentCount < 64; ++cursor) {
        if (*cursor == L'\\' || *cursor == L'/') {
            starts[segmentCount++] = cursor + 1;
        }
    }
    int maxDepth = segmentCount < 6 ? segmentCount : 6;
    int found = 0;
    for (int modIndex = 0; modIndex < g_nmods; ++modIndex) {
        for (int depth = maxDepth; depth >= 1; --depth) {
            const wchar_t *tail = starts[segmentCount - depth];
            wchar_t candidatePath[MAX_PATH];
            int written = _snwprintf(candidatePath, MAX_PATH, L"%ls\\%ls\\%ls", g_modsroot, g_mods[modIndex], tail);
            if (written < 0 || written >= MAX_PATH) {
                continue;   // truncated: unterminated buffer, never a real file
            }
            candidatePath[MAX_PATH-1] = 0;
            wp::normalize_separators(candidatePath);
            if (GetFileAttributesW(candidatePath) != INVALID_FILE_ATTRIBUTES) {
                wp::copy_truncated(out, candidatePath, MAX_PATH);
                found = 1;
                break;
            }
        }
    }
    return found;
}
