#include "internal.h"
#include "wp_util.h"

static WrgPatch g_patch[WRG_MAX_PATCH];
static int      g_npatch = 0;

static struct { HANDLE handle; const wchar_t *tail; wchar_t packpath[MAX_PATH]; } g_track[WRG_MAX_TRACK];
static int g_ntrack = 0;
static int g_read_hooks_installed = 0;

// ---- patch registry -------------------------------------------------------
WrgResult wrg_splice_add(const char *tail, uint64_t off, const char *inner,
                         const void *data, uint32_t len) {
    if (!tail || !data || !len) {
        return WRG_ERR;
    }
    wp::CritLock lock(g_lock);
    if (g_npatch >= WRG_MAX_PATCH) {
        return WRG_ERR_NOMEM;
    }
    WrgPatch *patch = &g_patch[g_npatch];
    MultiByteToWideChar(CP_UTF8, 0, tail, -1, patch->tail, 160);
    wp::normalize_separators(patch->tail);
    patch->off = off;
    patch->inner[0] = 0;
    if (inner) {
        strncpy(patch->inner, inner, sizeof(patch->inner)-1);
        patch->inner[sizeof(patch->inner)-1]=0;
    }
    patch->resolved = (off != WRG_OFFSET_RESOLVE);
    patch->len  = len;
    patch->data = static_cast<BYTE*>(malloc(len));
    if (!patch->data) {
        return WRG_ERR_NOMEM;
    }
    memcpy(patch->data, data, len);
    g_npatch++;
    return WRG_OK;
}

const wchar_t *wrg_overlay_tail_for(const wchar_t *name) {
    size_t nameLen = wcslen(name);
    wp::CritLock lock(g_lock);
    for (int index = 0; index < g_npatch; ++index) {
        size_t tailLen = wcslen(g_patch[index].tail);
        if (nameLen >= tailLen) {
            const wchar_t *suffix = name + nameLen - tailLen;
            wchar_t normalized[MAX_PATH];
            wp::copy_truncated(normalized, suffix, MAX_PATH);
            wp::normalize_separators(normalized);
            if (_wcsicmp(normalized, g_patch[index].tail) == 0) {
                return g_patch[index].tail;   // points into stable static storage
            }
        }
    }
    return NULL;
}

int wrg_has_patches(void) {
    return g_npatch > 0;
}

void wrg_track_add(HANDLE handle, const wchar_t *tail) {
    wp::CritLock lock(g_lock);
    if (g_ntrack < WRG_MAX_TRACK) {
        g_track[g_ntrack].handle = handle;
        g_track[g_ntrack].tail = tail;
        g_track[g_ntrack].packpath[0] = 0;
        g_ntrack++;
    }
}

void wrg_track_set_path(HANDLE handle, const wchar_t *packpath) {
    wp::CritLock lock(g_lock);
    for (int index = 0; index < g_ntrack; ++index) {
        if (g_track[index].handle == handle) {
            wp::copy_truncated(g_track[index].packpath, packpath, MAX_PATH);
            break;
        }
    }
    for (int index = 0; index < g_npatch; ++index) {
        WrgPatch *patch = &g_patch[index];
        if (patch->resolved || !patch->inner[0]) {
            continue;
        }
        unsigned long long off;
        if (wrg_edat_resolve(packpath, patch->inner, &off)) {
            patch->off = off;
            patch->resolved = 1;
        }
    }
}

void wrg_track_remove(HANDLE handle) {
    wp::CritLock lock(g_lock);
    for (int index = 0; index < g_ntrack; ++index) {
        if (g_track[index].handle == handle) {
            g_track[index] = g_track[--g_ntrack];
            break;
        }
    }
}

const wchar_t *wrg_track_tail(HANDLE handle) {
    wp::CritLock lock(g_lock);
    const wchar_t *tail = NULL;
    for (int index = 0; index < g_ntrack; ++index) {
        if (g_track[index].handle == handle) {
            tail = g_track[index].tail;
            break;
        }
    }
    return tail;
}

