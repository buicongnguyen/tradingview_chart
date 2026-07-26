#pragma once

#include "data/market_data_parser.hpp"
#include "domain/bar.hpp"

#include <QString>

#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

namespace tvchart {

struct HistoricalSeriesKey {
    QString provider;
    QString symbol;
    Timeframe timeframe{Timeframe::FiveMinutes};

    [[nodiscard]] bool operator==(const HistoricalSeriesKey&) const = default;
};

struct CachedHistoricalSeries {
    HistoricalSeriesKey key;
    Bars bars;
    MarketDataMetadata metadata;
    std::int64_t cachedAtUtcMilliseconds{};
    QString error;

    [[nodiscard]] bool ok() const noexcept {
        return error.isEmpty() && !bars.empty();
    }
};

struct CachedSeriesSummary {
    HistoricalSeriesKey key;
    MarketDataMetadata metadata;
    std::size_t barCount{};
    std::int64_t firstTimestamp{};
    std::int64_t lastTimestamp{};
    std::int64_t cachedAtUtcMilliseconds{};
};

class HistoricalDataStore final {
public:
    explicit HistoricalDataStore(QString databasePath);
    ~HistoricalDataStore();

    HistoricalDataStore(const HistoricalDataStore&) = delete;
    HistoricalDataStore& operator=(const HistoricalDataStore&) = delete;
    HistoricalDataStore(HistoricalDataStore&&) = delete;
    HistoricalDataStore& operator=(HistoricalDataStore&&) = delete;

    [[nodiscard]] bool open();
    [[nodiscard]] bool isOpen() const noexcept;
    [[nodiscard]] QString lastError() const;

    [[nodiscard]] QString upsertSeries(
        QString provider,
        QString symbol,
        Timeframe timeframe,
        const Bars& bars,
        const MarketDataMetadata& metadata);

    [[nodiscard]] CachedHistoricalSeries loadSeries(
        QString provider,
        QString symbol,
        Timeframe timeframe,
        std::optional<std::int64_t> fromTimestamp = std::nullopt,
        std::optional<std::int64_t> throughTimestamp = std::nullopt) const;

    [[nodiscard]] CachedHistoricalSeries loadLatestSeries(
        QString symbol,
        Timeframe timeframe) const;

    [[nodiscard]] std::vector<CachedSeriesSummary> availableSeries() const;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace tvchart
