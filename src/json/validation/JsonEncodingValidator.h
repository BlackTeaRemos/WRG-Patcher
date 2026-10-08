#pragma once

#include <windows.h>
#include <climits>
#include <stdexcept>
#include <string_view>

class JsonEncodingValidator {
public:
    static void Validate(std::string_view text) {
        if (text.empty()) {
            return;
        }
        if (text.size() > INT_MAX || !MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
            text.data(), static_cast<int>(text.size()), nullptr, 0)) {
            throw std::runtime_error("Invalid UTF-8 in JSON");
        }
    }
};
