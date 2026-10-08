#pragma once

#include "NationRegistry.h"
#include "validation/NationFieldsValidator.h"
#include "validation/NationManifestFileValidator.h"
#include "validation/NationManifestValidator.h"
#include "../../json/JsonReader.h"
#include "../../internal.h"
#include <fstream>
#include <filesystem>

class NationManifestReader {
public:
    static void ReadEnabled(NationRegistry& registry) {
        for (int modPosition = 0; modPosition < g_nmods; ++modPosition) {
            std::wstring directory = std::wstring(g_modsroot) + L"\\" + g_mods[modPosition];
            std::wstring path = directory + L"\\mod.json";
            if (!NationManifestFileValidator::ValidatePath(directory, path)) {
                continue;
            }
            __Read(path, registry);
        }
        registry.Seal();
    }

private:
    static void __Read(const std::wstring& path, NationRegistry& registry) {
        std::ifstream input(std::filesystem::path(path), std::ios::binary | std::ios::ate);
        auto length = input.tellg();
        NationManifestFileValidator::ValidateSize(static_cast<bool>(input), length);
        std::string text(static_cast<std::size_t>(length), '\0');
        input.seekg(0);
        NationManifestFileValidator::ValidateRead(
            static_cast<bool>(input.read(text.data(), static_cast<std::streamsize>(text.size()))));
        JsonReader reader(text);
        reader.Expect('{');
        bool first = true;
        NationManifestValidator validator;
        std::string member;
        while (reader.NextMember(first, member)) {
            if (member == "nations") {
                validator.ValidateNationsField();
                __ReadNations(reader, registry, validator);
            } else {
                reader.SkipValue(1);
            }
        }
        reader.Finish();
    }

    static void __ReadNations(JsonReader& reader, NationRegistry& registry, NationManifestValidator& validator) {
        reader.Expect('[');
        bool first = true;
        while (reader.NextElement(first)) {
            NationDefinition definition = __ReadDefinition(reader);
            validator.ValidateCountry(definition.code);
            registry.Add(std::move(definition));
        }
    }

    static NationDefinition __ReadDefinition(JsonReader& reader) {
        reader.Expect('{');
        bool first = true;
        NationFieldsValidator validator;
        NationDefinition definition;
        std::string member;
        while (reader.NextMember(first, member)) {
            validator.ValidateField(member);
            if (member == "code") {
                definition.code = reader.ReadString();
            } else {
                definition.sourceCountry = reader.ReadString();
            }
        }
        validator.ValidateComplete();
        return definition;
    }
};
