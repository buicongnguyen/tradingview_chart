#pragma once

#include "data/market_data_quality.hpp"
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
    PriceAdjustmentMode requestedAdjustmentMode{PriceAdjustmentMode::Raw};
    PriceAdjustmentMode appliedAdjustmentMode{PriceAdjustmentMode::Raw};
    std::size_t corporateActionCount{};
    MarketDataQualityReport quality;
    QString adjustmentWarning;
};

struct MarketDataParseResult {
    Bars bars;
    MarketDataMetadata metadata;
    std::vector<CorporateAction> corporateActions;
    std::vector<AdjustedClosePoint> adjustedCloses;
    std::size_t inputRows{};
    std::size_t rejectedRows{};
    std::size_t duplicateRows{};
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
