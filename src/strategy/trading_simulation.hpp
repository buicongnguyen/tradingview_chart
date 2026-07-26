#pragma once

#include "domain/bar.hpp"
#include "strategy/strategy_engine.hpp"
#include "strategy/strategy_models.hpp"

#include <QByteArray>
#include <QString>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace tvchart {

enum class SimulationMode : std::uint8_t {
    Automatic,
    Manual,
    Assisted,
};

enum class SimulationAction : std::uint8_t {
    None,
    Enter,
    Exit,
};

enum class SimulationDecisionOrigin : std::uint8_t {
    Strategy,
    Manual,
    Assisted,
    Engine,
};

enum class SimulationDecisionDisposition : std::uint8_t {
    Observed,
    Proposed,
    Queued,
    Approved,
    Rejected,
    Expired,
    Executed,
    Ignored,
};

enum class SimulationCommandType : std::uint8_t {
    ManualEnter,
    ManualExit,
    Approve,
    Reject,
};

struct SimulationDecision {
    std::int64_t timestamp{};
    SimulationAction action{SimulationAction::None};
    SimulationDecisionOrigin origin{SimulationDecisionOrigin::Engine};
    SimulationDecisionDisposition disposition{
        SimulationDecisionDisposition::Observed};
    QString detail;
};

struct SimulationUserCommand {
    std::int64_t timestamp{};
    SimulationCommandType type{SimulationCommandType::Reject};
};

struct SimulationConfig {
    StrategyDefinition strategy;
    BacktestParameters execution;
    SimulationMode mode{SimulationMode::Automatic};
    std::size_t startIndex{};
};

struct SimulationAccount {
    double initialCapital{};
    double cash{};
    double quantity{};
    double entryPrice{};
    double equity{};
    double realizedProfitLoss{};
    double unrealizedProfitLoss{};
    double totalReturnPercent{};
    double currentDrawdownPercent{};
    double maximumDrawdownPercent{};
    std::int64_t currentTimestamp{};
    std::size_t currentIndex{};
    SimulationAction pendingAction{SimulationAction::None};
    SimulationAction proposedAction{SimulationAction::None};
    QString ruleDetail;
    bool finished{};
};

struct SimulationSnapshot {
    static constexpr int currentSchemaVersion{1};

    QString symbol;
    QString provider;
    Timeframe timeframe{Timeframe::OneMinute};
    QByteArray sourceFingerprint;
    SimulationConfig config;
    std::int64_t currentTimestamp{};
    std::vector<SimulationUserCommand> commands;
};

struct SimulationSnapshotLoadResult {
    SimulationSnapshot snapshot;
    QString error;

    [[nodiscard]] bool ok() const noexcept { return error.isEmpty(); }
};

[[nodiscard]] QString simulationModeId(SimulationMode mode);
[[nodiscard]] QString simulationModeLabel(SimulationMode mode);
[[nodiscard]] QString simulationActionLabel(SimulationAction action);
[[nodiscard]] QString simulationDecisionOriginLabel(
    SimulationDecisionOrigin origin);
[[nodiscard]] QString simulationDecisionDispositionLabel(
    SimulationDecisionDisposition disposition);
[[nodiscard]] QByteArray simulationSourceFingerprint(
    const Bars& bars,
    Timeframe timeframe,
    const TimeframeSeries& additionalSeries = {});
[[nodiscard]] QByteArray serializeSimulationSnapshot(
    const SimulationSnapshot& snapshot);
[[nodiscard]] SimulationSnapshotLoadResult deserializeSimulationSnapshot(
    const QByteArray& json);

class TradingSimulationSession final {
public:
    [[nodiscard]] QString reset(
        Bars bars,
        Timeframe primaryTimeframe,
        TimeframeSeries additionalSeries,
        SimulationConfig config);
    [[nodiscard]] QString step();
    [[nodiscard]] QString requestManualAction(SimulationAction action);
    [[nodiscard]] QString approveProposal();
    [[nodiscard]] QString rejectProposal();
    [[nodiscard]] QString restore(
        Bars bars,
        Timeframe primaryTimeframe,
        TimeframeSeries additionalSeries,
        const SimulationSnapshot& snapshot,
        const QString& expectedSymbol,
        const QString& expectedProvider);
    [[nodiscard]] SimulationSnapshot snapshot(
        QString symbol,
        QString provider) const;
    void clear();

    [[nodiscard]] bool active() const noexcept;
    [[nodiscard]] const SimulationAccount& account() const noexcept;
    [[nodiscard]] const std::vector<BacktestTrade>& trades() const noexcept;
    [[nodiscard]] const std::vector<SimulationDecision>& decisions()
        const noexcept;
    [[nodiscard]] Bars visibleBars() const;
    [[nodiscard]] const SimulationConfig& config() const noexcept;
    [[nodiscard]] QByteArray sourceFingerprint() const;

private:
    [[nodiscard]] QString requestManualAction(
        SimulationAction action,
        bool recordCommand);
    [[nodiscard]] QString approveProposal(bool recordCommand);
    [[nodiscard]] QString rejectProposal(bool recordCommand);
    void evaluateCurrentBar();
    void appendDecision(
        SimulationAction action,
        SimulationDecisionOrigin origin,
        SimulationDecisionDisposition disposition,
        QString detail);
    void updateAccountAtClose(const Bar& bar);
    [[nodiscard]] bool executeEnter(const Bar& bar);
    [[nodiscard]] bool executeExit(const Bar& bar);

    Bars bars_;
    Timeframe primaryTimeframe_{Timeframe::OneMinute};
    TimeframeSeries additionalSeries_;
    std::unique_ptr<StrategyEvaluator> evaluator_;
    SimulationConfig config_;
    SimulationAccount account_;
    std::vector<BacktestTrade> trades_;
    std::vector<SimulationDecision> decisions_;
    std::vector<SimulationUserCommand> commands_;
    double entryCommission_{};
    std::int64_t entryTimestamp_{};
    double positionHigh_{};
    double positionLow_{};
    std::size_t positionBars_{};
    double highWaterEquity_{};
    bool active_{};
};

} // namespace tvchart
