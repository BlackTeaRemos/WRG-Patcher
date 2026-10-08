#pragma once

#include "NationCatalog.h"
#include "validation/NationConfigValidator.h"
#include "NationDefinition.h"
#include <algorithm>
#include <array>
#include <utility>
#include <vector>

class NationRegistry {
public:
    NationRegistry() = default;

    void Add(NationDefinition definition) {
        NationConfigValidator::ValidateDefinition(definition);
        const auto existing = std::find_if(_definitions.begin(), _definitions.end(),
            [&definition](const NationDefinition& candidate) {
                return candidate.code == definition.code;
            });
        if (existing != _definitions.end()) {
            NationConfigValidator::ValidateDuplicate(definition, *existing);
            return;
        }
        NationConfigValidator::ValidateCapacity(_definitions.size());
        _definitions.push_back(std::move(definition));
    }

    void Seal() {
        std::sort(_definitions.begin(), _definitions.end(),
            [](const NationDefinition& left, const NationDefinition& right) {
                return left.code < right.code;
            });
        for (unsigned int position = 0; position < NationCatalog::STOCK_COUNT; ++position) {
            _countryCodes[position] = NationCatalog::STOCK_CODES[position];
            _voiceCountries[position] = position;
        }
        for (std::size_t position = 0; position < _definitions.size(); ++position) {
            _countryCodes[NationCatalog::STOCK_COUNT + position] = _definitions[position].code.c_str();
            _voiceCountries[NationCatalog::STOCK_COUNT + position] =
                static_cast<unsigned int>(NationCatalog::StockIndex(_definitions[position].sourceCountry));
        }
    }

    unsigned int Count() const {
        return NationCatalog::STOCK_COUNT + static_cast<unsigned int>(_definitions.size());
    }

    const char* const* CountryCodes() const {
        return _countryCodes.data();
    }

    unsigned int VoiceCountry(unsigned int country) const {
        return country < Count() ? _voiceCountries[country] : country;
    }

private:
    std::vector<NationDefinition> _definitions;
    std::array<const char*, NationCatalog::MAX_COUNT> _countryCodes{};
    std::array<unsigned int, NationCatalog::MAX_COUNT> _voiceCountries{};
};
