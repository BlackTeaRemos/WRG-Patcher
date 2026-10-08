#pragma once

#include <windows.h>
#include <ios>
#include <stdexcept>
#include <string>

class NationManifestFileValidator {
public:
    static bool ValidatePath(const std::wstring& directory, const std::wstring& path) {
        DWORD attributes = GetFileAttributesW(path.c_str());
        if (attributes == INVALID_FILE_ATTRIBUTES) {
            DWORD error = GetLastError();
            if (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND) {
                return false;
            }
            throw std::runtime_error("Cannot inspect mod.json");
        }
        DWORD directoryAttributes = GetFileAttributesW(directory.c_str());
        if ((attributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT))
            || directoryAttributes == INVALID_FILE_ATTRIBUTES
            || (directoryAttributes & FILE_ATTRIBUTE_REPARSE_POINT)) {
            throw std::runtime_error("Invalid mod.json file or directory");
        }
        return true;
    }

    static void ValidateSize(bool readable, std::streamoff length) {
        if (!readable || length <= 0 || length > MAX_MANIFEST_BYTES) {
            throw std::runtime_error("mod.json unreadable, empty, or over 16 MiB");
        }
    }

    static void ValidateRead(bool complete) {
        if (!complete) {
            throw std::runtime_error("Cannot read mod.json");
        }
    }

private:
    static constexpr std::streamoff MAX_MANIFEST_BYTES = 16 * 1024 * 1024;
};
