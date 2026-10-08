#pragma once

#include <cstddef>
#include <stdexcept>
#include <string_view>

class JsonStringValidator {
public:
    static void ValidateStart(std::string_view text, std::size_t position) {
        if (position >= text.size() || text[position] != '"') {
            throw std::runtime_error("Expected a JSON string");
        }
    }

    static void ValidateCharacter(unsigned char character) {
        if (character < 32) {
            throw std::runtime_error("JSON strings cannot contain unescaped control characters");
        }
    }

    static void ValidateEscape(std::string_view text, std::size_t position) {
        if (position >= text.size()) {
            throw std::runtime_error("Incomplete JSON string escape");
        }
    }

    static char DecodeEscape(char escape) {
        switch (escape) {
            case '"':
            case '\\':
            case '/':
                return escape;
            case 'b':
                return '\b';
            case 'f':
                return '\f';
            case 'n':
                return '\n';
            case 'r':
                return '\r';
            case 't':
                return '\t';
            default:
                throw std::runtime_error("Unsupported JSON string escape");
        }
    }

    static void ValidateTerminated(bool terminated) {
        if (!terminated) {
            throw std::runtime_error("Unterminated JSON string");
        }
    }
};
