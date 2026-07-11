// edat trie walk: asset path -> byte offset (offset_data + entry.offset)
//
// header @0 "edat"; @25 offset_files u32; @29 size_files u32; @33 offset_data u32; @45 sectorSize u32
// dict @offset_files: u32 unk0 + 6x00, then radix trie. Entry @c:
//   u32 pathSize  (c -> children start for dir; 0 -> file leaf)
//   u32 entrySize (c -> next sibling; 0 -> parent end)
//   dir : CString name (even-padded), children in [c+pathSize .. next]
//   file: 32-byte header {u32 off, _4, u32 size, _4, md5[16]} then name

#include "internal.h"
#include "wp_util.h"

#define ED_OFF_FILES  25
#define ED_SIZE_FILES 29
#define ED_OFF_DATA   33

static unsigned int rd_u32(const BYTE *buf, size_t at) {
    return (unsigned int)buf[at] | ((unsigned int)buf[at+1]<<8) |
           ((unsigned int)buf[at+2]<<16) | ((unsigned int)buf[at+3]<<24);
}

// CString at pos, consumed length padded to even. Copies name into out (cap)
// Returns next position, or 0 on error
static size_t cstr_aligned2(const BYTE *dict, size_t dlen, size_t pos,
                            char *out, size_t cap) {
    size_t cursor = pos;
    while (cursor < dlen && dict[cursor] != 0) {
        cursor++;
    }
    if (cursor >= dlen) {
        return 0;
    }
    size_t nameLen = cursor - pos;
    if (out) {
        size_t n = nameLen < cap-1 ? nameLen : cap-1;
        memcpy(out, dict+pos, n);
        out[n]=0;
    }
    size_t rawLen = nameLen + 1;
    size_t paddedLen = rawLen + (rawLen & 1);
    return pos + paddedLen;
}

#define ED_MAX_DEPTH 64

// Recursive walk. prefix is the accumulated path so far
static void walk(const BYTE *dict, size_t dlen, size_t pos, size_t end,
                 const char *prefix, const char *target,
                 unsigned int *foundOffset, int *matched, int depth) {
    if (depth > ED_MAX_DEPTH) {
        return;   // cap untrusted nesting
    }
    while (pos < end && !*matched) {
        size_t entryStart = pos;
        if (pos + 8 > dlen) {
            return;
        }
        unsigned int pathSize  = rd_u32(dict, pos);
        unsigned int entrySize = rd_u32(dict, pos + 4);
        pos += 8;
        size_t next = entrySize ? entryStart + entrySize : end;
        if (entrySize != 0 && (next <= entryStart || next < pos || next > end)) {
            return;   // reject wrap/backward (untrusted entrySize)
        }

        if (pathSize != 0) {
            char name[256];
            size_t after = cstr_aligned2(dict, dlen, pos, name, sizeof(name));
            if (!after) {
                return;
            }
            char child[1024];
            _snprintf(child, sizeof(child), "%s%s", prefix, name);
            child[sizeof(child)-1] = 0;
            walk(dict, dlen, after, next, child, target, foundOffset, matched, depth + 1);
        } else {
            if (pos + 32 > dlen) {
                return;
            }
            unsigned int fileOffset = rd_u32(dict, pos);
            char name[256];
            if (!cstr_aligned2(dict, dlen, pos + 32, name, sizeof(name))) {
                return;
            }
            char full[1024];
            _snprintf(full, sizeof(full), "%s%s", prefix, name);
            full[sizeof(full)-1] = 0;
            if (_stricmp(full, target) == 0) {
                *foundOffset = fileOffset;
                *matched = 1;
                return;
            }
        }
        pos = next;
    }
}

int wrg_edat_resolve(const wchar_t *packpath, const char *inner,
                     unsigned long long *off) {
    if (!packpath || !inner || !off) {
        return 0;
    }

    // normalize target separators to backslash
    char target[1024];
    strncpy(target, inner, sizeof(target)-1);
    target[sizeof(target)-1]=0;
    for (char *sep = target; *sep; ++sep) {
        if (*sep == '/') {
            *sep = '\\';
        }
    }

    wp::HandleGuard fg(realCFW(packpath, GENERIC_READ, FILE_SHARE_READ|FILE_SHARE_WRITE,
                               NULL, OPEN_EXISTING, 0, NULL));
    if (!fg) {
        return 0;
    }
    HANDLE f = fg.get();

    BYTE head[64];
    DWORD bytesRead = 0;
    int resolved = 0;
    if (ReadFile(f, head, sizeof(head), &bytesRead, NULL) && bytesRead == sizeof(head)
        && head[0]=='e'&&head[1]=='d'&&head[2]=='a'&&head[3]=='t') {
        unsigned int offFiles  = rd_u32(head, ED_OFF_FILES);
        unsigned int sizeFiles = rd_u32(head, ED_SIZE_FILES);
        unsigned int offData   = rd_u32(head, ED_OFF_DATA);
        if (sizeFiles >= 10 && sizeFiles < (64u<<20)) {  // sane dict size cap
            BYTE *dict = static_cast<BYTE*>(malloc(sizeFiles));
            LARGE_INTEGER seekPos;
            seekPos.QuadPart = offFiles;
            if (dict && SetFilePointerEx(f, seekPos, NULL, FILE_BEGIN)
                && ReadFile(f, dict, sizeFiles, &bytesRead, NULL) && bytesRead == sizeFiles) {
                unsigned int unk0 = rd_u32(dict, 0);
                if (unk0 == 0x0A) {
                    unsigned int relativeOffset = 0;
                    int found = 0;
                    walk(dict, sizeFiles, 10, sizeFiles, "", target, &relativeOffset, &found, 0);
                    if (found) {
                        *off = (unsigned long long)offData + relativeOffset;
                        resolved = 1;
                    }
                }
            }
            free(dict);
        }
    }
    return resolved;
}
