#pragma once

#include "fundamentals/fundamental_models.hpp"

#include <QDate>
#include <QString>

#include <map>
#include <optional>
#include <vector>

namespace tvchart {

struct FundamentalSeriesPoint {
    QDate periodStart;
    QDate periodEnd;
    QDate filedDate;
    double value{};
    QString unit;
    bool derived{};
    QString provenance;

    [[nodiscard]] bool operator==(
        const FundamentalSeriesPoint&) const = default;
};

struct FundamentalDerivedMetrics {
    std::optional<double> revenueGrowthYoYPercent;
    std::optional<double> epsGrowthYoYPercent;
    std::optional<double> grossMarginPercent;
    std::optional<double> operatingMarginPercent;
    std::optional<double> netMarginPercent;
    std::optional<double> freeCashFlow;
    std::optional<double> freeCashFlowMarginPercent;
    std::optional<double> debtToEquity;
    std::optional<double> returnOnEquityPercent;
    std::optional<double> marketCapitalization;
    std::optional<double> priceToEarnings;
    std::optional<double> priceToSales;
    std::optional<double> priceToBook;
    std::optional<double> priceToFreeCashFlow;
};

struct FundamentalSnapshot {
    QString symbol;
    QDate asOfDate;
    QDate latestFiledDate;
    QString currency;
    std::map<FundamentalMetric, FundamentalSeriesPoint> latestAnnual;
    std::map<FundamentalMetric, FundamentalSeriesPoint> latestQuarter;
    std::map<FundamentalMetric, FundamentalSeriesPoint> trailingTwelveMonths;
    FundamentalDerivedMetrics derived;
    QString warning;
    QString error;

    [[nodiscard]] bool ok() const noexcept { return error.isEmpty(); }
};

struct DcfAssumptions {
    double annualGrowthPercent{5.0};
    double discountRatePercent{10.0};
    double terminalGrowthPercent{2.5};
    int forecastYears{5};
};

struct DcfReport {
    double startingFreeCashFlow{};
    double presentValueForecast{};
    double presentValueTerminal{};
    double enterpriseValue{};
    double netDebt{};
    double equityValue{};
    double shares{};
    double valuePerShare{};
    std::optional<double> impliedAnnualGrowthPercent;
    QString currency;
    QString error;

    [[nodiscard]] bool ok() const noexcept { return error.isEmpty(); }
};

[[nodiscard]] std::vector<FundamentalSeriesPoint>
fundamentalSeries(
    const std::vector<FundamentalFact>& facts,
    FundamentalMetric metric,
    FundamentalPeriodMode mode,
    const QDate& asOfDate);

[[nodiscard]] FundamentalSnapshot buildFundamentalSnapshot(
    const FundamentalCompany& company,
    const QDate& asOfDate,
    std::optional<double> currentPrice = std::nullopt);

[[nodiscard]] DcfReport calculateDcf(
    const FundamentalSnapshot& snapshot,
    const DcfAssumptions& assumptions,
    std::optional<double> currentPrice = std::nullopt);

} // namespace tvchart
