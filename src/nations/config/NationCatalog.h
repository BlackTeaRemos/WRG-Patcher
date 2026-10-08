#pragma once

#include <array>
#include <string_view>

class NationCatalog {
public:
    static constexpr unsigned int STOCK_COUNT = 23;
    static constexpr unsigned int MAX_COUNT = 62;
    inline static constexpr std::array<const char*, STOCK_COUNT> STOCK_CODES = {
        "US", "UK", "FR", "RDA", "RFA", "URSS", "POL", "TCH", "CAN", "DAN",
        "SWE", "NOR", "NK", "ROK", "CHI", "JAP", "ANZ", "HOL", "ISR", "FIN",
        "YUG", "SA", "ITA"
    };

    static int StockIndex(std::string_view code) {
        for (unsigned int position = 0; position < STOCK_COUNT; ++position) {
            if (code == STOCK_CODES[position]) {
                return static_cast<int>(position);
            }
        }
        return -1;
    }
};
