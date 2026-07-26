#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace tvchart {

enum class Timeframe : std::uint8_t {
    OneMinute,
    FiveMinutes,
    FifteenMinutes,
    OneHour,
    OneDay,
};

[[nodiscard]] constexpr std::int64_t timeframeSeconds(const Timeframe timeframe) noexcept {
    switch (timeframe) {
    case Timeframe::OneMinute:
        return 60;
    case Timeframe::FiveMinutes:
        return 5 * 60;
    case Timeframe::FifteenMinutes:
        return 15 * 60;
    case Timeframe::OneHour:
        return 60 * 60;
    case Timeframe::OneDay:
        return 24 * 60 * 60;
    }
    return 60;
}

struct Bar {
    std::int64_t timestamp{};
    double open{};
    double high{};
    double low{};
    double close{};
    double volume{};

    [[nodiscard]] bool operator==(const Bar&) const = default;
};

using Bars = std::vector<Bar>;

[[nodiscard]] std::optional<std::string> validateBar(const Bar& bar);
[[nodiscard]] std::optional<std::string> validateBars(const Bars& bars);
[[nodiscard]] std::size_t completedBarCount(
    const Bars& bars,
    Timeframe timeframe,
    std::int64_t asOfUtc) noexcept;

} // namespace tvchart
