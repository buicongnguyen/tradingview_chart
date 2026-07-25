#include "data/demo_data_source.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
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

[[nodiscard]] std::uint64_t mix(std::uint64_t value) noexcept {
    value += 0x9E3779B97F4A7C15ULL;
    value = (value ^ (value >> 30U)) * 0xBF58476D1CE4E5B9ULL;
    value = (value ^ (value >> 27U)) * 0x94D049BB133111EBULL;
    return value ^ (value >> 31U);
}

[[nodiscard]] double unitValue(
    const std::uint64_t seed,
    const std::int64_t bucket,
    const std::uint64_t stream) noexcept {
    const auto bits =
        mix(seed ^ static_cast<std::uint64_t>(bucket) ^ stream) >> 11U;
    return static_cast<double>(bits) * (1.0 / 9'007'199'254'740'992.0);
}

[[nodiscard]] double priceAt(
    const std::uint64_t seed,
    const double base,
    const std::int64_t bucket) {
    const auto position = static_cast<double>(bucket);
    const auto phase =
        static_cast<double>(seed % 6'283ULL) / 1'000.0;
    const auto cycle =
        0.10 * std::sin(position * 0.017 + phase) +
        0.04 * std::sin(position * 0.0037 + phase * 0.37);
    const auto noise =
        (unitValue(seed, bucket, 0xA0761D6478BD642FULL) - 0.5) * 0.018;
    return std::max(0.01, base * (1.0 + cycle + noise));
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
    const auto maximumCount =
        static_cast<std::uint64_t>(snappedEnd / interval);
    if (snappedEnd <= 0 ||
        static_cast<std::uint64_t>(count) > maximumCount) {
        return {};
    }
    const auto seed = fnv1a(symbol) ^ (static_cast<std::uint64_t>(timeframe) << 56U);
    const auto base = 45.0 + static_cast<double>(seed % 26'000ULL) / 100.0;

    Bars bars;
    bars.reserve(count);
    const auto firstTimestamp =
        snappedEnd - static_cast<std::int64_t>(count - 1) * interval;

    for (std::size_t index = 0; index < count; ++index) {
        const auto timestamp =
            firstTimestamp + static_cast<std::int64_t>(index) * interval;
        const auto bucket = timestamp / interval;
        const auto open = roundPrice(priceAt(seed, base, bucket - 1));
        const auto close = roundPrice(priceAt(seed, base, bucket));
        const auto upperWick =
            std::max(open, close) *
            (0.001 + 0.007 * unitValue(seed, bucket, 0xE7037ED1A0B428DBULL));
        const auto lowerWick =
            std::min(open, close) *
            (0.001 + 0.007 * unitValue(seed, bucket, 0x8EBC6AF09C88C6E3ULL));
        const auto volume =
            250'000.0 +
            1'750'000.0 * unitValue(seed, bucket, 0x589965CC75374CC3ULL);

        bars.push_back(Bar{
            .timestamp = timestamp,
            .open = open,
            .high = roundPrice(std::max(open, close) + upperWick),
            .low = roundPrice(
                std::max(0.01, std::min(open, close) - lowerWick)),
            .close = close,
            .volume = std::round(volume),
        });
    }

    return bars;
}

} // namespace tvchart
