#pragma once

#include "domain/bar.hpp"
#include "strategy/strategy_models.hpp"

#include <QHash>
#include <QString>

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <unordered_map>
#include <vector>

namespace tvchart {

using TimeframeSeries = std::map<Timeframe, Bars>;

struct RuleEvaluation {
    bool available{};
    bool matched{};
    std::int64_t timestamp{};
    QString detail;
};

class StrategyEvaluator final {
public:
    explicit StrategyEvaluator(const Bars& bars);
    StrategyEvaluator(
        const Bars& bars,
        Timeframe primaryTimeframe,
        const TimeframeSeries& additionalSeries);

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
    Timeframe primaryTimeframe_{Timeframe::OneMinute};
    const TimeframeSeries* additionalSeries_{};
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
    double maximumAdverseExcursionPercent{};
    double maximumFavorableExcursionPercent{};
    std::size_t barsHeld{};
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
    double buyAndHoldReturnPercent{};
    double averageTradeReturnPercent{};
    double largestWinPercent{};
    double largestLossPercent{};
    std::size_t maximumConsecutiveLosses{};
    std::optional<double> compoundAnnualGrowthRatePercent;
    std::optional<double> sharpeRatio;
    std::optional<double> sortinoRatio;
    std::optional<double> calmarRatio;
    double longestUnderwaterDays{};
    QString error;

    [[nodiscard]] bool ok() const noexcept { return error.isEmpty(); }
};

struct HoldoutBacktestResult {
    std::size_t splitIndex{};
    std::int64_t splitTimestamp{};
    double holdoutPercent{};
    BacktestResult training;
    BacktestResult holdout;
    QString error;

    [[nodiscard]] bool ok() const noexcept {
        return error.isEmpty() && training.ok() && holdout.ok();
    }
};

[[nodiscard]] QString validateBacktestParameters(
    const BacktestParameters& parameters);
[[nodiscard]] BacktestResult runBacktest(
    const Bars& bars,
    const StrategyDefinition& strategy,
    const BacktestParameters& parameters);
[[nodiscard]] BacktestResult runBacktest(
    const Bars& bars,
    Timeframe primaryTimeframe,
    const TimeframeSeries& additionalSeries,
    const StrategyDefinition& strategy,
    const BacktestParameters& parameters);
[[nodiscard]] HoldoutBacktestResult runHoldoutBacktest(
    const Bars& bars,
    const StrategyDefinition& strategy,
    const BacktestParameters& parameters,
    double holdoutPercent);
[[nodiscard]] HoldoutBacktestResult runHoldoutBacktest(
    const Bars& bars,
    Timeframe primaryTimeframe,
    const TimeframeSeries& additionalSeries,
    const StrategyDefinition& strategy,
    const BacktestParameters& parameters,
    double holdoutPercent);

struct WalkForwardFold {
    std::size_t index{};
    std::size_t startIndex{};
    std::size_t endIndex{};
    std::int64_t startTimestamp{};
    std::int64_t endTimestamp{};
    BacktestResult result;
};

struct WalkForwardAnalysis {
    std::vector<WalkForwardFold> folds;
    double medianReturnPercent{};
    double positiveFoldPercent{};
    double worstFoldReturnPercent{};
    QString error;

    [[nodiscard]] bool ok() const noexcept {
        return error.isEmpty() && !folds.empty();
    }
};

struct MonteCarloAnalysis {
    std::size_t simulationCount{};
    std::size_t tradeCount{};
    double medianTerminalReturnPercent{};
    double percentile5TerminalReturnPercent{};
    double percentile95TerminalReturnPercent{};
    double percentile95MaximumDrawdownPercent{};
    double probabilityOfLossPercent{};
    QString error;

    [[nodiscard]] bool ok() const noexcept { return error.isEmpty(); }
};

struct ParameterStabilityPoint {
    std::uint32_t period{};
    BacktestResult result;
};

struct ParameterStabilityAnalysis {
    QString operandLabel;
    std::vector<ParameterStabilityPoint> points;
    QString error;

    [[nodiscard]] bool ok() const noexcept {
        return error.isEmpty() && !points.empty();
    }
};

enum class MarketRegime : std::uint8_t {
    UptrendLowVolatility,
    UptrendHighVolatility,
    DowntrendLowVolatility,
    DowntrendHighVolatility,
    Unavailable,
};

struct RegimeResult {
    MarketRegime regime{MarketRegime::Unavailable};
    std::size_t trades{};
    double winRatePercent{};
    double averageReturnPercent{};
    double netProfitLoss{};
};

struct RegimeAnalysis {
    std::vector<RegimeResult> regimes;
    std::size_t unavailableTrades{};
    QString error;

