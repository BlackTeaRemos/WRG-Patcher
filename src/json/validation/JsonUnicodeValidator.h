#pragma once

#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string_view>

class JsonUnicodeValidator {
public:
    static std::uint32_t ReadCodepoint(std::string_view text, std::size_t& position) {
        std::uint32_t codepoint = __ReadHex(text, position);
        if (codepoint >= 0xD800 && codepoint <= 0xDBFF) {
            if (text.size() - position < 6 || text[position] != '\\' || text[position + 1] != 'u') {
                throw std::runtime_error("JSON Unicode surrogate requires a paired escape");
            }
            position += 2;
            std::uint32_t lowSurrogate = __ReadHex(text, position);
            if (lowSurrogate < 0xDC00 || lowSurrogate > 0xDFFF) {
                throw std::runtime_error("Invalid JSON Unicode surrogate pair");
            }
            codepoint = 0x10000 + ((codepoint - 0xD800) << 10) + lowSurrogate - 0xDC00;
        } else if (codepoint >= 0xDC00 && codepoint <= 0xDFFF) {
            throw std::runtime_error("Unpaired JSON Unicode surrogate");
        }
        return codepoint;
    }

private:
    static std::uint32_t __ReadHex(std::string_view text, std::size_t& position) {
        if (position > text.size() || text.size() - position < 4) {
            throw std::runtime_error("Incomplete JSON Unicode escape");
        }
        std::uint32_t value = 0;
        for (unsigned int digitPosition = 0; digitPosition < 4; ++digitPosition) {
            unsigned char character = static_cast<unsigned char>(text[position++]);
            value <<= 4;
            if (character >= '0' && character <= '9') {
                value += character - '0';
            } else if (character >= 'a' && character <= 'f') {
                value += character - 'a' + 10;
            } else if (character >= 'A' && character <= 'F') {
                value += character - 'A' + 10;
            } else {
                throw std::runtime_error("Invalid JSON Unicode escape");
            }
        }
        return value;
    }
};
