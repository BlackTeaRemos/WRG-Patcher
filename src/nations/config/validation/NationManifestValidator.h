#pragma once

#include <set>
#include <stdexcept>
#include <string>

class NationManifestValidator {
public:
    void ValidateNationsField() {
        if (_hasNations) {
            throw std::runtime_error("Duplicate nations field in mod.json");
        }
        _hasNations = true;
    }

    void ValidateCountry(const std::string& code) {
        if (!_declaredCountries.insert(code).second) {
            throw std::runtime_error("Duplicate country in mod.json: " + code);
        }
    }

private:
    bool _hasNations = false;
    std::set<std::string> _declaredCountries;
};
