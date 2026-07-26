#pragma once

#include <QByteArray>
#include <QHash>
#include <QString>
#include <QStringList>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace tvchart {

enum class PortfolioTransactionType : std::uint8_t {
    Deposit,
    Withdrawal,
    Buy,
    Sell,
    Dividend,
    Fee,
    Split,
};

struct PortfolioTransaction {
    QString id;
    PortfolioTransactionType type{PortfolioTransactionType::Buy};
    QString symbol;
    std::int64_t timestampUtc{};
    double quantity{};
    double price{};
    double amount{};
    double fees{};
    QString currency{QStringLiteral("USD")};
    QString note;

    [[nodiscard]] bool operator==(const PortfolioTransaction&) const = default;
};

struct PortfolioTarget {
    QString symbol;
    double targetPercent{};

    [[nodiscard]] bool operator==(const PortfolioTarget&) const = default;
};

struct Portfolio {
    QString id;
    QString name;
    QString baseCurrency{QStringLiteral("USD")};
    std::vector<PortfolioTransaction> transactions;
    std::vector<PortfolioTarget> targets;

    [[nodiscard]] bool operator==(const Portfolio&) const = default;
};

struct PortfolioWorkspace {
    static constexpr int currentSchemaVersion{2};
    static constexpr std::size_t maximumPortfolios{16};
    static constexpr std::size_t maximumTransactionsPerPortfolio{5'000};
    static constexpr std::size_t maximumTransactionsTotal{5'000};
    static constexpr std::size_t maximumTargetsPerPortfolio{500};

    std::vector<Portfolio> portfolios;

    [[nodiscard]] bool operator==(const PortfolioWorkspace&) const = default;
};

struct PortfolioWorkspaceLoadResult {
    PortfolioWorkspace workspace;
    QString error;

    [[nodiscard]] bool ok() const noexcept { return error.isEmpty(); }
};

struct PortfolioPrice {
    double price{};
    std::int64_t asOfUtc{};
    QString currency;
};

struct PortfolioHolding {
    QString symbol;
    double quantity{};
    double costBasis{};
    double averageCost{};
    std::optional<double> latestPrice;
    std::optional<double> marketValue;
    std::optional<double> unrealizedProfitLoss;
    double realizedProfitLoss{};
    double income{};
    double allocationPercent{};
};

struct PortfolioSnapshot {
    QString portfolioId;
    QString baseCurrency;
    std::vector<PortfolioHolding> holdings;
    QStringList missingPrices;
    std::int64_t valuationTimestampUtc{};
    double cash{};
    double marketValue{};
    double equity{};
    double netContributions{};
    double totalGain{};
    double realizedProfitLoss{};
    double unrealizedProfitLoss{};
    double income{};
    double feesPaid{};
    double largestPositionPercent{};
    double concentrationIndex{};
    std::optional<double> effectiveHoldings;
    bool completeValuation{true};
    QString error;

    [[nodiscard]] bool ok() const noexcept { return error.isEmpty(); }
};

[[nodiscard]] QString portfolioTransactionTypeId(
    PortfolioTransactionType type);
[[nodiscard]] QString portfolioTransactionTypeLabel(
    PortfolioTransactionType type);
[[nodiscard]] QString validatePortfolioTransaction(
    const PortfolioTransaction& transaction);
[[nodiscard]] QString validatePortfolioTarget(
    const PortfolioTarget& target);
[[nodiscard]] QString validatePortfolio(const Portfolio& portfolio);
[[nodiscard]] QString validatePortfolioWorkspace(
    const PortfolioWorkspace& workspace);
[[nodiscard]] QByteArray serializePortfolioWorkspace(
    const PortfolioWorkspace& workspace);
[[nodiscard]] PortfolioWorkspaceLoadResult deserializePortfolioWorkspace(
    const QByteArray& json);
[[nodiscard]] PortfolioSnapshot calculatePortfolioSnapshot(
    const Portfolio& portfolio,
    const QHash<QString, PortfolioPrice>& latestPrices);
[[nodiscard]] PortfolioWorkspace defaultPortfolioWorkspace();

} // namespace tvchart
