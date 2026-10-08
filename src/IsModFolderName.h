#pragma once
#include <stddef.h>
#include <wchar.h>

inline bool IsModFolderName(const wchar_t* name) {
    if (!name || !name[0]) {
        return false;
    }
    size_t length = wcslen(name);
    if (length >= 128 || name[length - 1] == L'.' || name[length - 1] == L' '
        || wcscmp(name, L".") == 0 || wcscmp(name, L"..") == 0) {
        return false;
    }
    for (const wchar_t* character = name; *character; ++character) {
        if (*character < 32 || wcschr(L"\\/:*?\"<>|", *character)) {
            return false;
        }
    }
    return true;
}
