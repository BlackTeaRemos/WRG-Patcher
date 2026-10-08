#pragma once

#include <cstddef>
#include <stdexcept>
#include <string_view>

class JsonNumberValidator {
public:
    static void Validate(std::string_view text, std::size_t& position) {
        if (position < text.size() && text[position] == '-') {
            ++position;
        }
        if (position < text.size() && text[position] == '0') {
            ++position;
        } else {
            __ReadDigits(text, position);
        }
        if (position < text.size() && text[position] == '.') {
            ++position;
            __ReadDigits(text, position);
        }
        if (position < text.size() && (text[position] == 'e' || text[position] == 'E')) {
            ++position;
            if (position < text.size() && (text[position] == '+' || text[position] == '-')) {
                ++position;
            }
            __ReadDigits(text, position);
        }
    }

private:
    static void __ReadDigits(std::string_view text, std::size_t& position) {
        std::size_t beginning = position;
        while (position < text.size() && text[position] >= '0' && text[position] <= '9') {
            ++position;
        }
        if (position == beginning) {
            throw std::runtime_error("Invalid JSON number");
        }
    }
};
