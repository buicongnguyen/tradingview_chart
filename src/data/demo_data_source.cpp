#include "data/demo_data_source.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <random>
#include <string>

namespace tvchart {
namespace {

[[nodiscard]] std::uint64_t fnv1a(const std::string_view value) noexcept {
    std::uint64_t hash = 14695981039346656037ULL;
    for (const unsigned char character : value) {
        hash ^= character;
        hash *= 1099511628211ULL;
    }
    return hash;
}

[[nodiscard]] double roundPrice(const double value) {
    return std::round(value * 100.0) / 100.0;
}

} // namespace

Bars DemoDataSource::generate(
    const std::string_view symbol,
    const Timeframe timeframe,
    const std::size_t count,
    const std::int64_t endTimestamp) {
    if (count == 0 || endTimestamp <= 0) {
        return {};
    }

    const auto interval = timeframeSeconds(timeframe);
    const auto snappedEnd = endTimestamp - (endTimestamp % interval);
    const auto seed = fnv1a(symbol) ^ (static_cast<std::uint64_t>(timeframe) << 56U);
    std::mt19937_64 generator(seed);
    std::normal_distribution<double> returnDistribution(0.00015, 0.011);
    std::uniform_real_distribution<double> wickDistribution(0.001, 0.008);
    std::lognormal_distribution<double> volumeDistribution(13.2, 0.45);

    const auto base = 45.0 + static_cast<double>(seed % 26'000ULL) / 100.0;
    auto previousClose = base;

    Bars bars;
    bars.reserve(count);
    const auto firstTimestamp =
        snappedEnd - static_cast<std::int64_t>(count - 1) * interval;

    for (std::size_t index = 0; index < count; ++index) {
        const auto open = previousClose;
        const auto drift = std::clamp(returnDistribution(generator), -0.08, 0.08);
        const auto close = std::max(0.01, open * (1.0 + drift));
        const auto upperWick = std::max(open, close) * wickDistribution(generator);
        const auto lowerWick = std::min(open, close) * wickDistribution(generator);
        const auto high = std::max(open, close) + upperWick;
        const auto low = std::max(0.01, std::min(open, close) - lowerWick);

        bars.push_back(Bar{
            .timestamp = firstTimestamp + static_cast<std::int64_t>(index) * interval,
            .open = roundPrice(open),
            .high = roundPrice(high),
            .low = roundPrice(low),
            .close = roundPrice(close),
            .volume = std::round(volumeDistribution(generator)),
        });
        previousClose = bars.back().close;
    }

    return bars;
}

} // namespace tvchart