    [[nodiscard]] bool ok() const noexcept { return error.isEmpty(); }
};

struct StrategyBatchSeries {
    QString symbol;
    QString provider;
    Bars bars;
    Timeframe timeframe{Timeframe::OneDay};
    TimeframeSeries additionalSeries;
};

struct StrategyBatchResult {
    QString symbol;
    QString provider;
    BacktestResult result;
};

[[nodiscard]] WalkForwardAnalysis runWalkForwardAnalysis(
    const Bars& bars,
    Timeframe primaryTimeframe,
    const TimeframeSeries& additionalSeries,
    const StrategyDefinition& strategy,
    const BacktestParameters& parameters,
    std::size_t foldCount);
[[nodiscard]] MonteCarloAnalysis runTradeMonteCarlo(
    const BacktestResult& backtest,
    std::size_t simulationCount,
    std::uint64_t seed = 0x54564348415254ULL);
[[nodiscard]] ParameterStabilityAnalysis runPrimaryPeriodStability(
    const Bars& bars,
    Timeframe primaryTimeframe,
    const TimeframeSeries& additionalSeries,
    const StrategyDefinition& strategy,
    const BacktestParameters& parameters,
    const std::vector<std::uint32_t>& periods);
[[nodiscard]] QString marketRegimeLabel(MarketRegime regime);
[[nodiscard]] RegimeAnalysis analyzeTradeRegimes(
    const Bars& bars,
    const BacktestResult& backtest);
[[nodiscard]] std::vector<StrategyBatchResult> runStrategyBatch(
    const std::vector<StrategyBatchSeries>& series,
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
    Timeframe timeframe{Timeframe::OneMinute};
    TimeframeSeries additionalSeries;
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

enum class AlertFrequency : std::uint8_t {
    Once,
    OncePerBar,
    OnTransition,
    Cooldown,
};

struct StrategyAlert {
    QString id;
    QString symbol;
    ConditionGroup condition;
    bool enabled{true};
    QString name;
    AlertFrequency frequency{AlertFrequency::OncePerBar};
    std::int64_t cooldownSeconds{};
    std::int64_t expiresAtUtc{};
};

struct AlertTrigger {
    QString alertId;
    QString symbol;
    std::int64_t timestamp{};
    std::int64_t triggeredAtUtc{};
    QString message;
};

struct AlertEvaluation {
    std::optional<AlertTrigger> trigger;
    bool unavailable{};
    bool expired{};
    QString error;
};

struct AlertWorkspace {
    static constexpr int currentSchemaVersion{1};
    static constexpr std::size_t maximumAlerts{64};
    static constexpr std::size_t maximumHistory{1'000};

    std::vector<StrategyAlert> alerts;
    std::vector<AlertTrigger> history;
};

struct AlertWorkspaceLoadResult {
    AlertWorkspace workspace;
    QString error;

    [[nodiscard]] bool ok() const noexcept { return error.isEmpty(); }
};

class StrategyAlertEngine final {
public:
    [[nodiscard]] AlertEvaluation evaluate(
        const StrategyAlert& alert,
        const Bars& bars,
        std::int64_t evaluatedAtUtc = 0);
    [[nodiscard]] AlertEvaluation evaluate(
        const StrategyAlert& alert,
        const Bars& bars,
        Timeframe primaryTimeframe,
        const TimeframeSeries& additionalSeries,
        std::int64_t evaluatedAtUtc = 0);
    [[nodiscard]] const std::vector<AlertTrigger>& auditLog() const noexcept;
    [[nodiscard]] bool recordExternalTrigger(AlertTrigger trigger);
    void restoreAuditLog(std::vector<AlertTrigger> history);
    void clear();

private:
    struct AlertState {
        std::int64_t lastBarTimestamp{};
        std::int64_t lastTriggeredAtUtc{};
        bool hasTriggered{};
        bool lastMatched{};
    };

    QHash<QString, AlertState> states_;
    std::vector<AlertTrigger> auditLog_;
};

[[nodiscard]] QString alertFrequencyId(AlertFrequency frequency);
[[nodiscard]] QString alertFrequencyLabel(AlertFrequency frequency);
[[nodiscard]] QString validateStrategyAlert(const StrategyAlert& alert);
[[nodiscard]] QByteArray serializeAlertWorkspace(
    const AlertWorkspace& workspace);
[[nodiscard]] AlertWorkspaceLoadResult deserializeAlertWorkspace(
    const QByteArray& json);

} // namespace tvchart
