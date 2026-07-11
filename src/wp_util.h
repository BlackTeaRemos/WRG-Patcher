#ifndef WRG_WP_UTIL_H
#define WRG_WP_UTIL_H

#include <windows.h>
#include <cstdint>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cwchar>
#include <expected>
#include <span>
#include <string_view>
#include <utility>

namespace wp {

enum class Error {
    Fail,
    BadArg,
    NoMem,
    Range,
    NotFound,
};

template <class T>
using Result = std::expected<T, Error>;

// restores original page protection on scope exit
class VProtectGuard {
public:
    VProtectGuard(void* addr, std::size_t size, DWORD newProt) noexcept
        : addr_(addr), size_(size) {
        succeeded_ = ::VirtualProtect(addr_, size_, newProt, &oldProtect_) != FALSE;
    }
    ~VProtectGuard() noexcept {
        if (succeeded_) {
            DWORD tmp;
            ::VirtualProtect(addr_, size_, oldProtect_, &tmp);
        }
    }
    VProtectGuard(const VProtectGuard&) = delete;
    VProtectGuard& operator=(const VProtectGuard&) = delete;

    [[nodiscard]] bool ok() const noexcept {
        return succeeded_;
    }
    explicit operator bool() const noexcept {
        return succeeded_;
    }

private:
    void*       addr_;
    std::size_t size_;
    DWORD       oldProtect_ = 0;
    bool        succeeded_  = false;
};

// closes HANDLE on scope exit; host-internal file reads only, not engine-owned handles
class HandleGuard {
public:
    HandleGuard() noexcept = default;
    explicit HandleGuard(HANDLE handle) noexcept : handle_(handle) {}
    ~HandleGuard() noexcept {
        reset();
    }

    HandleGuard(HandleGuard&& other) noexcept : handle_(std::exchange(other.handle_, INVALID_HANDLE_VALUE)) {}
    HandleGuard& operator=(HandleGuard&& other) noexcept {
        if (this != &other) {
            reset();
            handle_ = std::exchange(other.handle_, INVALID_HANDLE_VALUE);
        }
        return *this;
    }
    HandleGuard(const HandleGuard&) = delete;
    HandleGuard& operator=(const HandleGuard&) = delete;

    [[nodiscard]] HANDLE get() const noexcept {
        return handle_;
    }
    [[nodiscard]] bool valid() const noexcept {
        return handle_ != INVALID_HANDLE_VALUE && handle_ != nullptr;
    }
    explicit operator bool() const noexcept {
        return valid();
    }

    void reset() noexcept {
        if (valid()) {
            ::CloseHandle(handle_);
        }
        handle_ = INVALID_HANDLE_VALUE;
    }

private:
    HANDLE handle_ = INVALID_HANDLE_VALUE;
};

class CritLock {
public:
    explicit CritLock(CRITICAL_SECTION& criticalSection) noexcept : criticalSection_(&criticalSection) {
        ::EnterCriticalSection(criticalSection_);
    }
    ~CritLock() noexcept {
        ::LeaveCriticalSection(criticalSection_);
    }
    CritLock(const CritLock&) = delete;
    CritLock& operator=(const CritLock&) = delete;

private:
    CRITICAL_SECTION* criticalSection_;
};

// in-place forward-slash -> backslash normalization, null-terminated wchar_t buffer
inline void normalize_separators(wchar_t* path) noexcept {
    for (wchar_t* sep = path; *sep; ++sep) {
        if (*sep == L'/') {
            *sep = L'\\';
        }
    }
}

// copies src into dst (capacity wchar_t's), always null-terminating within capacity
inline void copy_truncated(wchar_t* dst, const wchar_t* src, std::size_t capacity) noexcept {
    wcsncpy(dst, src, capacity);
    dst[capacity - 1] = 0;
}

inline char* read_file_text(const wchar_t* path) noexcept {
    FILE* file = _wfopen(path, L"rb");
    if (!file) {
        return nullptr;
    }
    fseek(file, 0, SEEK_END);
    long fileSize = ftell(file);
    fseek(file, 0, SEEK_SET);
    if (fileSize <= 0) {
        fclose(file);
        return nullptr;
    }
    char* fileBytes = static_cast<char*>(malloc(static_cast<std::size_t>(fileSize) + 1));
    if (!fileBytes) {
        fclose(file);
        return nullptr;
    }
    std::size_t bytesRead = fread(fileBytes, 1, static_cast<std::size_t>(fileSize), file);
    fileBytes[bytesRead] = 0;
    fclose(file);
    return fileBytes;
}

inline bool patch_bytes(void* destination, std::span<const std::uint8_t> source) noexcept {
    VProtectGuard guard(destination, source.size(), PAGE_EXECUTE_READWRITE);
    if (!guard) {
        return false;
    }
    std::memcpy(destination, source.data(), source.size());
    ::FlushInstructionCache(::GetCurrentProcess(), destination, source.size());
    return true;
}

}

#endif
