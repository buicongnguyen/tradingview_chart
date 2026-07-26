#pragma once

#include <QDate>
#include <QString>

#include <cstddef>
#include <cstdint>
#include <vector>

namespace tvchart {

enum class FundamentalMetric : std::uint8_t {
    Revenue,
    GrossProfit,
    OperatingIncome,
    NetIncome,
    DilutedEps,
    Cash,
    TotalDebt,
    Assets,
    Liabilities,
    Equity,
    OperatingCashFlow,
    CapitalExpenditure,
    DilutedShares,
};

enum class FundamentalPeriodMode : std::uint8_t {
    Annual,
    Quarterly,
    TrailingTwelveMonths,
};

struct FundamentalFact {
    QString symbol;
    QString cik;
    FundamentalMetric metric{FundamentalMetric::Revenue};
    QString taxonomy;
    QString tag;
    QString unit;
    double value{};
    QDate periodStart;
    QDate periodEnd;
    QDate filedDate;
    QString form;
    int fiscalYear{};
    QString fiscalPeriod;
    QString accession;
    QString frame;
    QString sourceUrl;
    std::int64_t retrievedAtUtc{};

    [[nodiscard]] bool operator==(const FundamentalFact&) const = default;
};

struct FundamentalCompany {
    QString symbol;
    QString cik;
    QString name;
    QString provider{QStringLiteral("SEC EDGAR")};
    std::int64_t retrievedAtUtc{};
    std::vector<FundamentalFact> facts;

    [[nodiscard]] bool operator==(const FundamentalCompany&) const = default;
};

struct FundamentalCompanySummary {
    QString symbol;
    QString cik;
    QString name;
    QDate latestFiledDate;
    std::size_t factCount{};
    std::int64_t retrievedAtUtc{};
};

[[nodiscard]] QString fundamentalMetricId(FundamentalMetric metric);
[[nodiscard]] QString fundamentalMetricLabel(FundamentalMetric metric);
[[nodiscard]] QString fundamentalPeriodModeLabel(FundamentalPeriodMode mode);
[[nodiscard]] QString validateFundamentalFact(const FundamentalFact& fact);
[[nodiscard]] QString validateFundamentalCompany(
    const FundamentalCompany& company);
[[nodiscard]] int fundamentalTagPriority(
    FundamentalMetric metric,
    const QString& tag);
[[nodiscard]] bool isAdditiveFundamentalMetric(FundamentalMetric metric);
[[nodiscard]] bool isInstantFundamentalMetric(FundamentalMetric metric);

} // namespace tvchart
