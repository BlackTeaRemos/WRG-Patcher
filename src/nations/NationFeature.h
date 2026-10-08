#pragma once

#include "config/NationCatalog.h"
#include "config/NationManifestReader.h"
#include "config/NationRegistry.h"
#include "runtime/NationMemoryPatch.h"
#include <memory>

class NationFeature {
public:
    static bool Initialize() {
        try {
            auto registry = std::make_unique<NationRegistry>();
            NationManifestReader::ReadEnabled(*registry);
            if (registry->Count() == NationCatalog::STOCK_COUNT) {
                return true;
            }
            if (!NationMemoryPatch::Install(*registry)) {
                wrg_log(L"NATIONS-FAIL", g_modsroot, L"Unsupported executable or country extension could not be installed");
                return false;
            }
            unsigned int added = registry->Count() - NationCatalog::STOCK_COUNT;
            registry.release();
            std::wstring message = std::to_wstring(added) + L" additional countries - runtime contract wrg.nations.v2";
            wrg_log(L"NATIONS", g_modsroot, message.c_str());
            return true;
        } catch (const std::exception& failure) {
            std::string message = failure.what();
            std::wstring wideMessage(message.begin(), message.end());
            wrg_log(L"NATIONS-FAIL", g_modsroot, wideMessage.c_str());
            return false;
        }
    }
};
