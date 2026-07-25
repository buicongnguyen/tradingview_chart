#include "analysis/technical_indicators.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace tvchart {
namespace {

constexpr auto kSmaPeriod = std::size_t{20};
constexpr auto kEmaPeriod = std::size_t{20};
constexpr auto kRsiPeriod = std::size_t{14};
constexpr auto kMacdFastPeriod = std::size_t{12};
constexpr auto kMacdSlowPeriod = std::size_t{26};
constexpr auto kMacdSignalPeriod = std::size_t{9};
constexpr auto kSecondsPerUtcDay = std::int64_t{24 * 60 * 60};

void requireValidBars(const Bars& bars) {
    if (const auto error = validateBars(bars)) {
        throw std::invalid_argument("Technical calculations require valid bars: " + *error);
    }
}

[[nodiscard]] double finiteResult(
    const long double value,
    const char* calculation) {
    const auto result = static_cast<double>(value);
    if (!std::isfinite(result)) {
        throw std::overflow_error(
            std::string(calculation) + " produced a non-finite result.");
    }
    return result;
}

[[nodiscard]] std::vector<std::optional<double>> exponentialMovingAverage(
    const std::vector<double>& values,
    const std::size_t period) {
    std::vector<std::optional<double>> result(values.size());
    if (period == 0 || values.size() < period) {
        return result;
    }

    auto sum = 0.0L;
    for (std::size_t index = 0; index < period; ++index) {
        sum += static_cast<long double>(values[index]);
    }
    auto current = finiteResult(sum / static_cast<long double>(period), "EMA");
    result[period - 1] = current;

    const auto multiplier = 2.0 / (static_cast<double>(period) + 1.0);
    for (std::size_t index = period; index < values.size(); ++index) {
        current = finiteResult(
            (static_cast<long double>(values[index] - current) * multiplier) +
                current,
            "EMA");
        result[index] = current;
    }
    return result;
}

[[nodiscard]] IndicatorCalculation simpleMovingAverage(const Bars& bars) {
    IndicatorCalculation result{.kind = IndicatorKind::SimpleMovingAverage};
    if (bars.size() < kSmaPeriod) {
        return result;
    }

    auto sum = 0.0L;
    for (std::size_t index = 0; index < bars.size(); ++index) {
        sum += static_cast<long double>(bars[index].close);
        if (index >= kSmaPeriod) {
            sum -= static_cast<long double>(bars[index - kSmaPeriod].close);
        }
        if (index + 1 >= kSmaPeriod) {
            result.primary.push_back({
                .timestamp = bars[index].timestamp,
                .value = finiteResult(
                    sum / static_cast<long double>(kSmaPeriod),
                    "SMA"),
            });
        }
    }
    return result;
}

[[nodiscard]] IndicatorCalculation exponentialMovingAverage(const Bars& bars) {
    IndicatorCalculation result{.kind = IndicatorKind::ExponentialMovingAverage};
    std::vector<double> closes;
    closes.reserve(bars.size());
    for (const auto& bar : bars) {
        closes.push_back(bar.close);
    }
    const auto values = exponentialMovingAverage(closes, kEmaPeriod);
    for (std::size_t index = 0; index < values.size(); ++index) {
        if (values[index]) {
            result.primary.push_back({
                .timestamp = bars[index].timestamp,
                .value = *values[index],
            });
        }
    }
    return result;
}

[[nodiscard]] IndicatorCalculation volumeWeightedAveragePrice(const Bars& bars) {
    IndicatorCalculation result{
        .kind = IndicatorKind::VolumeWeightedAveragePrice,
    };
    auto currentUtcDay = std::int64_t{-1};
    auto cumulativeNotional = 0.0L;
    auto cumulativeVolume = 0.0L;

    for (const auto& bar : bars) {
        const auto utcDay = bar.timestamp / kSecondsPerUtcDay;
        if (utcDay != currentUtcDay) {
            currentUtcDay = utcDay;
            cumulativeNotional = 0.0L;
            cumulativeVolume = 0.0L;
        }

        const auto typicalPrice =
            (static_cast<long double>(bar.high) +
             static_cast<long double>(bar.low) +
             static_cast<long double>(bar.close)) /
            3.0L;
        cumulativeNotional +=
            typicalPrice * static_cast<long double>(bar.volume);
        cumulativeVolume += static_cast<long double>(bar.volume);
        if (cumulativeVolume > 0.0L) {
            result.primary.push_back({
                .timestamp = bar.timestamp,
                .value = finiteResult(
                    cumulativeNotional / cumulativeVolume,
                    "VWAP"),
            });
        }
    }
    return result;
}

[[nodiscard]] IndicatorCalculation relativeStrengthIndex(const Bars& bars) {
    IndicatorCalculation result{.kind = IndicatorKind::RelativeStrengthIndex};
    if (bars.size() <= kRsiPeriod) {
        return result;
    }

    auto gains = 0.0L;
    auto losses = 0.0L;
    for (std::size_t index = 1; index <= kRsiPeriod; ++index) {
        const auto change = static_cast<long double>(bars[index].close) -
                            static_cast<long double>(bars[index - 1].close);
        gains += std::max(0.0L, change);
        losses += std::max(0.0L, -change);
    }
    auto averageGain = gains / static_cast<long double>(kRsiPeriod);
    auto averageLoss = losses / static_cast<long double>(kRsiPeriod);

    const auto calculate = [](const long double gain, const long double loss) {
        if (loss == 0.0L) {
            return gain == 0.0L ? 50.0L : 100.0L;
        }
        if (gain == 0.0L) {
            return 0.0L;
        }
        const auto strength = gain / loss;
        return 100.0L - (100.0L / (1.0L + strength));
    };

    result.primary.push_back({
        .timestamp = bars[kRsiPeriod].timestamp,
        .value = finiteResult(calculate(averageGain, averageLoss), "RSI"),
    });
    for (std::size_t index = kRsiPeriod + 1; index < bars.size(); ++index) {
        const auto change = static_cast<long double>(bars[index].close) -
                            static_cast<long double>(bars[index - 1].close);
        const auto gain = std::max(0.0L, change);
        const auto loss = std::max(0.0L, -change);
        averageGain =
            ((averageGain * static_cast<long double>(kRsiPeriod - 1)) + gain) /
            static_cast<long double>(kRsiPeriod);
        averageLoss =
            ((averageLoss * static_cast<long double>(kRsiPeriod - 1)) + loss) /
            static_cast<long double>(kRsiPeriod);
        result.primary.push_back({
            .timestamp = bars[index].timestamp,
            .value = finiteResult(calculate(averageGain, averageLoss), "RSI"),
        });
    }
    return result;
}

[[nodiscard]] IndicatorCalculation movingAverageConvergenceDivergence(
    const Bars& bars) {
    IndicatorCalculation result{
        .kind = IndicatorKind::MovingAverageConvergenceDivergence,
    };
    std::vector<double> closes;
    closes.reserve(bars.size());
    for (const auto& bar : bars) {
        closes.push_back(bar.close);
    }

    const auto fast = exponentialMovingAverage(closes, kMacdFastPeriod);
    const auto slow = exponentialMovingAverage(closes, kMacdSlowPeriod);
    std::vector<double> macdValues;
    std::vector<std::size_t> macdIndexes;
    for (std::size_t index = 0; index < bars.size(); ++index) {
        if (fast[index] && slow[index]) {
            const auto macd = finiteResult(*fast[index] - *slow[index], "MACD");
            result.primary.push_back({
                .timestamp = bars[index].timestamp,
                .value = macd,
            });
            macdValues.push_back(macd);
            macdIndexes.push_back(index);
        }
    }

    const auto signal =
        exponentialMovingAverage(macdValues, kMacdSignalPeriod);
    for (std::size_t index = 0; index < signal.size(); ++index) {
        if (!signal[index]) {
            continue;
        }
        const auto barIndex = macdIndexes[index];
        result.secondary.push_back({
            .timestamp = bars[barIndex].timestamp,
            .value = *signal[index],
        });
        result.histogram.push_back({
            .timestamp = bars[barIndex].timestamp,
            .value = finiteResult(
                static_cast<long double>(macdValues[index]) - *signal[index],
                "MACD histogram"),
        });
    }
    return result;
}

} // namespace

