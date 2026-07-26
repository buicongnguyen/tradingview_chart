#pragma once

#include "domain/bar.hpp"

#include <QString>

#include <cstddef>
#include <cstdint>
#include <vector>

namespace tvchart {

enum class PriceAdjustmentMode : std::uint8_t {
    Raw,
    SplitAdjusted,
    TotalReturn,
};

enum class CorporateActionType : std::uint8_t {
    CashDividend,
    StockSplit,
};

struct CorporateAction {
    CorporateActionType type{CorporateActionType::CashDividend};
    std::int64_t timestamp{};
    double amount{};
    double numerator{};
    double denominator{};
    QString currency;
    QString provider;

    [[nodiscard]] bool operator==(const CorporateAction&) const = default;
};

struct AdjustedClosePoint {
    std::int64_t timestamp{};
    double adjustedClose{};

    [[nodiscard]] bool operator==(const AdjustedClosePoint&) const = default;
};

enum class DataQualityGrade : std::uint8_t {
    Good,
    Caution,
    Poor,
};

enum class DataQualitySeverity : std::uint8_t {
    Information,
    Warning,
    Error,
};

struct DataQualityIssue {
    QString code;
    DataQualitySeverity severity{DataQualitySeverity::Information};
    std::int64_t timestamp{};
    QString detail;

    [[nodiscard]] bool operator==(const DataQualityIssue&) const = default;
};

struct MarketDataQualityReport {
    static constexpr std::size_t maximumIssues{32};

    DataQualityGrade grade{DataQualityGrade::Good};
    std::size_t inputRows{};
    std::size_t acceptedRows{};
    std::size_t rejectedRows{};
    std::size_t duplicateRows{};
    std::size_t suspiciousGaps{};
    std::size_t outlierBars{};
    std::size_t zeroVolumeBars{};
    std::vector<DataQualityIssue> issues;

    [[nodiscard]] bool operator==(const MarketDataQualityReport&) const =
        default;
};

struct PriceAdjustmentResult {
    Bars bars;
    PriceAdjustmentMode appliedMode{PriceAdjustmentMode::Raw};
    QString warning;
    QString error;

    [[nodiscard]] bool ok() const noexcept {
        return error.isEmpty() && !bars.empty();
    }
};

[[nodiscard]] QString priceAdjustmentModeId(PriceAdjustmentMode mode);
[[nodiscard]] QString priceAdjustmentModeLabel(PriceAdjustmentMode mode);
[[nodiscard]] QString dataQualityGradeLabel(DataQualityGrade grade);
[[nodiscard]] QString dataQualitySeverityLabel(DataQualitySeverity severity);
[[nodiscard]] QString validateCorporateAction(const CorporateAction& action);
[[nodiscard]] PriceAdjustmentResult applyPriceAdjustment(
    const Bars& rawBars,
    const std::vector<CorporateAction>& actions,
    const std::vector<AdjustedClosePoint>& adjustedCloses,
    PriceAdjustmentMode requestedMode);
[[nodiscard]] MarketDataQualityReport analyzeMarketDataQuality(
    const Bars& bars,
    Timeframe timeframe,
    std::size_t inputRows,
    std::size_t rejectedRows,
    std::size_t duplicateRows,
    const std::vector<CorporateAction>& actions);

} // namespace tvchart
