#pragma once

#include "validation/JsonStringValidator.h"
#include "validation/JsonUnicodeValidator.h"
#include <cstdint>
#include <string>
#include <string_view>

class JsonStringReader {
public:
    static std::string Read(std::string_view text, std::size_t& position) {
        JsonStringValidator::ValidateStart(text, position);
        ++position;
        std::string value;
        bool terminated = false;
        while (position < text.size()) {
            unsigned char character = static_cast<unsigned char>(text[position++]);
            if (character == '"') {
                terminated = true;
                break;
            }
            JsonStringValidator::ValidateCharacter(character);
            if (character != '\\') {
                value.push_back(static_cast<char>(character));
                continue;
            }
            JsonStringValidator::ValidateEscape(text, position);
            const char escape = text[position++];
            if (escape == 'u') {
                __AppendCodepoint(JsonUnicodeValidator::ReadCodepoint(text, position), value);
            } else {
                value.push_back(JsonStringValidator::DecodeEscape(escape));
            }
        }
        JsonStringValidator::ValidateTerminated(terminated);
        return value;
    }

private:
    static void __AppendCodepoint(std::uint32_t codepoint, std::string& value) {
        if (codepoint <= 0x7F) {
            value.push_back(static_cast<char>(codepoint));
        } else if (codepoint <= 0x7FF) {
            value.push_back(static_cast<char>(0xC0 | (codepoint >> 6)));
            value.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
        } else if (codepoint <= 0xFFFF) {
            value.push_back(static_cast<char>(0xE0 | (codepoint >> 12)));
            value.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
            value.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
        } else {
            value.push_back(static_cast<char>(0xF0 | (codepoint >> 18)));
            value.push_back(static_cast<char>(0x80 | ((codepoint >> 12) & 0x3F)));
            value.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
            value.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
        }
    }
};
