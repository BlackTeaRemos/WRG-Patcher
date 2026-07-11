#include "internal.h"
#include "wp_util.h"

static void load_one(const wchar_t *path) {
    wp::HandleGuard fg(realCFW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL));
    if (!fg) {
        return;
    }
    DWORD fileSize = GetFileSize(fg.get(), NULL), bytesRead = 0;
    BYTE *fileBytes = static_cast<BYTE*>(malloc(fileSize));
    if (fileBytes && ReadFile(fg.get(), fileBytes, fileSize, &bytesRead, NULL) && bytesRead == fileSize) {
        unsigned int cursor = 0;
        if (cursor + 4 <= fileSize) {
            unsigned int patchCount = *reinterpret_cast<unsigned int*>(fileBytes + cursor);
            cursor += 4;
            for (unsigned int patchIndex = 0; patchIndex < patchCount; ++patchIndex) {
                if (cursor + 4 > fileSize) {
                    break;
                }
                unsigned int tailLen = *reinterpret_cast<unsigned int*>(fileBytes + cursor);
                cursor += 4;
                if (cursor + tailLen + 12 > fileSize || tailLen >= 160) {
                    break;
                }
                char tailBuf[160];
                memcpy(tailBuf, fileBytes + cursor, tailLen);
                tailBuf[tailLen] = 0;
                cursor += tailLen;
                unsigned long long offset = *reinterpret_cast<unsigned long long*>(fileBytes + cursor);
                cursor += 8;
                unsigned int dataLen = *reinterpret_cast<unsigned int*>(fileBytes + cursor);
                cursor += 4;
                if (cursor + dataLen > fileSize) {
                    break;
                }
                wrg_splice_add(tailBuf, offset, NULL, fileBytes + cursor, dataLen);
                cursor += dataLen;
            }
        }
    }
    free(fileBytes);
}

void wrg_overlay_load_from_mods(void) {
    for (int modIndex = 0; modIndex < g_nmods; ++modIndex) {
        wchar_t searchPattern[MAX_PATH];
        _snwprintf(searchPattern, MAX_PATH, L"%ls\\%ls\\overlays\\*.ovl", g_modsroot, g_mods[modIndex]);
        WIN32_FIND_DATAW findData;
        HANDLE findHandle = FindFirstFileW(searchPattern, &findData);
        if (findHandle == INVALID_HANDLE_VALUE) {
            continue;
        }
        do {
            wchar_t path[MAX_PATH];
            _snwprintf(path, MAX_PATH, L"%ls\\%ls\\overlays\\%ls",
                       g_modsroot, g_mods[modIndex], findData.cFileName);
            load_one(path);
        } while (FindNextFileW(findHandle, &findData));
        FindClose(findHandle);
    }
}
