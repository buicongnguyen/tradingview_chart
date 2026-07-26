#pragma once

#include "domain/bar.hpp"
#include "strategy/strategy_models.hpp"

#include <QHash>
#include <QString>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <unordered_map>
#include <vector>

namespace tvchart {

struct RuleEvaluation {
    bool available{};
    bool matched{};
    std::int64_t timestamp{};
    QString detail;
};

class StrategyEvaluator final {
public:
    explicit StrategyEvaluator(const Bars& bars);

    [[nodiscard]] RuleEvaluation evaluate(
        const ConditionGroup& group,
        std::size_t barIndex);
    [[nodiscard]] std::optional<double> value(
        const StrategyOperand& operand,
        std::size_t barIndex);

private:
    using ValueSeries = std::vector<std::optional<double>>;

    [[nodiscard]] const ValueSeries& series(const StrategyOperand& operand);

    const Bars& bars_;
    std::unordered_map<std::uint64_t, ValueSeries> series_;
    QString calculationError_;
};

struct BacktestParameters {
    double initialCapital{100'000.0};
    double allocationPercent{100.0};
    double commissionPerSide{};
    double slippageBasisPoints{};
    bool allowFractionalShares{true};
};

struct BacktestTrade {
    std::int64_t entryTimestamp{};
    std::int64_t exitTimestamp{};
    double quantity{};
    double entryPrice{};
    double exitPrice{};
    double entryCommission{};
    double exitCommission{};
    double profitLoss{};
    double returnPercent{};
    bool forcedExit{};
};

struct EquityPoint {
    std::int64_t timestamp{};
    double equity{};
};

struct BacktestResult {
    std::vector<BacktestTrade> trades;
    std::vector<EquityPoint> equityCurve;
    double initialCapital{};
    double finalEquity{};
    double netProfit{};
    double totalReturnPercent{};
    double maximumDrawdownPercent{};
    double winRatePercent{};
    std::optional<double> profitFactor;
    double exposurePercent{};
    QString error;

    [[nodiscard]] bool ok() const noexcept { return error.isEmpty(); }
};

[[nodiscard]] QString validateBacktestParameters(
    const BacktestParameters& parameters);
[[nodiscard]] BacktestResult runBacktest(
    const Bars& bars,
    const StrategyDefinition& strategy,
    const BacktestParameters& parameters);

enum class ScanStatus : std::uint8_t {
    Match,
    NoMatch,
    Unavailable,
};

struct ScanSeries {
    QString symbol;
    QString provider;
    Bars bars;
};

struct ScanResult {
    QString symbol;
    QString provider;
    ScanStatus status{ScanStatus::Unavailable};
    std::int64_t timestamp{};
    double latestClose{};
    QString detail;
};

[[nodiscard]] std::vector<ScanResult> scanLatest(
    const std::vector<ScanSeries>& series,
    const ConditionGroup& group);

struct StrategyAlert {
    QString id;
    QString symbol;
    ConditionGroup condition;
    bool enabled{true};
};

struct AlertTrigger {
    QString alertId;
    QString symbol;
    std::int64_t timestamp{};
    QString message;
};

struct AlertEvaluation {
    std::optional<AlertTrigger> trigger;
    bool unavailable{};
    QString error;
};

class StrategyAlertEngine final {
public:
    [[nodiscard]] AlertEvaluation evaluate(
        const StrategyAlert& alert,
        const Bars& bars);
    [[nodiscard]] const std::vector<AlertTrigger>& auditLog() const noexcept;
    void clear();

private:
    QHash<QString, std::int64_t> lastTriggered_;
    std::vector<AlertTrigger> auditLog_;
};

} // namespace tvchart
