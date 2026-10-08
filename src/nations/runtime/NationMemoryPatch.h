#pragma once

#include "NationResolver.h"
#include <array>

/** Publish country lookup, reverse lookup, and correctly sized voice storage together. */
class NationMemoryPatch {
public:
    static bool Install(const NationRegistry& registry) {
        if (!NationNativeLayout::Matches() || !NationResolver::Prepare(registry)) {
            return false;
        }
        std::array<NationMemoryPatch, 5> changes = {
            __Branch(NationNativeLayout::COUNTRY_RESOLVER, 0xE9, reinterpret_cast<std::uintptr_t>(&NationResolver::Resolve)),
            __Branch(NationNativeLayout::VOICE_LOOKUP, 0xE8, reinterpret_cast<std::uintptr_t>(&NationResolver::ResolveVoice)),
            __Word(NationNativeLayout::VOICE_ALLOCATION + 1,
                registry.Count() * NationNativeLayout::VOICE_TYPES * NationNativeLayout::VOICE_ACTIONS),
            __Word(0x12CD111, reinterpret_cast<std::uintptr_t>(registry.CountryCodes())),
            __Word(0x12CD159, reinterpret_cast<std::uintptr_t>(registry.CountryCodes()))
        };
        // Acquire every page before the first engine byte changes.
        for (std::size_t position = 0; position < changes.size(); ++position) {
            if (!VirtualProtect(changes[position]._address, changes[position]._length,
                    PAGE_EXECUTE_READWRITE, &changes[position]._protection)) {
                for (std::size_t protectedPosition = position; protectedPosition > 0; --protectedPosition) {
                    changes[protectedPosition - 1].__RestoreProtection();
                }
                return false;
            }
        }
        for (const NationMemoryPatch& change : changes) {
            std::memcpy(change._address, change._bytes.data(), change._length);
            FlushInstructionCache(GetCurrentProcess(), change._address, change._length);
        }
        for (std::size_t position = changes.size(); position > 0; --position) {
            changes[position - 1].__RestoreProtection();
        }
        return true;
    }

private:
    NationMemoryPatch() = default;

    static NationMemoryPatch __Branch(std::uintptr_t address, BYTE opcode, std::uintptr_t target) {
        NationMemoryPatch patch;
        patch._address = NationNativeLayout::Address(address);
        patch._length = 5;
        patch._bytes[0] = opcode;
        std::uint32_t displacement = static_cast<std::uint32_t>(target
            - reinterpret_cast<std::uintptr_t>(patch._address + 5));
        std::memcpy(patch._bytes.data() + 1, &displacement, sizeof(displacement));
        return patch;
    }

    static NationMemoryPatch __Word(std::uintptr_t address, std::uint32_t value) {
        NationMemoryPatch patch;
        patch._address = NationNativeLayout::Address(address);
        patch._length = 4;
        std::memcpy(patch._bytes.data(), &value, sizeof(value));
        return patch;
    }

    void __RestoreProtection() const {
        DWORD ignored = 0;
        VirtualProtect(_address, _length, _protection, &ignored);
    }

    BYTE* _address = nullptr;
    std::array<BYTE, 5> _bytes{};
    std::size_t _length = 0;
    DWORD _protection = 0;
};
