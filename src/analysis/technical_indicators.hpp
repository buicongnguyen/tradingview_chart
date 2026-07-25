#pragma once

#include "domain/bar.hpp"

#include <cstdint>
#include <string_view>
#include <vector>

namespace tvchart {

enum class IndicatorKind : std::uint8_t {
    None,
    SimpleMovingAverage,
    ExponentialMovingAverage,
    VolumeWeightedAveragePrice,
    RelativeStrengthIndex,
    MovingAverageConvergenceDivergence,
};

struct IndicatorPoint {
    std::int64_t timestamp{};
    double value{};

    [[nodiscard]] bool operator==(const IndicatorPoint&) const = default;
};

struct IndicatorCalculation {
    IndicatorKind kind{IndicatorKind::None};
    std::vector<IndicatorPoint> primary;
    std::vector<IndicatorPoint> secondary;
    std::vector<IndicatorPoint> histogram;
};

struct MarketStatistics {
    double latestClose{};
    double barChange{};
    double barChangePercent{};
    double loadedHigh{};
    double loadedLow{};
    double loadedRangePositionPercent{};
    double averageVolume20{};
};

[[nodiscard]] constexpr std::string_view indicatorId(
    const IndicatorKind kind) noexcept {
    switch (kind) {
    case IndicatorKind::None:
        return "none";
    case IndicatorKind::SimpleMovingAverage:
        return "sma";
    case IndicatorKind::ExponentialMovingAverage:
        return "ema";
    case IndicatorKind::VolumeWeightedAveragePrice:
        return "vwap";
    case IndicatorKind::RelativeStrengthIndex:
        return "rsi";
    case IndicatorKind::MovingAverageConvergenceDivergence:
        return "macd";
    }
    return "none";
}

[[nodiscard]] constexpr std::string_view indicatorLabel(
    const IndicatorKind kind) noexcept {
    switch (kind) {
    case IndicatorKind::None:
        return "None";
    case IndicatorKind::SimpleMovingAverage:
        return "SMA (20)";
    case IndicatorKind::ExponentialMovingAverage:
        return "EMA (20)";
    case IndicatorKind::VolumeWeightedAveragePrice:
        return "VWAP (UTC session)";
    case IndicatorKind::RelativeStrengthIndex:
        return "RSI (14)";
    case IndicatorKind::MovingAverageConvergenceDivergence:
        return "MACD (12, 26, 9)";
    }
    return "None";
}

[[nodiscard]] IndicatorCalculation calculateIndicator(
    const Bars& bars,
    IndicatorKind kind);
[[nodiscard]] MarketStatistics calculateMarketStatistics(const Bars& bars);

} // namespace tvchart
