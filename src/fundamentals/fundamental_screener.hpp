#pragma once

#include "domain/bar.hpp"
#include "fundamentals/fundamental_analysis.hpp"
#include "research/research_models.hpp"

#include <QByteArray>
#include <QDate>
#include <QString>

#include <map>
#include <optional>
#include <vector>

namespace tvchart {

enum class FundamentalScreenField : std::uint8_t {
    Price,
    MarketCapitalization,
    RevenueTtm,
    RevenueGrowthYoY,
    EpsTtm,
    EpsGrowthYoY,
    FreeCashFlowTtm,
    GrossMargin,
    OperatingMargin,
    NetMargin,
    DebtToEquity,
    ReturnOnEquity,
    PriceToEarnings,
    PriceToSales,
    PriceToBook,
    PriceToFreeCashFlow,
    Rsi14,
    DistanceFromSma50,
    Volatility20,
    DaysToEarnings,
    AnalystTargetUpside,
};

enum class FundamentalScreenOperator : std::uint8_t {
    GreaterThanOrEqual,
    LessThanOrEqual,
};

struct FundamentalScreenCondition {
    FundamentalScreenField field{
        FundamentalScreenField::RevenueGrowthYoY};
    FundamentalScreenOperator comparison{
        FundamentalScreenOperator::GreaterThanOrEqual};
    double threshold{};

    [[nodiscard]] bool operator==(
        const FundamentalScreenCondition&) const = default;
};

struct FundamentalScreenDefinition {
    static constexpr std::size_t maximumConditions{16};

    QString name{QStringLiteral("Fundamental screen")};
    std::vector<FundamentalScreenCondition> conditions;
    FundamentalScreenField sortField{
        FundamentalScreenField::RevenueGrowthYoY};
    bool sortDescending{true};

    [[nodiscard]] bool operator==(
        const FundamentalScreenDefinition&) const = default;
};

struct FundamentalScreenInput {
    FundamentalCompany company;
    Bars dailyBars;
    QString priceCurrency;
    std::vector<ResearchEvent> events;
    std::vector<AnalystTargetEstimate> targetEstimates;
};

struct FundamentalScreenRow {
    QString symbol;
    QDate priceDate;
    QDate latestFiledDate;
    std::map<FundamentalScreenField, std::optional<double>> values;
    bool matched{};
    QString unavailableReason;
};

struct FundamentalScreenResult {
    std::vector<FundamentalScreenRow> rows;
    QString error;

    [[nodiscard]] bool ok() const noexcept { return error.isEmpty(); }
};

struct FundamentalScreenLoadResult {
    FundamentalScreenDefinition definition;
    QString error;

    [[nodiscard]] bool ok() const noexcept { return error.isEmpty(); }
};

[[nodiscard]] QString fundamentalScreenFieldId(
    FundamentalScreenField field);
[[nodiscard]] QString fundamentalScreenFieldLabel(
    FundamentalScreenField field);
[[nodiscard]] QString fundamentalScreenOperatorLabel(
    FundamentalScreenOperator comparison);
[[nodiscard]] QString validateFundamentalScreen(
    const FundamentalScreenDefinition& definition);
[[nodiscard]] FundamentalScreenResult runFundamentalScreen(
    const FundamentalScreenDefinition& definition,
    const std::vector<FundamentalScreenInput>& inputs,
    const QDate& asOfDate);
[[nodiscard]] QByteArray serializeFundamentalScreen(
    const FundamentalScreenDefinition& definition);
[[nodiscard]] FundamentalScreenLoadResult deserializeFundamentalScreen(
    const QByteArray& payload);

} // namespace tvchart
