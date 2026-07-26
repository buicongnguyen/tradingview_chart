#pragma once

#include "domain/bar.hpp"

#include <cstdint>
#include <string>
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
    RollingHigh,
    RollingLow,
    VolumeSimpleMovingAverage,
};

struct IndicatorSpec {
    IndicatorKind kind{IndicatorKind::None};
    std::uint32_t period{20};
    std::uint32_t fastPeriod{12};
    std::uint32_t slowPeriod{26};
    std::uint32_t signalPeriod{9};

    [[nodiscard]] bool operator==(const IndicatorSpec&) const = default;
};

struct IndicatorPoint {
    std::int64_t timestamp{};
    double value{};

    [[nodiscard]] bool operator==(const IndicatorPoint&) const = default;
};

struct IndicatorCalculation {
    IndicatorKind kind{IndicatorKind::None};
    IndicatorSpec spec;
    std::string label{"None"};
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
    case IndicatorKind::RollingHigh:
        return "rolling-high";
    case IndicatorKind::RollingLow:
        return "rolling-low";
    case IndicatorKind::VolumeSimpleMovingAverage:
        return "volume-sma";
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
    case IndicatorKind::RollingHigh:
        return "Rolling high (20)";
    case IndicatorKind::RollingLow:
        return "Rolling low (20)";
    case IndicatorKind::VolumeSimpleMovingAverage:
        return "Volume SMA (20)";
    }
    return "None";
}

[[nodiscard]] IndicatorSpec defaultIndicatorSpec(IndicatorKind kind) noexcept;
[[nodiscard]] std::string indicatorLabel(const IndicatorSpec& spec);
[[nodiscard]] bool validIndicatorSpec(const IndicatorSpec& spec) noexcept;
[[nodiscard]] IndicatorCalculation calculateIndicator(
    const Bars& bars,
    IndicatorKind kind);
[[nodiscard]] IndicatorCalculation calculateIndicator(
    const Bars& bars,
    const IndicatorSpec& spec);
[[nodiscard]] std::vector<IndicatorCalculation> calculateIndicators(
    const Bars& bars,
    const std::vector<IndicatorSpec>& specs);
[[nodiscard]] MarketStatistics calculateMarketStatistics(const Bars& bars);

} // namespace tvchart
