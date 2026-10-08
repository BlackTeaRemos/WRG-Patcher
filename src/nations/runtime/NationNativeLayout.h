#pragma once

#include "../../internal.h"
#include <array>
#include <cstdint>
#include <cstring>

/** Native addresses and instruction guards for executable 1ca4400-6f9dca58. */
class NationNativeLayout {
public:
    static constexpr std::uintptr_t IMAGE_BASE = 0x400000;
    static constexpr std::uintptr_t COUNTRY_RESOLVER = 0xA67F40;
    static constexpr std::uintptr_t STRING_RESOLVE = 0x502540;
    static constexpr std::uintptr_t STRING_EQUALS = 0x4FCD50;
    static constexpr std::uintptr_t VOICE_ALLOCATION = 0xA646B5;
    static constexpr std::uintptr_t VOICE_LOOKUP = 0xA6485F;
    static constexpr std::uintptr_t COUNTRY_TABLE = 0x1DB0F30;
    static constexpr unsigned int VOICE_TYPES = 43;
    static constexpr unsigned int VOICE_ACTIONS = 24;

    static BYTE* Address(std::uintptr_t address) {
        return reinterpret_cast<BYTE*>(GetModuleHandleW(nullptr)) + address - IMAGE_BASE;
    }

    /** Refuse other executable layouts and hooks that have changed a patch site. */
    static bool Matches() {
        if (std::strcmp(wrg_version_tag(), "1ca4400-6f9dca58") != 0) {
            return false;
        }
        const auto* header = reinterpret_cast<const IMAGE_DOS_HEADER*>(GetModuleHandleW(nullptr));
        const auto* portable = reinterpret_cast<const IMAGE_NT_HEADERS32*>(
            reinterpret_cast<const BYTE*>(header) + header->e_lfanew);
        if (portable->FileHeader.Machine != IMAGE_FILE_MACHINE_I386
            || portable->OptionalHeader.SizeOfImage != 0x1E68000
            || portable->FileHeader.TimeDateStamp != 1725971169) {
            return false;
        }
        const std::array<BYTE, 12> prologue = {0x56, 0x8D, 0x4C, 0x24, 0x08,
            0xE8, 0xF6, 0xA5, 0xA9, 0xFF, 0x8B, 0xF0};
        const std::array<BYTE, 10> allocation = {0x68, 0xB8, 0x5C, 0, 0, 0xE8, 1, 0x4B, 0, 0};
        const std::array<BYTE, 5> voiceLookup = {0xE8, 0xDC, 0x36, 0, 0};
        const std::array<BYTE, 3> stride = {0x6B, 0xC0, 0x2B};
        if (std::memcmp(Address(COUNTRY_RESOLVER), prologue.data(), prologue.size()) != 0
            || std::memcmp(Address(VOICE_ALLOCATION), allocation.data(), allocation.size()) != 0
            || std::memcmp(Address(VOICE_LOOKUP), voiceLookup.data(), voiceLookup.size()) != 0
            || std::memcmp(Address(0xA64706), stride.data(), stride.size()) != 0
            || std::memcmp(Address(0xA6486B), stride.data(), stride.size()) != 0) {
            return false;
        }
        const std::array<BYTE, 3> indexedPush = {0xFF, 0x34, 0x85};
        for (std::uintptr_t instruction : {0x12CD10Eu, 0x12CD156u}) {
            std::uintptr_t pointer = 0;
            std::memcpy(&pointer, Address(instruction) + 3, sizeof(pointer));
            if (std::memcmp(Address(instruction), indexedPush.data(), indexedPush.size()) != 0
                || pointer != reinterpret_cast<std::uintptr_t>(Address(COUNTRY_TABLE))) {
                return false;
            }
        }
        return true;
    }
};