int wrg_splice_apply(const wchar_t *tail, unsigned long long readOffset,
                     void *buf, size_t readLen) {
    int spliced = 0;
    wp::CritLock lock(g_lock);
    for (int index = 0; index < g_npatch; ++index) {
        WrgPatch *patch = &g_patch[index];
        if (!patch->resolved) {
            continue;
        }
        if (_wcsicmp(patch->tail, tail) != 0) {
            continue;
        }
        unsigned long long patchStart = patch->off, patchEnd = patch->off + patch->len;
        unsigned long long readStart = readOffset, readEnd = readOffset + readLen;
        if (patchEnd <= readStart || patchStart >= readEnd) {
            continue;          // no overlap
        }
        unsigned long long overlapStart = patchStart > readStart ? patchStart : readStart;
        unsigned long long overlapEnd = patchEnd < readEnd ? patchEnd : readEnd;
        size_t bufOffset = (size_t)(overlapStart - readStart);
        size_t dataOffset = (size_t)(overlapStart - patchStart);
        size_t overlapLen = (size_t)(overlapEnd - overlapStart);
        if (bufOffset + overlapLen <= readLen && dataOffset + overlapLen <= patch->len) {
            memcpy(static_cast<BYTE*>(buf) + bufOffset, patch->data + dataOffset, overlapLen);
            spliced++;
        }
    }
    return spliced;
}

typedef LONG NTSTATUS;
typedef struct { union { NTSTATUS Status; PVOID Pointer; }; ULONG_PTR Information; } WRG_IOSB;
typedef NTSTATUS (NTAPI *NtReadFile_t)(HANDLE,HANDLE,PVOID,PVOID,WRG_IOSB*,PVOID,ULONG,LARGE_INTEGER*,PULONG);
static NtReadFile_t realNtReadFile;

static int explicit_offset(LARGE_INTEGER *off, unsigned long long *out) {
    if (!off) {
        return 0;
    }
    if (off->HighPart == -1 || off->HighPart == -2) {
        return 0;  // use-position sentinel
    }
    if (off->QuadPart < 0) {
        return 0;
    }
    *out = (unsigned long long)off->QuadPart;
    return 1;
}

static NTSTATUS NTAPI myNtReadFile(HANDLE handle, HANDLE ev, PVOID apc, PVOID apcctx,
    WRG_IOSB *iosb, PVOID buf, ULONG len, LARGE_INTEGER *off, PULONG key) {
    const wchar_t *tail = wrg_track_tail(handle);
    unsigned long long readOffset = 0;
    int haveOffset = tail ? explicit_offset(off, &readOffset) : 0;
    NTSTATUS status = realNtReadFile(handle, ev, apc, apcctx, iosb, buf, len, off, key);
    if (tail && haveOffset && status == 0 && iosb && buf) {
        unsigned long long readLen = (unsigned long long)iosb->Information;
        if (readLen > len) {
            readLen = len;
        }
        wrg_splice_apply(tail, readOffset, buf, (size_t)readLen);
    }
    return status;
}

static BOOL WINAPI myReadFile(HANDLE handle, LPVOID buf, DWORD len, LPDWORD got, LPOVERLAPPED ov) {
    const wchar_t *tail = wrg_track_tail(handle);
    unsigned long long readOffset = 0;
    int haveOffset = 0;
    BOOL ok = realReadFile(handle, buf, len, got, ov);
    if (tail && ok && got) {
        if (ov) {
            readOffset = ((unsigned long long)ov->OffsetHigh << 32) | ov->Offset;
            haveOffset = 1;
        }
        else {
            LARGE_INTEGER zeroOffset;
            zeroOffset.QuadPart = 0;
            LARGE_INTEGER after;
            if (SetFilePointerEx(handle, zeroOffset, &after, FILE_CURRENT)) {
                readOffset = (unsigned long long)after.QuadPart - *got;
                haveOffset = 1;
            }
        }
    }
    if (tail && haveOffset && ok && got && buf) {
        wrg_splice_apply(tail, readOffset, buf, (size_t)*got);
    }
    return ok;
}

void wrg_install_read_hooks(void) {
    if (g_read_hooks_installed || g_npatch == 0) {
        return;
    }
    HMODULE ntdll = GetModuleHandleA("ntdll.dll");
    realNtReadFile = (NtReadFile_t)GetProcAddress(ntdll, "NtReadFile");
    wrg_hook_all("NtReadFile", (void*)myNtReadFile);
    wrg_hook_all("ReadFile",   (void*)myReadFile);
    g_read_hooks_installed = 1;
}
