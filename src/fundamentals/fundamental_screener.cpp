#include "fundamentals/fundamental_screener.hpp"

#include "analysis/technical_indicators.hpp"
#include "watchlists/watchlist_workspace.hpp"

#include <QJsonArray>
#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTimeZone>

#include <algorithm>
#include <array>
#include <cmath>
#include <exception>
#include <limits>
#include <numeric>
#include <ranges>

namespace tvchart {
namespace {

constexpr auto kMaximumPayloadBytes = qsizetype{64 * 1024};

constexpr std::array kFields{
    FundamentalScreenField::Price,
    FundamentalScreenField::MarketCapitalization,
    FundamentalScreenField::RevenueTtm,
    FundamentalScreenField::RevenueGrowthYoY,
    FundamentalScreenField::EpsTtm,
    FundamentalScreenField::EpsGrowthYoY,
    FundamentalScreenField::FreeCashFlowTtm,
    FundamentalScreenField::GrossMargin,
    FundamentalScreenField::OperatingMargin,
    FundamentalScreenField::NetMargin,
    FundamentalScreenField::DebtToEquity,
    FundamentalScreenField::ReturnOnEquity,
    FundamentalScreenField::PriceToEarnings,
    FundamentalScreenField::PriceToSales,
    FundamentalScreenField::PriceToBook,
    FundamentalScreenField::PriceToFreeCashFlow,
    FundamentalScreenField::Rsi14,
    FundamentalScreenField::DistanceFromSma50,
    FundamentalScreenField::Volatility20,
    FundamentalScreenField::DaysToEarnings,
    FundamentalScreenField::AnalystTargetUpside,
};

[[nodiscard]] std::optional<FundamentalScreenField> parseField(
    const QString& id) {
    for (const auto field : kFields) {
        if (fundamentalScreenFieldId(field) == id) {
            return field;
        }
    }
    return std::nullopt;
}

[[nodiscard]] std::optional<double> snapshotValue(
    const FundamentalSnapshot& snapshot,
    const FundamentalMetric metric) {
    const auto found =
        snapshot.trailingTwelveMonths.find(metric);
    return found == snapshot.trailingTwelveMonths.end()
               ? std::nullopt
               : std::optional<double>{found->second.value};
}

[[nodiscard]] std::optional<double> latestIndicator(
    const Bars& bars,
    const IndicatorSpec& spec) {
    if (bars.empty()) {
        return std::nullopt;
    }
    try {
        const auto calculation = calculateIndicator(bars, spec);
        return calculation.primary.empty()
                   ? std::nullopt
                   : std::optional<double>{
                         calculation.primary.back().value};
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

[[nodiscard]] std::optional<double> volatility20(
    const Bars& bars) {
    if (bars.size() < 21) {
        return std::nullopt;
    }
    std::vector<double> returns;
    returns.reserve(20);
    for (auto index = bars.size() - 20;
         index < bars.size();
         ++index) {
        const auto prior = bars[index - 1].close;
        if (prior <= 0.0) {
            return std::nullopt;
        }
        returns.push_back(bars[index].close / prior - 1.0);
    }
    const auto mean =
        std::accumulate(returns.begin(), returns.end(), 0.0) /
        static_cast<double>(returns.size());
    auto sum = 0.0;
    for (const auto value : returns) {
        const auto deviation = value - mean;
        sum += deviation * deviation;
    }
    return std::sqrt(
               sum / static_cast<double>(returns.size() - 1)) *
           std::sqrt(252.0) * 100.0;
}

[[nodiscard]] FundamentalScreenRow buildRow(
    const FundamentalScreenInput& input,
    const QDate& asOfDate) {
    FundamentalScreenRow row{
        .symbol = input.company.symbol,
    };
    if (validateBars(input.dailyBars) ||
        input.dailyBars.empty()) {
        row.unavailableReason =
            QStringLiteral("Compatible raw daily price history is unavailable.");
        return row;
    }
    Bars pointInTimeBars;
    pointInTimeBars.reserve(input.dailyBars.size());
    for (const auto& bar : input.dailyBars) {
        const auto priceDate =
            QDateTime::fromSecsSinceEpoch(
                bar.timestamp,
                QTimeZone::UTC)
                .date();
        if (priceDate > asOfDate) {
            break;
        }
        pointInTimeBars.push_back(bar);
    }
    if (pointInTimeBars.empty()) {
        row.unavailableReason =
            QStringLiteral("No price history exists on or before the as-of date.");
        return row;
    }
    const auto& latestBar = pointInTimeBars.back();
    row.priceDate =
        QDateTime::fromSecsSinceEpoch(
            latestBar.timestamp,
            QTimeZone::UTC)
            .date();
    auto snapshot = buildFundamentalSnapshot(
        input.company,
        asOfDate);
    if (!snapshot.ok()) {
        row.unavailableReason = snapshot.error;
        return row;
    }
    const auto priceCurrency =
        input.priceCurrency.trimmed().toUpper();
    const auto compatiblePriceCurrency =
        !snapshot.currency.isEmpty() &&
        !priceCurrency.isEmpty() &&
        snapshot.currency == priceCurrency;
    if (compatiblePriceCurrency) {
        snapshot = buildFundamentalSnapshot(
            input.company,
            asOfDate,
            latestBar.close);
    }
    row.latestFiledDate = snapshot.latestFiledDate;
    const auto assign =
        [&](const FundamentalScreenField field,
            const std::optional<double> value) {
            row.values.emplace(field, value);
        };
    assign(FundamentalScreenField::Price, latestBar.close);
    assign(
        FundamentalScreenField::MarketCapitalization,
        snapshot.derived.marketCapitalization);
    assign(
        FundamentalScreenField::RevenueTtm,
        snapshotValue(snapshot, FundamentalMetric::Revenue));
    assign(
        FundamentalScreenField::RevenueGrowthYoY,
        snapshot.derived.revenueGrowthYoYPercent);
    assign(
        FundamentalScreenField::EpsTtm,
        snapshotValue(snapshot, FundamentalMetric::DilutedEps));
    assign(
        FundamentalScreenField::EpsGrowthYoY,
        snapshot.derived.epsGrowthYoYPercent);
    assign(
        FundamentalScreenField::FreeCashFlowTtm,
        snapshot.derived.freeCashFlow);
    assign(
        FundamentalScreenField::GrossMargin,
        snapshot.derived.grossMarginPercent);
    assign(
        FundamentalScreenField::OperatingMargin,
        snapshot.derived.operatingMarginPercent);
    assign(
        FundamentalScreenField::NetMargin,
        snapshot.derived.netMarginPercent);
    assign(
        FundamentalScreenField::DebtToEquity,
        snapshot.derived.debtToEquity);
    assign(
        FundamentalScreenField::ReturnOnEquity,
        snapshot.derived.returnOnEquityPercent);
    assign(
        FundamentalScreenField::PriceToEarnings,
        snapshot.derived.priceToEarnings);
    assign(
        FundamentalScreenField::PriceToSales,
        snapshot.derived.priceToSales);
    assign(
        FundamentalScreenField::PriceToBook,
        snapshot.derived.priceToBook);
    assign(
        FundamentalScreenField::PriceToFreeCashFlow,
        snapshot.derived.priceToFreeCashFlow);

    auto rsiSpec = defaultIndicatorSpec(
        IndicatorKind::RelativeStrengthIndex);
    rsiSpec.period = 14;
    assign(
        FundamentalScreenField::Rsi14,
        latestIndicator(pointInTimeBars, rsiSpec));
    auto smaSpec = defaultIndicatorSpec(
        IndicatorKind::SimpleMovingAverage);
    smaSpec.period = 50;
    const auto sma = latestIndicator(pointInTimeBars, smaSpec);
    assign(
        FundamentalScreenField::DistanceFromSma50,
        sma && *sma > 0.0
            ? std::optional<double>{
                  (latestBar.close / *sma - 1.0) * 100.0}
            : std::nullopt);
    assign(
        FundamentalScreenField::Volatility20,
        volatility20(pointInTimeBars));

    std::optional<double> daysToEarnings;
    for (const auto& event : input.events) {
        if (event.type != ResearchEventType::Earnings ||
            normalizeWatchlistSymbol(event.symbol) !=
                input.company.symbol ||
            event.asOfUtc <= 0 ||
            QDateTime::fromSecsSinceEpoch(
                event.asOfUtc,
                QTimeZone::UTC)
                    .date() > asOfDate ||
            event.scheduledDate < asOfDate) {
            continue;
        }
        const auto days = asOfDate.daysTo(event.scheduledDate);
        if (!daysToEarnings || days < *daysToEarnings) {
            daysToEarnings = static_cast<double>(days);
        }
    }
    assign(
        FundamentalScreenField::DaysToEarnings,
        daysToEarnings);

    std::optional<double> targetUpside;
    if (latestBar.close > 0.0 &&
        !priceCurrency.isEmpty()) {
        std::map<QString, const AnalystTargetEstimate*> latest;
        for (const auto& estimate : input.targetEstimates) {
            if (estimate.scope != TargetEstimateScope::Organization ||
                normalizeWatchlistSymbol(estimate.symbol) !=
                    input.company.symbol ||
                estimate.publishedDate > asOfDate ||
                !validateTargetEstimate(estimate).isEmpty()) {
                continue;
            }
            const auto key =
                estimate.organization.toCaseFolded() +
                u'|' + estimate.currency.toUpper();
            const auto found = latest.find(key);
            if (found == latest.end() ||
                found->second->publishedDate <
                    estimate.publishedDate) {
                latest[key] = &estimate;
            }
        }
        auto total = 0.0;
        auto count = std::size_t{};
        for (const auto& [key, estimate] : latest) {
            static_cast<void>(key);
            if (
                estimate->currency.compare(
                    priceCurrency,
                    Qt::CaseInsensitive) != 0) {
                continue;
            }
            total += estimate->targetPrice;
            ++count;
        }
        if (count > 0) {
            targetUpside =
                (total / static_cast<double>(count) /
                     latestBar.close -
                 1.0) *
                100.0;
        }
    }
    assign(
        FundamentalScreenField::AnalystTargetUpside,
        targetUpside);
    return row;
}

} // namespace

QString fundamentalScreenFieldId(
    const FundamentalScreenField field) {
    switch (field) {
    case FundamentalScreenField::Price:
        return QStringLiteral("price");
    case FundamentalScreenField::MarketCapitalization:
        return QStringLiteral("market-cap");
    case FundamentalScreenField::RevenueTtm:
        return QStringLiteral("revenue-ttm");
    case FundamentalScreenField::RevenueGrowthYoY:
        return QStringLiteral("revenue-growth-yoy");
    case FundamentalScreenField::EpsTtm:
        return QStringLiteral("eps-ttm");
    case FundamentalScreenField::EpsGrowthYoY:
        return QStringLiteral("eps-growth-yoy");
    case FundamentalScreenField::FreeCashFlowTtm:
        return QStringLiteral("fcf-ttm");
    case FundamentalScreenField::GrossMargin:
        return QStringLiteral("gross-margin");
    case FundamentalScreenField::OperatingMargin:
        return QStringLiteral("operating-margin");
    case FundamentalScreenField::NetMargin:
        return QStringLiteral("net-margin");
    case FundamentalScreenField::DebtToEquity:
        return QStringLiteral("debt-equity");
    case FundamentalScreenField::ReturnOnEquity:
        return QStringLiteral("roe");
    case FundamentalScreenField::PriceToEarnings:
        return QStringLiteral("pe");
    case FundamentalScreenField::PriceToSales:
        return QStringLiteral("ps");
    case FundamentalScreenField::PriceToBook:
        return QStringLiteral("pb");
    case FundamentalScreenField::PriceToFreeCashFlow:
        return QStringLiteral("pfcf");
    case FundamentalScreenField::Rsi14:
        return QStringLiteral("rsi14");
    case FundamentalScreenField::DistanceFromSma50:
        return QStringLiteral("sma50-distance");
    case FundamentalScreenField::Volatility20:
        return QStringLiteral("volatility20");
    case FundamentalScreenField::DaysToEarnings:
        return QStringLiteral("days-to-earnings");
    case FundamentalScreenField::AnalystTargetUpside:
        return QStringLiteral("target-upside");
    }
    return QStringLiteral("unknown");
}

QString fundamentalScreenFieldLabel(
    const FundamentalScreenField field) {
    switch (field) {
    case FundamentalScreenField::Price:
        return QStringLiteral("Price");
    case FundamentalScreenField::MarketCapitalization:
        return QStringLiteral("Market cap");
    case FundamentalScreenField::RevenueTtm:
        return QStringLiteral("Revenue TTM");
    case FundamentalScreenField::RevenueGrowthYoY:
        return QStringLiteral("Revenue growth YoY %");
    case FundamentalScreenField::EpsTtm:
        return QStringLiteral("Diluted EPS TTM");
    case FundamentalScreenField::EpsGrowthYoY:
        return QStringLiteral("EPS growth YoY %");
    case FundamentalScreenField::FreeCashFlowTtm:
        return QStringLiteral("Free cash flow TTM");
    case FundamentalScreenField::GrossMargin:
        return QStringLiteral("Gross margin %");
    case FundamentalScreenField::OperatingMargin:
        return QStringLiteral("Operating margin %");
    case FundamentalScreenField::NetMargin:
        return QStringLiteral("Net margin %");
    case FundamentalScreenField::DebtToEquity:
        return QStringLiteral("Long-term debt / equity");
    case FundamentalScreenField::ReturnOnEquity:
        return QStringLiteral("Return on equity %");
    case FundamentalScreenField::PriceToEarnings:
        return QStringLiteral("P/E");
    case FundamentalScreenField::PriceToSales:
        return QStringLiteral("P/S");
    case FundamentalScreenField::PriceToBook:
        return QStringLiteral("P/B");
    case FundamentalScreenField::PriceToFreeCashFlow:
        return QStringLiteral("P/FCF");
    case FundamentalScreenField::Rsi14:
        return QStringLiteral("RSI (14)");
    case FundamentalScreenField::DistanceFromSma50:
        return QStringLiteral("Distance from SMA50 %");
    case FundamentalScreenField::Volatility20:
        return QStringLiteral("20-day annualized volatility %");
    case FundamentalScreenField::DaysToEarnings:
        return QStringLiteral("Days to earnings");
    case FundamentalScreenField::AnalystTargetUpside:
        return QStringLiteral("Organization target upside %");
    }
    return QStringLiteral("Unknown");
}

QString fundamentalScreenOperatorLabel(
    const FundamentalScreenOperator comparison) {
    return comparison ==
                   FundamentalScreenOperator::GreaterThanOrEqual
               ? QStringLiteral("≥")
               : QStringLiteral("≤");
}

QString validateFundamentalScreen(
    const FundamentalScreenDefinition& definition) {
    if (definition.name.trimmed().isEmpty() ||
        definition.name.size() > 120 ||
        definition.conditions.size() >
            FundamentalScreenDefinition::maximumConditions) {
        return QStringLiteral(
            "Fundamental screen identity or condition count is invalid.");
    }
    if (!parseField(
             fundamentalScreenFieldId(definition.sortField))) {
        return QStringLiteral(
            "Fundamental screen sort field is invalid.");
    }
    for (const auto& condition : definition.conditions) {
        if (!parseField(
                fundamentalScreenFieldId(condition.field)) ||
            (condition.comparison !=
                 FundamentalScreenOperator::GreaterThanOrEqual &&
             condition.comparison !=
                 FundamentalScreenOperator::LessThanOrEqual)) {
            return QStringLiteral(
                "Fundamental screen condition is invalid.");
        }
        if (!std::isfinite(condition.threshold) ||
            std::abs(condition.threshold) > 1.0e20) {
            return QStringLiteral(
                "Fundamental screen threshold is invalid.");
        }
    }
    return {};
}

FundamentalScreenResult runFundamentalScreen(
    const FundamentalScreenDefinition& definition,
    const std::vector<FundamentalScreenInput>& inputs,
    const QDate& asOfDate) {
    FundamentalScreenResult result;
    if (const auto error = validateFundamentalScreen(definition);
        !error.isEmpty() || !asOfDate.isValid()) {
        result.error =
            error.isEmpty()
                ? QStringLiteral(
                      "Fundamental screen as-of date is invalid.")
                : error;
        return result;
    }
    if (inputs.size() > 2'000) {
        result.error = QStringLiteral(
            "Fundamental screen exceeds the 2,000-symbol safety limit.");
        return result;
    }
    result.rows.reserve(inputs.size());
    for (const auto& input : inputs) {
        auto row = buildRow(input, asOfDate);
        row.matched = row.unavailableReason.isEmpty();
        for (const auto& condition : definition.conditions) {
            const auto found = row.values.find(condition.field);
            if (found == row.values.end() || !found->second) {
                row.matched = false;
                if (row.unavailableReason.isEmpty()) {
                    row.unavailableReason =
                        QStringLiteral("%1 is unavailable.")
                            .arg(
                                fundamentalScreenFieldLabel(
                                    condition.field));
                }
                break;
            }
            const auto value = *found->second;
            const auto matched =
                condition.comparison ==
                        FundamentalScreenOperator::GreaterThanOrEqual
                    ? value >= condition.threshold
                    : value <= condition.threshold;
            if (!matched) {
                row.matched = false;
                break;
            }
        }
        result.rows.push_back(std::move(row));
    }
    std::ranges::stable_sort(
        result.rows,
        [&](const FundamentalScreenRow& left,
            const FundamentalScreenRow& right) {
            if (left.matched != right.matched) {
                return left.matched;
            }
            const auto leftValue =
                left.values.contains(definition.sortField)
                    ? left.values.at(definition.sortField)
                    : std::nullopt;
            const auto rightValue =
                right.values.contains(definition.sortField)
                    ? right.values.at(definition.sortField)
                    : std::nullopt;
            if (leftValue.has_value() != rightValue.has_value()) {
                return leftValue.has_value();
            }
            if (leftValue && rightValue &&
                *leftValue != *rightValue) {
                return definition.sortDescending
                           ? *leftValue > *rightValue
                           : *leftValue < *rightValue;
            }
            return left.symbol < right.symbol;
        });
    return result;
}

QByteArray serializeFundamentalScreen(
    const FundamentalScreenDefinition& definition) {
    if (!validateFundamentalScreen(definition).isEmpty()) {
        return {};
    }
    QJsonArray conditions;
    for (const auto& condition : definition.conditions) {
        conditions.append(QJsonObject{
            {
                QStringLiteral("field"),
                fundamentalScreenFieldId(condition.field),
            },
            {
                QStringLiteral("comparison"),
                condition.comparison ==
                        FundamentalScreenOperator::GreaterThanOrEqual
                    ? QStringLiteral("gte")
                    : QStringLiteral("lte"),
            },
            {
                QStringLiteral("threshold"),
                condition.threshold,
            },
        });
    }
    const auto payload = QJsonDocument(QJsonObject{
        {QStringLiteral("schemaVersion"), 1},
        {QStringLiteral("name"), definition.name},
        {
            QStringLiteral("sortField"),
            fundamentalScreenFieldId(definition.sortField),
        },
        {
            QStringLiteral("sortDescending"),
            definition.sortDescending,
        },
        {QStringLiteral("conditions"), conditions},
    }).toJson(QJsonDocument::Compact);
    return payload.size() <= kMaximumPayloadBytes
               ? payload
               : QByteArray{};
}

FundamentalScreenLoadResult deserializeFundamentalScreen(
    const QByteArray& payload) {
    if (payload.size() > kMaximumPayloadBytes) {
        return {.error = QStringLiteral(
                    "Saved fundamental screen exceeds the size limit.")};
    }
    const auto document = QJsonDocument::fromJson(payload);
    if (!document.isObject()) {
        return {.error = QStringLiteral(
                    "Saved fundamental screen is invalid JSON.")};
    }
    const auto object = document.object();
    if (object.value(QStringLiteral("schemaVersion")).toInt(-1) != 1) {
        return {.error = QStringLiteral(
                    "Saved fundamental screen schema is unsupported.")};
    }
    const auto sortField = parseField(
        object.value(QStringLiteral("sortField")).toString());
    const auto values =
        object.value(QStringLiteral("conditions")).toArray();
    if (!sortField ||
        values.size() >
            static_cast<qsizetype>(
                FundamentalScreenDefinition::maximumConditions)) {
        return {.error = QStringLiteral(
                    "Saved fundamental screen fields are invalid.")};
    }
    FundamentalScreenDefinition definition{
        .name =
            object.value(QStringLiteral("name"))
                .toString()
                .trimmed(),
        .sortField = *sortField,
        .sortDescending =
            object.value(QStringLiteral("sortDescending"))
                .toBool(true),
    };
    for (const auto& value : values) {
        const auto condition = value.toObject();
        const auto field = parseField(
            condition.value(QStringLiteral("field")).toString());
        const auto comparison =
            condition.value(QStringLiteral("comparison")).toString();
        const auto threshold =
            condition.value(QStringLiteral("threshold"))
                .toDouble(
                    std::numeric_limits<double>::quiet_NaN());
        if (!field ||
            (comparison != QStringLiteral("gte") &&
             comparison != QStringLiteral("lte")) ||
            !std::isfinite(threshold)) {
            return {.error = QStringLiteral(
                        "Saved fundamental screen condition is invalid.")};
        }
        definition.conditions.push_back({
            .field = *field,
            .comparison =
                comparison == QStringLiteral("gte")
                    ? FundamentalScreenOperator::GreaterThanOrEqual
                    : FundamentalScreenOperator::LessThanOrEqual,
            .threshold = threshold,
        });
    }
    if (const auto error = validateFundamentalScreen(definition);
        !error.isEmpty()) {
        return {.error = error};
    }
    return {.definition = std::move(definition)};
}

} // namespace tvchart
