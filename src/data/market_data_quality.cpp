#include "data/market_data_quality.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <ranges>

namespace tvchart {
namespace {

constexpr auto kMaximumActionValue = 1.0e12;
constexpr auto kMaximumSplitRatio = 1.0e6;
constexpr auto kOutlierReturn = 0.40;

void addIssue(
    MarketDataQualityReport& report,
    QString code,
    const DataQualitySeverity severity,
    const std::int64_t timestamp,
    QString detail) {
    if (report.issues.size() >= MarketDataQualityReport::maximumIssues) {
        return;
    }
    report.issues.push_back({
        .code = std::move(code),
        .severity = severity,
        .timestamp = timestamp,
        .detail = std::move(detail),
    });
}

[[nodiscard]] bool splitNear(
    const std::vector<CorporateAction>& actions,
    const std::int64_t timestamp,
    const std::int64_t tolerance) {
    return std::ranges::any_of(
        actions,
        [&](const CorporateAction& action) {
            return action.type == CorporateActionType::StockSplit &&
                   std::abs(action.timestamp - timestamp) <= tolerance;
        });
}

[[nodiscard]] std::size_t estimatedGapCount(
    const std::int64_t delta,
    const std::int64_t expected) {
    if (expected <= 0 || delta <= expected) {
        return 0;
    }
    return static_cast<std::size_t>(
        std::max<std::int64_t>(1, delta / expected - 1));
}

} // namespace

QString priceAdjustmentModeId(const PriceAdjustmentMode mode) {
    switch (mode) {
    case PriceAdjustmentMode::Raw:
        return QStringLiteral("raw");
    case PriceAdjustmentMode::SplitAdjusted:
        return QStringLiteral("split-adjusted");
    case PriceAdjustmentMode::TotalReturn:
        return QStringLiteral("total-return");
    }
    return QStringLiteral("raw");
}

QString priceAdjustmentModeLabel(const PriceAdjustmentMode mode) {
    switch (mode) {
    case PriceAdjustmentMode::Raw:
        return QStringLiteral("Raw");
    case PriceAdjustmentMode::SplitAdjusted:
        return QStringLiteral("Split adjusted");
    case PriceAdjustmentMode::TotalReturn:
        return QStringLiteral("Total return adjusted");
    }
    return QStringLiteral("Raw");
}

QString dataQualityGradeLabel(const DataQualityGrade grade) {
    switch (grade) {
    case DataQualityGrade::Good:
        return QStringLiteral("Good");
    case DataQualityGrade::Caution:
        return QStringLiteral("Caution");
    case DataQualityGrade::Poor:
        return QStringLiteral("Poor");
    }
    return QStringLiteral("Poor");
}

QString dataQualitySeverityLabel(const DataQualitySeverity severity) {
    switch (severity) {
    case DataQualitySeverity::Information:
        return QStringLiteral("Information");
    case DataQualitySeverity::Warning:
        return QStringLiteral("Warning");
    case DataQualitySeverity::Error:
        return QStringLiteral("Error");
    }
    return QStringLiteral("Error");
}

QString validateCorporateAction(const CorporateAction& action) {
    if (action.timestamp <= 0 || action.provider.trimmed().isEmpty() ||
        action.provider.size() > 120 || action.currency.size() > 12 ||
        !std::isfinite(action.amount) ||
        !std::isfinite(action.numerator) ||
        !std::isfinite(action.denominator)) {
        return QStringLiteral("Corporate-action provenance or values are invalid.");
    }
    if (action.type == CorporateActionType::CashDividend) {
        if (action.amount <= 0.0 || action.amount > kMaximumActionValue ||
            action.numerator != 0.0 || action.denominator != 0.0) {
            return QStringLiteral("Cash-dividend fields are invalid.");
        }
    } else if (
        action.numerator <= 0.0 ||
        action.numerator > kMaximumSplitRatio ||
        action.denominator <= 0.0 ||
        action.denominator > kMaximumSplitRatio ||
        action.amount != 0.0) {
        return QStringLiteral("Stock-split fields are invalid.");
    }
    return {};
}

PriceAdjustmentResult applyPriceAdjustment(
    const Bars& rawBars,
    const std::vector<CorporateAction>& actions,
    const std::vector<AdjustedClosePoint>& adjustedCloses,
    const PriceAdjustmentMode requestedMode) {
    PriceAdjustmentResult result{
        .bars = rawBars,
        .appliedMode = PriceAdjustmentMode::Raw,
    };
    if (const auto error = validateBars(rawBars)) {
        result.error =
            QStringLiteral("Cannot adjust invalid raw bars: %1")
                .arg(QString::fromStdString(*error));
        result.bars.clear();
        return result;
    }
    for (const auto& action : actions) {
        if (const auto error = validateCorporateAction(action);
            !error.isEmpty()) {
            result.error = error;
            result.bars.clear();
            return result;
        }
    }
    if (requestedMode == PriceAdjustmentMode::Raw) {
        return result;
    }

    if (requestedMode == PriceAdjustmentMode::SplitAdjusted) {
        for (const auto& action : actions) {
            if (action.type != CorporateActionType::StockSplit) {
                continue;
            }
            const auto priceFactor =
                action.denominator / action.numerator;
            const auto volumeFactor =
                action.numerator / action.denominator;
            for (auto& bar : result.bars) {
                if (bar.timestamp >= action.timestamp) {
                    continue;
                }
                bar.open *= priceFactor;
                bar.high *= priceFactor;
                bar.low *= priceFactor;
                bar.close *= priceFactor;
                bar.volume *= volumeFactor;
            }
        }
        if (const auto error = validateBars(result.bars)) {
            result.error =
                QStringLiteral("Split adjustment produced invalid bars: %1")
                    .arg(QString::fromStdString(*error));
            result.bars.clear();
            return result;
        }
        result.appliedMode = PriceAdjustmentMode::SplitAdjusted;
        return result;
    }

    std::map<std::int64_t, double> adjustedByTimestamp;
    for (const auto& point : adjustedCloses) {
        if (point.timestamp <= 0 ||
            !std::isfinite(point.adjustedClose) ||
            point.adjustedClose <= 0.0) {
            continue;
        }
        adjustedByTimestamp[point.timestamp] = point.adjustedClose;
    }
    if (adjustedByTimestamp.size() != rawBars.size()) {
        result.warning =
            QStringLiteral(
                "Total-return adjustment was unavailable or incomplete; raw "
                "prices are shown.");
        return result;
    }
    for (std::size_t index = 0; index < result.bars.size(); ++index) {
        auto& bar = result.bars[index];
        const auto found = adjustedByTimestamp.find(bar.timestamp);
        if (found == adjustedByTimestamp.end() || bar.close <= 0.0) {
            result.warning =
                QStringLiteral(
                    "Total-return adjustment was incomplete; raw prices are "
                    "shown.");
            result.bars = rawBars;
            return result;
        }
        const auto factor = found->second / bar.close;
        if (!std::isfinite(factor) || factor <= 0.0 ||
            factor > kMaximumSplitRatio) {
            result.warning =
                QStringLiteral(
                    "Total-return adjustment contained an invalid factor; raw "
                    "prices are shown.");
            result.bars = rawBars;
            return result;
        }
        bar.open *= factor;
        bar.high *= factor;
        bar.low *= factor;
        bar.close = found->second;
    }
    if (const auto error = validateBars(result.bars)) {
        result.warning =
            QStringLiteral(
                "Total-return adjustment failed validation; raw prices are "
                "shown.");
        result.bars = rawBars;
        return result;
    }
    result.appliedMode = PriceAdjustmentMode::TotalReturn;
    return result;
}

MarketDataQualityReport analyzeMarketDataQuality(
    const Bars& bars,
    const Timeframe timeframe,
    const std::size_t inputRows,
    const std::size_t rejectedRows,
    const std::size_t duplicateRows,
    const std::vector<CorporateAction>& actions) {
    MarketDataQualityReport report{
        .inputRows = inputRows,
        .acceptedRows = bars.size(),
        .rejectedRows = rejectedRows,
        .duplicateRows = duplicateRows,
    };
    if (const auto error = validateBars(bars)) {
        report.grade = DataQualityGrade::Poor;
        addIssue(
            report,
            QStringLiteral("invalid-series"),
            DataQualitySeverity::Error,
            0,
            QString::fromStdString(*error));
        return report;
    }
    if (rejectedRows > 0) {
        addIssue(
            report,
            QStringLiteral("rejected-provider-rows"),
            DataQualitySeverity::Warning,
            0,
            QStringLiteral("%1 provider row(s) were unusable.")
                .arg(static_cast<qulonglong>(rejectedRows)));
    }
    if (duplicateRows > 0) {
        addIssue(
            report,
            QStringLiteral("duplicate-provider-rows"),
            DataQualitySeverity::Information,
            0,
            QStringLiteral("%1 identical duplicate row(s) were collapsed.")
                .arg(static_cast<qulonglong>(duplicateRows)));
    }

    const auto expected = timeframeSeconds(timeframe);
    for (std::size_t index = 0; index < bars.size(); ++index) {
        if (bars[index].volume == 0.0) {
            ++report.zeroVolumeBars;
        }
        if (index == 0) {
            continue;
        }
        const auto delta =
            bars[index].timestamp - bars[index - 1].timestamp;
        bool suspicious = false;
        if (timeframe == Timeframe::OneDay) {
            suspicious = delta > 4 * expected;
        } else {
            suspicious =
                delta > expected + expected / 2 &&
                delta < 6 * 60 * 60;
        }
        if (suspicious) {
            report.suspiciousGaps += estimatedGapCount(delta, expected);
            addIssue(
                report,
                QStringLiteral("irregular-short-gap"),
                DataQualitySeverity::Warning,
                bars[index].timestamp,
                QStringLiteral(
                    "The interval from the previous bar is %1 seconds; an "
                    "exchange calendar was not assumed.")
                    .arg(delta));
        }

        const auto previousClose = bars[index - 1].close;
        const auto priceReturn =
            previousClose > 0.0
                ? bars[index].close / previousClose - 1.0
                : 0.0;
        const auto splitTolerance =
            std::max<std::int64_t>(2 * expected, 24 * 60 * 60);
        if (std::abs(priceReturn) > kOutlierReturn &&
            !splitNear(
                actions,
                bars[index].timestamp,
                splitTolerance)) {
            ++report.outlierBars;
            addIssue(
                report,
                QStringLiteral("extreme-unexplained-return"),
                DataQualitySeverity::Warning,
                bars[index].timestamp,
                QStringLiteral(
                    "Close-to-close move of %1% has no nearby parsed split.")
                    .arg(priceReturn * 100.0, 0, 'f', 2));
        }
    }
    if (report.zeroVolumeBars > 0) {
        addIssue(
            report,
            QStringLiteral("zero-volume"),
            DataQualitySeverity::Information,
            0,
            QStringLiteral(
                "%1 bar(s) have zero or unavailable centralized volume.")
                .arg(static_cast<qulonglong>(report.zeroVolumeBars)));
    }

    const auto rejectedRatio =
        inputRows > 0
            ? static_cast<double>(rejectedRows) /
                  static_cast<double>(inputRows)
            : 0.0;
    if (bars.empty() || rejectedRatio > 0.10 ||
        report.outlierBars > 2 ||
        report.suspiciousGaps > std::max<std::size_t>(5, bars.size() / 20)) {
        report.grade = DataQualityGrade::Poor;
    } else if (
        rejectedRows > 0 || report.outlierBars > 0 ||
        report.suspiciousGaps > 0) {
        report.grade = DataQualityGrade::Caution;
    }
    return report;
}

} // namespace tvchart
