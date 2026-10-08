#pragma once

#include "../NationCatalog.h"
#include "../NationDefinition.h"
#include <cstddef>
#include <stdexcept>

class NationConfigValidator {
public:
    static void ValidateDefinition(const NationDefinition& definition) {
        if (definition.code.empty() || definition.code.size() > 10
            || definition.code.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_") != std::string::npos) {
            throw std::runtime_error("Country code: use 1-10 characters (A-Z, 0-9, _)");
        }
        if (NationCatalog::StockIndex(definition.code) >= 0 || definition.code == "GER" || definition.code == "ALL") {
            throw std::runtime_error("Reserved country code: " + definition.code);
        }
        if (NationCatalog::StockIndex(definition.sourceCountry) < 0) {
            throw std::runtime_error("Unknown stock voice country: " + definition.sourceCountry);
        }
    }

    static void ValidateDuplicate(const NationDefinition& definition, const NationDefinition& existing) {
        if (existing.sourceCountry != definition.sourceCountry) {
            throw std::runtime_error("Conflicting voice country for " + definition.code);
        }
    }

    static void ValidateCapacity(std::size_t definitionCount) {
        if (definitionCount >= NationCatalog::MAX_COUNT - NationCatalog::STOCK_COUNT) {
            throw std::runtime_error("Country limit exceeded: " + std::to_string(NationCatalog::MAX_COUNT));
        }
    }
};
