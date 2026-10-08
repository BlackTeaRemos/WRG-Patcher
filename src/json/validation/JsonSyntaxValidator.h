#pragma once

#include <cstddef>
#include <stdexcept>
#include <string_view>

class JsonSyntaxValidator {
public:
    static void ValidateToken(bool matched) {
        if (!matched) {
            throw std::runtime_error("Unexpected JSON token");
        }
    }

    static void ValidateDepth(unsigned int depth) {
        if (depth >= MAX_DEPTH) {
            throw std::runtime_error("JSON exceeds the supported nesting depth");
        }
    }

    static void ValidateValue(std::string_view text, std::size_t position) {
        if (position >= text.size()) {
            throw std::runtime_error("Missing JSON value");
        }
    }

    static void ValidateComplete(std::string_view text, std::size_t position) {
        if (position != text.size()) {
            throw std::runtime_error("JSON contains trailing data");
        }
    }

    static void ValidateLiteral(std::string_view text, std::size_t position, std::string_view literal) {
        if (text.substr(position, literal.size()) != literal) {
            throw std::runtime_error("Invalid JSON literal");
        }
    }

private:
    static constexpr unsigned int MAX_DEPTH = 64;
};
