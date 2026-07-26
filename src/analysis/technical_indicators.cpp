#include "analysis/technical_indicators.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace tvchart {
namespace {

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

[[nodiscard]] IndicatorCalculation makeCalculation(const IndicatorSpec& spec) {
    return {
        .kind = spec.kind,
        .spec = spec,
        .label = indicatorLabel(spec),
    };
}

[[nodiscard]] IndicatorCalculation simpleMovingAverage(
    const Bars& bars,
    const IndicatorSpec& spec) {
    auto result = makeCalculation(spec);
    const auto period = static_cast<std::size_t>(spec.period);
    if (bars.size() < period) {
        return result;
    }

    auto sum = 0.0L;
    for (std::size_t index = 0; index < bars.size(); ++index) {
        sum += static_cast<long double>(bars[index].close);
        if (index >= period) {
            sum -= static_cast<long double>(bars[index - period].close);
        }
        if (index + 1 >= period) {
            result.primary.push_back({
                .timestamp = bars[index].timestamp,
                .value = finiteResult(
                    sum / static_cast<long double>(period),
                    "SMA"),
            });
        }
    }
    return result;
}

[[nodiscard]] IndicatorCalculation exponentialMovingAverage(
    const Bars& bars,
    const IndicatorSpec& spec) {
    auto result = makeCalculation(spec);
    std::vector<double> closes;
    closes.reserve(bars.size());
    for (const auto& bar : bars) {
        closes.push_back(bar.close);
    }
    const auto values = exponentialMovingAverage(
        closes,
        static_cast<std::size_t>(spec.period));
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

[[nodiscard]] IndicatorCalculation volumeWeightedAveragePrice(
    const Bars& bars,
    const IndicatorSpec& spec) {
    auto result = makeCalculation(spec);
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

[[nodiscard]] IndicatorCalculation relativeStrengthIndex(
    const Bars& bars,
    const IndicatorSpec& spec) {
    auto result = makeCalculation(spec);
    const auto period = static_cast<std::size_t>(spec.period);
    if (bars.size() <= period) {
        return result;
    }

    auto gains = 0.0L;
    auto losses = 0.0L;
    for (std::size_t index = 1; index <= period; ++index) {
        const auto change = static_cast<long double>(bars[index].close) -
                            static_cast<long double>(bars[index - 1].close);
        gains += std::max(0.0L, change);
        losses += std::max(0.0L, -change);
    }
    auto averageGain = gains / static_cast<long double>(period);
    auto averageLoss = losses / static_cast<long double>(period);

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
        .timestamp = bars[period].timestamp,
        .value = finiteResult(calculate(averageGain, averageLoss), "RSI"),
    });
    for (std::size_t index = period + 1; index < bars.size(); ++index) {
        const auto change = static_cast<long double>(bars[index].close) -
                            static_cast<long double>(bars[index - 1].close);
        const auto gain = std::max(0.0L, change);
        const auto loss = std::max(0.0L, -change);
        averageGain =
            ((averageGain * static_cast<long double>(period - 1)) + gain) /
            static_cast<long double>(period);
        averageLoss =
            ((averageLoss * static_cast<long double>(period - 1)) + loss) /
            static_cast<long double>(period);
        result.primary.push_back({
            .timestamp = bars[index].timestamp,
            .value = finiteResult(calculate(averageGain, averageLoss), "RSI"),
        });
    }
    return result;
}

[[nodiscard]] IndicatorCalculation movingAverageConvergenceDivergence(
    const Bars& bars,
    const IndicatorSpec& spec) {
    auto result = makeCalculation(spec);
    std::vector<double> closes;
    closes.reserve(bars.size());
    for (const auto& bar : bars) {
        closes.push_back(bar.close);
    }

    const auto fast = exponentialMovingAverage(
        closes,
        static_cast<std::size_t>(spec.fastPeriod));
    const auto slow = exponentialMovingAverage(
        closes,
        static_cast<std::size_t>(spec.slowPeriod));
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
        exponentialMovingAverage(
            macdValues,
            static_cast<std::size_t>(spec.signalPeriod));
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

[[nodiscard]] IndicatorCalculation rollingExtreme(
    const Bars& bars,
    const IndicatorSpec& spec,
    const bool high) {
    auto result = makeCalculation(spec);
    const auto period = static_cast<std::size_t>(spec.period);
    if (bars.size() < period) {
        return result;
    }

    for (std::size_t index = period - 1; index < bars.size(); ++index) {
        auto value = high ? bars[index - period + 1].high
                          : bars[index - period + 1].low;
        for (auto window = index - period + 2; window <= index; ++window) {
            value = high ? std::max(value, bars[window].high)
                         : std::min(value, bars[window].low);
        }
        result.primary.push_back({
            .timestamp = bars[index].timestamp,
            .value = value,
        });
    }
    return result;
}

[[nodiscard]] IndicatorCalculation volumeSimpleMovingAverage(
    const Bars& bars,
    const IndicatorSpec& spec) {
    auto result = makeCalculation(spec);
    const auto period = static_cast<std::size_t>(spec.period);
    if (bars.size() < period) {
        return result;
    }

    auto sum = 0.0L;
    for (std::size_t index = 0; index < bars.size(); ++index) {
        sum += static_cast<long double>(bars[index].volume);
        if (index >= period) {
            sum -= static_cast<long double>(bars[index - period].volume);
        }
        if (index + 1 >= period) {
            result.primary.push_back({
                .timestamp = bars[index].timestamp,
                .value = finiteResult(
                    sum / static_cast<long double>(period),
                    "Volume SMA"),
            });
        }
    }
    return result;
}

} // namespace

IndicatorSpec defaultIndicatorSpec(const IndicatorKind kind) noexcept {
    IndicatorSpec spec{.kind = kind};
    if (kind == IndicatorKind::RelativeStrengthIndex) {
        spec.period = 14;
    }
    return spec;
}

std::string indicatorLabel(const IndicatorSpec& spec) {
    std::ostringstream label;
    switch (spec.kind) {
    case IndicatorKind::None:
        return "None";
    case IndicatorKind::SimpleMovingAverage:
        label << "SMA (" << spec.period << ')';
        break;
    case IndicatorKind::ExponentialMovingAverage:
        label << "EMA (" << spec.period << ')';
        break;
    case IndicatorKind::VolumeWeightedAveragePrice:
        return "VWAP (UTC session)";
    case IndicatorKind::RelativeStrengthIndex:
        label << "RSI (" << spec.period << ')';
        break;
    case IndicatorKind::MovingAverageConvergenceDivergence:
        label << "MACD (" << spec.fastPeriod << ", " << spec.slowPeriod
              << ", " << spec.signalPeriod << ')';
        break;
    case IndicatorKind::RollingHigh:
        label << "Rolling high (" << spec.period << ')';
        break;
    case IndicatorKind::RollingLow:
        label << "Rolling low (" << spec.period << ')';
        break;
    case IndicatorKind::VolumeSimpleMovingAverage:
        label << "Volume SMA (" << spec.period << ')';
        break;
    }
    return label.str();
}

bool validIndicatorSpec(const IndicatorSpec& spec) noexcept {
    constexpr auto maximumPeriod = std::uint32_t{500};
    switch (spec.kind) {
    case IndicatorKind::None:
    case IndicatorKind::VolumeWeightedAveragePrice:
        return true;
    case IndicatorKind::MovingAverageConvergenceDivergence:
        return spec.fastPeriod > 0 &&
               spec.slowPeriod > spec.fastPeriod &&
               spec.slowPeriod <= maximumPeriod &&
               spec.signalPeriod > 0 &&
               spec.signalPeriod <= maximumPeriod;
    case IndicatorKind::SimpleMovingAverage:
    case IndicatorKind::ExponentialMovingAverage:
    case IndicatorKind::RelativeStrengthIndex:
    case IndicatorKind::RollingHigh:
    case IndicatorKind::RollingLow:
    case IndicatorKind::VolumeSimpleMovingAverage:
        return spec.period > 0 && spec.period <= maximumPeriod;
    }
    return false;
}

IndicatorCalculation calculateIndicator(
    const Bars& bars,
    const IndicatorKind kind) {
    return calculateIndicator(bars, defaultIndicatorSpec(kind));
}

IndicatorCalculation calculateIndicator(
    const Bars& bars,
    const IndicatorSpec& spec) {
    if (!validIndicatorSpec(spec)) {
        throw std::invalid_argument("Invalid technical indicator parameters.");
    }
    if (spec.kind == IndicatorKind::None) {
        return makeCalculation(spec);
    }
    requireValidBars(bars);
    switch (spec.kind) {
    case IndicatorKind::None:
        return makeCalculation(spec);
    case IndicatorKind::SimpleMovingAverage:
        return simpleMovingAverage(bars, spec);
    case IndicatorKind::ExponentialMovingAverage:
        return exponentialMovingAverage(bars, spec);
    case IndicatorKind::VolumeWeightedAveragePrice:
        return volumeWeightedAveragePrice(bars, spec);
    case IndicatorKind::RelativeStrengthIndex:
        return relativeStrengthIndex(bars, spec);
    case IndicatorKind::MovingAverageConvergenceDivergence:
        return movingAverageConvergenceDivergence(bars, spec);
    case IndicatorKind::RollingHigh:
        return rollingExtreme(bars, spec, true);
    case IndicatorKind::RollingLow:
        return rollingExtreme(bars, spec, false);
    case IndicatorKind::VolumeSimpleMovingAverage:
        return volumeSimpleMovingAverage(bars, spec);
    }
    return makeCalculation(spec);
}

std::vector<IndicatorCalculation> calculateIndicators(
    const Bars& bars,
    const std::vector<IndicatorSpec>& specs) {
    std::vector<IndicatorCalculation> calculations;
    calculations.reserve(specs.size());
    for (const auto& spec : specs) {
        if (spec.kind != IndicatorKind::None) {
            calculations.push_back(calculateIndicator(bars, spec));
        }
    }
    return calculations;
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
