#pragma once

#include "domain/bar.hpp"

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace tvchart {

class DemoDataSource final {
public:
    [[nodiscard]] static Bars generate(
        std::string_view symbol,
        Timeframe timeframe,
        std::size_t count,
        std::int64_t endTimestamp);
};

} // namespace tvchart
