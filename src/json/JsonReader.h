#pragma once

#include "JsonStringReader.h"
#include "validation/JsonEncodingValidator.h"
#include "validation/JsonNumberValidator.h"
#include "validation/JsonSyntaxValidator.h"

class JsonReader {
public:
    explicit JsonReader(std::string_view text) : _text(text) {
        JsonEncodingValidator::Validate(text);
        if (_text.starts_with("\xEF\xBB\xBF")) {
            _position = 3;
        }
    }

    bool Consume(char expected) {
        __SkipWhitespace();
        if (_position < _text.size() && _text[_position] == expected) {
            ++_position;
            return true;
        }
        return false;
    }

    void Expect(char expected) {
        JsonSyntaxValidator::ValidateToken(Consume(expected));
    }

    std::string ReadString() {
        __SkipWhitespace();
        return JsonStringReader::Read(_text, _position);
    }

    bool NextMember(bool& first, std::string& name) {
        if (Consume('}')) {
            return false;
        }
        if (!first) {
            Expect(',');
        }
        name = ReadString();
        Expect(':');
        first = false;
        return true;
    }

    bool NextElement(bool& first) {
        if (Consume(']')) {
            return false;
        }
        if (!first) {
            Expect(',');
        }
        first = false;
        return true;
    }

    void SkipValue(unsigned int depth = 0) {
        JsonSyntaxValidator::ValidateDepth(depth);
        __SkipWhitespace();
        JsonSyntaxValidator::ValidateValue(_text, _position);
        char character = _text[_position];
        if (character == '"') {
            ReadString();
        } else if (character == '{') {
            Expect('{');
            bool first = true;
            std::string name;
            while (NextMember(first, name)) {
                SkipValue(depth + 1);
            }
        } else if (character == '[') {
            Expect('[');
            bool first = true;
            while (NextElement(first)) {
                SkipValue(depth + 1);
            }
        } else if (character == 't') {
            __ReadLiteral("true");
        } else if (character == 'f') {
            __ReadLiteral("false");
        } else if (character == 'n') {
            __ReadLiteral("null");
        } else {
            JsonNumberValidator::Validate(_text, _position);
        }
    }

    void Finish() {
        __SkipWhitespace();
        JsonSyntaxValidator::ValidateComplete(_text, _position);
    }

private:
    void __SkipWhitespace() {
        while (_position < _text.size()) {
            char character = _text[_position];
            if (character != ' ' && character != '\t' && character != '\r' && character != '\n') {
                break;
            }
            ++_position;
        }
    }

    void __ReadLiteral(std::string_view literal) {
        JsonSyntaxValidator::ValidateLiteral(_text, _position, literal);
        _position += literal.size();
    }

    std::string_view _text;
    std::size_t _position = 0;
};
