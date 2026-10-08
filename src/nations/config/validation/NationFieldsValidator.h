#pragma once

#include <stdexcept>
#include <string>

class NationFieldsValidator {
public:
    void ValidateField(const std::string& member) {
        if (member == "code" && !_hasCode) {
            _hasCode = true;
        } else if (member == "voice_country" && !_hasVoice) {
            _hasVoice = true;
        } else {
            throw std::runtime_error("Unknown or duplicate nation field: " + member);
        }
    }

    void ValidateComplete() const {
        if (!_hasCode || !_hasVoice) {
            throw std::runtime_error("Nation requires code and voice_country");
        }
    }

private:
    bool _hasCode = false;
    bool _hasVoice = false;
};
