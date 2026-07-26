#pragma once

#include "domain/bar.hpp"

#include <QByteArray>
#include <QString>

#include <cstdint>
#include <optional>

namespace tvchart {

enum class DataDeliveryMode {
    Polled,
    LocalFile,
    Synthetic,
};

struct MarketDataMetadata {
    DataDeliveryMode deliveryMode{DataDeliveryMode::Polled};
    QString exchange;
    QString currency;
    QString timezone;
    QString instrumentType;
    QString interval;
    std::optional<int> exchangeDelayMinutes;
    std::int64_t retrievedAtUtc{};
};

struct MarketDataParseResult {
    Bars bars;
    MarketDataMetadata metadata;
    QString error;

    [[nodiscard]] bool ok() const noexcept {
        return error.isEmpty() && !bars.empty();
    }
};

class MarketDataParser final {
public:
    [[nodiscard]] static MarketDataParseResult parseYahoo(const QByteArray& payload);
    [[nodiscard]] static MarketDataParseResult parseTwelveData(const QByteArray& payload);
};

} // namespace tvchart
