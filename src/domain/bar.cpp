#include "domain/bar.hpp"

#include <algorithm>
#include <cmath>
#include <iterator>

namespace tvchart {

std::optional<std::string> validateBar(const Bar& bar) {
    if (bar.timestamp <= 0) {
        return "timestamp must be a positive Unix timestamp";
    }

    const auto finite = [](const double value) {
        return std::isfinite(value);
    };
    if (!finite(bar.open) || !finite(bar.high) || !finite(bar.low) ||
        !finite(bar.close) || !finite(bar.volume)) {
        return "prices and volume must be finite numbers";
    }
    if (bar.open <= 0.0 || bar.high <= 0.0 || bar.low <= 0.0 || bar.close <= 0.0) {
        return "prices must be greater than zero";
    }
    if (bar.volume < 0.0) {
        return "volume must not be negative";
    }
    if (bar.high < std::max(bar.open, bar.close)) {
        return "high must be greater than or equal to open and close";
    }
    if (bar.low > std::min(bar.open, bar.close)) {
        return "low must be less than or equal to open and close";
    }
    if (bar.high < bar.low) {
        return "high must be greater than or equal to low";
    }
    return std::nullopt;
}

std::optional<std::string> validateBars(const Bars& bars) {
    if (bars.empty()) {
        return "the series contains no bars";
    }

    std::int64_t previousTimestamp{};
    for (std::size_t index = 0; index < bars.size(); ++index) {
        if (const auto error = validateBar(bars[index])) {
            return "bar " + std::to_string(index + 1) + ": " + *error;
        }
        if (index > 0 && bars[index].timestamp <= previousTimestamp) {
            return "bar " + std::to_string(index + 1) +
                   ": timestamps must be strictly increasing";
        }
        previousTimestamp = bars[index].timestamp;
    }
    return std::nullopt;
}

std::size_t completedBarCount(
    const Bars& bars,
    const Timeframe timeframe,
    const std::int64_t asOfUtc) noexcept {
    const auto duration = timeframeSeconds(timeframe);
    if (asOfUtc <= duration) {
        return 0;
    }
    const auto latestCompletedStart = asOfUtc - duration;
    const auto end = std::upper_bound(
        bars.begin(),
        bars.end(),
        latestCompletedStart,
        [](const std::int64_t timestamp, const Bar& bar) {
            return timestamp < bar.timestamp;
        });
    return static_cast<std::size_t>(std::distance(bars.begin(), end));
}

} // namespace tvchart