IndicatorCalculation calculateIndicator(
    const Bars& bars,
    const IndicatorKind kind) {
    if (kind == IndicatorKind::None) {
        return {};
    }
    requireValidBars(bars);
    switch (kind) {
    case IndicatorKind::None:
        return {};
    case IndicatorKind::SimpleMovingAverage:
        return simpleMovingAverage(bars);
    case IndicatorKind::ExponentialMovingAverage:
        return exponentialMovingAverage(bars);
    case IndicatorKind::VolumeWeightedAveragePrice:
        return volumeWeightedAveragePrice(bars);
    case IndicatorKind::RelativeStrengthIndex:
        return relativeStrengthIndex(bars);
    case IndicatorKind::MovingAverageConvergenceDivergence:
        return movingAverageConvergenceDivergence(bars);
    }
    return {};
}

MarketStatistics calculateMarketStatistics(const Bars& bars) {
    requireValidBars(bars);
    const auto& latest = bars.back();
    const auto previousClose =
        bars.size() > 1 ? bars[bars.size() - 2].close : latest.open;
    const auto barChange = finiteResult(
        static_cast<long double>(latest.close) -
            static_cast<long double>(previousClose),
        "Bar change");

    MarketStatistics statistics{
        .latestClose = latest.close,
        .barChange = barChange,
        .barChangePercent =
            previousClose > 0.0
                ? finiteResult(
                      static_cast<long double>(barChange) * 100.0L /
                          static_cast<long double>(previousClose),
                      "Bar change percentage")
                : 0.0,
        .loadedHigh = latest.high,
        .loadedLow = latest.low,
    };
    for (const auto& bar : bars) {
        statistics.loadedHigh = std::max(statistics.loadedHigh, bar.high);
        statistics.loadedLow = std::min(statistics.loadedLow, bar.low);
    }

    const auto range =
        static_cast<long double>(statistics.loadedHigh) -
        static_cast<long double>(statistics.loadedLow);
    statistics.loadedRangePositionPercent =
        range > 0.0L
            ? finiteResult(
                  (static_cast<long double>(latest.close) -
                   static_cast<long double>(statistics.loadedLow)) *
                      100.0L / range,
                  "Loaded range position")
            : 50.0;

    const auto volumeCount = std::min<std::size_t>(20, bars.size());
    auto volumeTotal = 0.0L;
    for (auto index = bars.size() - volumeCount; index < bars.size(); ++index) {
        volumeTotal += static_cast<long double>(bars[index].volume);
    }
    statistics.averageVolume20 = finiteResult(
        volumeTotal / static_cast<long double>(volumeCount),
        "Average volume");
    return statistics;
}

} // namespace tvchart
