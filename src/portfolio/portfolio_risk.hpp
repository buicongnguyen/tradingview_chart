#pragma once

#include "domain/bar.hpp"
#include "portfolio/portfolio_models.hpp"

#include <QString>
#include <QStringList>

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <vector>

namespace tvchart {

using PortfolioHistory = std::map<QString, Bars>;

struct PairCorrelation {
    QString leftSymbol;
    QString rightSymbol;
    std::size_t observations{};
    std::optional<double> correlation;
};

struct PortfolioRiskContribution {
    QString symbol;
    double weightPercent{};
    double contributionPercent{};
};

struct PortfolioRiskReport {
    QString benchmarkSymbol;
    QStringList missingHistory;
    std::size_t observations{};
    std::size_t benchmarkObservations{};
    std::int64_t firstTimestamp{};
    std::int64_t lastTimestamp{};
    std::optional<double> annualizedReturnPercent;
    std::optional<double> annualizedVolatilityPercent;
    std::optional<double> sharpeRatio;
    std::optional<double> maximumDrawdownPercent;
    std::optional<double> historicalValueAtRisk95Percent;
    std::optional<double> historicalConditionalValueAtRisk95Percent;
    std::optional<double> beta;
    std::optional<double> annualizedAlphaPercent;
    std::optional<double> moneyWeightedReturnPercent;
    std::optional<double> dailyCloseTimeWeightedReturnPercent;
    bool dailyCloseTimeWeightedReturnIsApproximate{};
    std::vector<PairCorrelation> correlations;
    std::vector<PortfolioRiskContribution> riskContributions;
    QString error;

    [[nodiscard]] bool ok() const noexcept { return error.isEmpty(); }
};

struct RebalanceSuggestion {
    QString symbol;
    double currentPercent{};
    double targetPercent{};
    double currentValue{};
    double targetValue{};
    double differenceValue{};
    std::optional<double> approximateShares;
};

struct RebalanceReport {
    std::vector<RebalanceSuggestion> suggestions;
    double targetCashPercent{100.0};
    QStringList missingPrices;
    QString error;

    [[nodiscard]] bool ok() const noexcept { return error.isEmpty(); }
};

[[nodiscard]] PortfolioRiskReport calculatePortfolioRisk(
    const Portfolio& portfolio,
    const PortfolioSnapshot& snapshot,
    const PortfolioHistory& histories,
    QString benchmarkSymbol,
    const Bars& benchmarkBars);
[[nodiscard]] RebalanceReport calculateRebalance(
    const Portfolio& portfolio,
    const PortfolioSnapshot& snapshot,
    const QHash<QString, PortfolioPrice>& latestPrices = {});

} // namespace tvchart
