#pragma once

#include "NationNativeLayout.h"
#include "../config/NationCatalog.h"
#include "../config/NationRegistry.h"

/** Preserve stock resolution while giving authored codes their own country IDs. */
class NationResolver {
public:
    static bool Prepare(const NationRegistry& registry) {
        BYTE* trampoline = static_cast<BYTE*>(VirtualAlloc(nullptr, 10,
            MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
        if (!trampoline) {
            return false;
        }
        std::memcpy(trampoline, NationNativeLayout::Address(NationNativeLayout::COUNTRY_RESOLVER), 5);
        trampoline[5] = 0xE9;
        const auto displacement = static_cast<std::uint32_t>(
            reinterpret_cast<std::uintptr_t>(NationNativeLayout::Address(NationNativeLayout::COUNTRY_RESOLVER) + 5)
            - reinterpret_cast<std::uintptr_t>(trampoline + 10));
        std::memcpy(trampoline + 6, &displacement, sizeof(displacement));
        DWORD previousProtection = 0;
        if (!VirtualProtect(trampoline, 10, PAGE_EXECUTE_READ, &previousProtection)) {
            VirtualFree(trampoline, 0, MEM_RELEASE);
            return false;
        }
        FlushInstructionCache(GetCurrentProcess(), trampoline, 10);
        _registry = &registry;
        _original = reinterpret_cast<CountryLookup>(trampoline);
        return true;
    }

    static int __cdecl Resolve(std::uint32_t countryToken) {
        void* stringObject = reinterpret_cast<StringResolve>(
            NationNativeLayout::Address(NationNativeLayout::STRING_RESOLVE))(&countryToken);
        const auto equals = reinterpret_cast<StringEquals>(
            NationNativeLayout::Address(NationNativeLayout::STRING_EQUALS));
        for (unsigned int country = NationCatalog::STOCK_COUNT; country < _registry->Count(); ++country) {
            if (equals(stringObject, _registry->CountryCodes()[country])) {
                return static_cast<int>(country);
            }
        }
        return _original(countryToken);
    }

    /** The acknowledgement query alone uses the author's explicit native donor. */
    static int __cdecl ResolveVoice(std::uint32_t countryToken) {
        return static_cast<int>(_registry->VoiceCountry(static_cast<unsigned int>(Resolve(countryToken))));
    }

private:
    using CountryLookup = int (__cdecl*)(std::uint32_t);
    using StringResolve = void* (__thiscall*)(const void*);
    using StringEquals = bool (__thiscall*)(void*, const char*);
    inline static CountryLookup _original = nullptr;
    inline static const NationRegistry* _registry = nullptr;
};
