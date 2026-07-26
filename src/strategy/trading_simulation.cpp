#include "strategy/trading_simulation.hpp"

#include <QCryptographicHash>
#include <QDataStream>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QIODevice>

#include <algorithm>
#include <cmath>
#include <iterator>
#include <limits>
#include <ranges>
#include <utility>

namespace tvchart {
namespace {

constexpr auto kMaximumSnapshotBytes = 1024 * 1024;
constexpr auto kMaximumCommands = std::size_t{10'000};

[[nodiscard]] QString commandId(const SimulationCommandType type) {
    switch (type) {
    case SimulationCommandType::ManualEnter:
        return QStringLiteral("manual-enter");
    case SimulationCommandType::ManualExit:
        return QStringLiteral("manual-exit");
    case SimulationCommandType::Approve:
        return QStringLiteral("approve");
    case SimulationCommandType::Reject:
        return QStringLiteral("reject");
    }
    return QStringLiteral("reject");
}

[[nodiscard]] std::optional<SimulationCommandType> commandFromId(
    const QString& id) {
    if (id == QStringLiteral("manual-enter")) {
        return SimulationCommandType::ManualEnter;
    }
    if (id == QStringLiteral("manual-exit")) {
        return SimulationCommandType::ManualExit;
    }
    if (id == QStringLiteral("approve")) {
        return SimulationCommandType::Approve;
    }
    if (id == QStringLiteral("reject")) {
        return SimulationCommandType::Reject;
    }
    return std::nullopt;
}

[[nodiscard]] std::optional<SimulationMode> modeFromId(const QString& id) {
    if (id == QStringLiteral("automatic")) {
        return SimulationMode::Automatic;
    }
    if (id == QStringLiteral("manual")) {
        return SimulationMode::Manual;
    }
    if (id == QStringLiteral("assisted")) {
        return SimulationMode::Assisted;
    }
    return std::nullopt;
}

[[nodiscard]] bool finiteAccountValue(const double value) noexcept {
    return std::isfinite(value) && std::abs(value) <= 1.0e15;
}

[[nodiscard]] QString validateSnapshot(const SimulationSnapshot& snapshot) {
    if (snapshot.symbol.trimmed().isEmpty() ||
        snapshot.symbol.size() > 64 ||
        snapshot.provider.trimmed().isEmpty() ||
        snapshot.provider.size() > 120 ||
        snapshot.sourceFingerprint.size() !=
            QCryptographicHash::hashLength(
                QCryptographicHash::Sha256) ||
        snapshot.currentTimestamp <= 0 ||
        snapshot.commands.size() > kMaximumCommands) {
        return QStringLiteral("Simulation snapshot metadata is invalid.");
    }
    if (const auto error = validateStrategy(snapshot.config.strategy);
        !error.isEmpty()) {
        return error;
    }
    if (const auto error =
            validateBacktestParameters(snapshot.config.execution);
        !error.isEmpty()) {
        return error;
    }
    auto previous = std::int64_t{};
    for (const auto& command : snapshot.commands) {
        if (command.timestamp <= 0 ||
            (previous > 0 && command.timestamp < previous)) {
            return QStringLiteral(
                "Simulation snapshot commands are not chronological.");
        }
        previous = command.timestamp;
    }
    return {};
}

} // namespace

QString simulationModeId(const SimulationMode mode) {
    switch (mode) {
    case SimulationMode::Automatic:
        return QStringLiteral("automatic");
    case SimulationMode::Manual:
        return QStringLiteral("manual");
    case SimulationMode::Assisted:
        return QStringLiteral("assisted");
    }
    return QStringLiteral("automatic");
}

QString simulationModeLabel(const SimulationMode mode) {
    switch (mode) {
    case SimulationMode::Automatic:
        return QStringLiteral("Automatic");
    case SimulationMode::Manual:
        return QStringLiteral("Manual");
    case SimulationMode::Assisted:
        return QStringLiteral("Assisted");
    }
    return QStringLiteral("Automatic");
}

QString simulationActionLabel(const SimulationAction action) {
    switch (action) {
    case SimulationAction::None:
        return QStringLiteral("None");
    case SimulationAction::Enter:
        return QStringLiteral("Buy");
    case SimulationAction::Exit:
        return QStringLiteral("Sell");
    }
    return QStringLiteral("None");
}

QString simulationDecisionOriginLabel(
    const SimulationDecisionOrigin origin) {
    switch (origin) {
    case SimulationDecisionOrigin::Strategy:
        return QStringLiteral("Strategy");
    case SimulationDecisionOrigin::Manual:
        return QStringLiteral("Manual");
    case SimulationDecisionOrigin::Assisted:
        return QStringLiteral("Assisted");
    case SimulationDecisionOrigin::Engine:
        return QStringLiteral("Engine");
    }
    return QStringLiteral("Engine");
}

QString simulationDecisionDispositionLabel(
    const SimulationDecisionDisposition disposition) {
    switch (disposition) {
    case SimulationDecisionDisposition::Observed:
        return QStringLiteral("Observed");
    case SimulationDecisionDisposition::Proposed:
        return QStringLiteral("Proposed");
    case SimulationDecisionDisposition::Queued:
        return QStringLiteral("Queued");
    case SimulationDecisionDisposition::Approved:
        return QStringLiteral("Approved");
    case SimulationDecisionDisposition::Rejected:
        return QStringLiteral("Rejected");
    case SimulationDecisionDisposition::Expired:
        return QStringLiteral("Expired");
    case SimulationDecisionDisposition::Executed:
        return QStringLiteral("Executed");
    case SimulationDecisionDisposition::Ignored:
        return QStringLiteral("Ignored");
    }
    return QStringLiteral("Observed");
}

QByteArray simulationSourceFingerprint(
    const Bars& bars,
    const Timeframe timeframe,
    const TimeframeSeries& additionalSeries) {
    QByteArray payload;
    QDataStream stream(&payload, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::BigEndian);
    stream.setFloatingPointPrecision(QDataStream::DoublePrecision);
    const auto appendSeries =
        [&stream](
            const Timeframe seriesTimeframe,
            const Bars& series) {
            stream << static_cast<qint32>(seriesTimeframe)
                   << static_cast<quint64>(series.size());
            for (const auto& bar : series) {
                stream << static_cast<qint64>(bar.timestamp)
                       << bar.open
                       << bar.high
                       << bar.low
                       << bar.close
                       << bar.volume;
            }
        };
    stream << static_cast<quint64>(
        additionalSeries.size() + 1);
    appendSeries(timeframe, bars);
    for (const auto& [seriesTimeframe, series] :
         additionalSeries) {
        appendSeries(seriesTimeframe, series);
    }
    return QCryptographicHash::hash(
        payload,
        QCryptographicHash::Sha256);
}

QByteArray serializeSimulationSnapshot(
    const SimulationSnapshot& snapshot) {
    QJsonArray commands;
    for (const auto& command : snapshot.commands) {
        commands.append(QJsonObject{
            {QStringLiteral("timestamp"),
             QString::number(command.timestamp)},
            {QStringLiteral("type"), commandId(command.type)},
        });
    }
    const auto& execution = snapshot.config.execution;
    const QJsonObject root{
        {QStringLiteral("schemaVersion"),
         SimulationSnapshot::currentSchemaVersion},
        {QStringLiteral("symbol"), snapshot.symbol},
        {QStringLiteral("provider"), snapshot.provider},
        {QStringLiteral("timeframe"),
         static_cast<int>(snapshot.timeframe)},
        {QStringLiteral("sourceFingerprint"),
         QString::fromLatin1(snapshot.sourceFingerprint.toHex())},
        {QStringLiteral("strategy"),
         QString::fromLatin1(
             serializeStrategy(snapshot.config.strategy).toBase64())},
        {QStringLiteral("mode"),
         simulationModeId(snapshot.config.mode)},
        {QStringLiteral("startIndex"),
         QString::number(snapshot.config.startIndex)},
        {QStringLiteral("currentTimestamp"),
         QString::number(snapshot.currentTimestamp)},
        {QStringLiteral("execution"),
         QJsonObject{
             {QStringLiteral("initialCapital"),
              execution.initialCapital},
             {QStringLiteral("allocationPercent"),
              execution.allocationPercent},
             {QStringLiteral("commissionPerSide"),
              execution.commissionPerSide},
             {QStringLiteral("slippageBasisPoints"),
              execution.slippageBasisPoints},
             {QStringLiteral("allowFractionalShares"),
              execution.allowFractionalShares},
         }},
        {QStringLiteral("commands"), commands},
    };
    return QJsonDocument(root).toJson(QJsonDocument::Compact);
}

SimulationSnapshotLoadResult deserializeSimulationSnapshot(
    const QByteArray& json) {
    SimulationSnapshotLoadResult result;
    if (json.isEmpty() || json.size() > kMaximumSnapshotBytes) {
        result.error =
            QStringLiteral("Simulation snapshot size is invalid.");
        return result;
    }
    QJsonParseError parseError;
    const auto document = QJsonDocument::fromJson(json, &parseError);
    if (parseError.error != QJsonParseError::NoError ||
        !document.isObject()) {
        result.error =
            QStringLiteral("Simulation snapshot JSON is invalid.");
        return result;
    }
    const auto root = document.object();
    if (root.value(QStringLiteral("schemaVersion")).toInt(-1) !=
        SimulationSnapshot::currentSchemaVersion) {
        result.error =
            QStringLiteral("Simulation snapshot schema is unsupported.");
        return result;
    }
    const auto mode =
        modeFromId(root.value(QStringLiteral("mode")).toString());
    const auto strategyBytes = QByteArray::fromBase64(
        root.value(QStringLiteral("strategy"))
            .toString()
            .toLatin1(),
        QByteArray::AbortOnBase64DecodingErrors);
    const auto strategy = deserializeStrategy(strategyBytes);
    bool startOk = false;
    const auto startIndex =
        root.value(QStringLiteral("startIndex"))
            .toString()
            .toULongLong(&startOk);
    bool timestampOk = false;
    const auto currentTimestamp =
        root.value(QStringLiteral("currentTimestamp"))
            .toString()
            .toLongLong(&timestampOk);
    const auto timeframeValue =
        root.value(QStringLiteral("timeframe")).toInt(-1);
    if (!mode || !strategy.ok() || !startOk || !timestampOk ||
        timeframeValue < static_cast<int>(Timeframe::OneMinute) ||
        timeframeValue > static_cast<int>(Timeframe::OneDay)) {
        result.error =
            QStringLiteral("Simulation snapshot configuration is invalid.");
        return result;
    }
    const auto executionObject =
        root.value(QStringLiteral("execution")).toObject();
    const auto commandsValue =
        root.value(QStringLiteral("commands"));
    if (executionObject.isEmpty() || !commandsValue.isArray()) {
        result.error =
            QStringLiteral("Simulation snapshot fields are missing.");
        return result;
    }

    result.snapshot = {
        .symbol = root.value(QStringLiteral("symbol")).toString(),
        .provider = root.value(QStringLiteral("provider")).toString(),
        .timeframe = static_cast<Timeframe>(timeframeValue),
        .sourceFingerprint = QByteArray::fromHex(
            root.value(QStringLiteral("sourceFingerprint"))
                .toString()
                .toLatin1()),
        .config = {
            .strategy = strategy.strategy,
            .execution = {
                .initialCapital =
                    executionObject
                        .value(QStringLiteral("initialCapital"))
                        .toDouble(std::numeric_limits<double>::quiet_NaN()),
                .allocationPercent =
                    executionObject
                        .value(QStringLiteral("allocationPercent"))
                        .toDouble(std::numeric_limits<double>::quiet_NaN()),
                .commissionPerSide =
                    executionObject
                        .value(QStringLiteral("commissionPerSide"))
                        .toDouble(std::numeric_limits<double>::quiet_NaN()),
                .slippageBasisPoints =
                    executionObject
                        .value(QStringLiteral("slippageBasisPoints"))
                        .toDouble(std::numeric_limits<double>::quiet_NaN()),
                .allowFractionalShares =
                    executionObject
                        .value(QStringLiteral("allowFractionalShares"))
                        .toBool(true),
            },
            .mode = *mode,
            .startIndex = static_cast<std::size_t>(startIndex),
        },
        .currentTimestamp = currentTimestamp,
    };
    const auto commandArray = commandsValue.toArray();
    if (commandArray.size() >
        static_cast<qsizetype>(kMaximumCommands)) {
        result.error =
            QStringLiteral("Simulation snapshot has too many commands.");
        return result;
    }
    result.snapshot.commands.reserve(
        static_cast<std::size_t>(commandArray.size()));
    for (const auto& commandValue : commandArray) {
        if (!commandValue.isObject()) {
            result.error =
                QStringLiteral("Simulation snapshot command is invalid.");
            return result;
        }
        const auto commandObject = commandValue.toObject();
        bool commandTimestampOk = false;
        const auto commandTimestamp =
            commandObject.value(QStringLiteral("timestamp"))
                .toString()
                .toLongLong(&commandTimestampOk);
        const auto type = commandFromId(
            commandObject.value(QStringLiteral("type")).toString());
        if (!commandTimestampOk || !type) {
            result.error =
                QStringLiteral("Simulation snapshot command is invalid.");
            return result;
        }
        result.snapshot.commands.push_back({
            .timestamp = commandTimestamp,
            .type = *type,
        });
    }
    result.error = validateSnapshot(result.snapshot);
    return result;
}

QString TradingSimulationSession::reset(
    Bars bars,
    const Timeframe primaryTimeframe,
    TimeframeSeries additionalSeries,
    SimulationConfig config) {
    clear();
    if (const auto error = validateBars(bars)) {
        return QStringLiteral("Simulation bars are invalid: %1")
            .arg(QString::fromStdString(*error));
    }
    if (const auto error = validateStrategy(config.strategy);
        !error.isEmpty()) {
        return error;
    }
    if (const auto error =
            validateBacktestParameters(config.execution);
        !error.isEmpty()) {
        return error;
    }
    if (config.startIndex >= bars.size()) {
        return QStringLiteral(
            "Simulation start is outside the completed series.");
    }
    for (const auto& [timeframe, series] : additionalSeries) {
        if (timeframe == primaryTimeframe ||
            validateBars(series).has_value()) {
            return QStringLiteral(
                "Simulation higher-timeframe history is invalid.");
        }
    }

    bars_ = std::move(bars);
    primaryTimeframe_ = primaryTimeframe;
    additionalSeries_ = std::move(additionalSeries);
    config_ = std::move(config);
    evaluator_ = std::make_unique<StrategyEvaluator>(
        bars_,
        primaryTimeframe_,
        additionalSeries_);
    account_ = {
        .initialCapital = config_.execution.initialCapital,
        .cash = config_.execution.initialCapital,
        .equity = config_.execution.initialCapital,
        .currentTimestamp = bars_[config_.startIndex].timestamp,
        .currentIndex = config_.startIndex,
        .finished = config_.startIndex + 1 >= bars_.size(),
    };
    highWaterEquity_ = account_.initialCapital;
    active_ = true;
    evaluateCurrentBar();
    return {};
}

void TradingSimulationSession::appendDecision(
    const SimulationAction action,
    const SimulationDecisionOrigin origin,
    const SimulationDecisionDisposition disposition,
    QString detail) {
    decisions_.push_back({
        .timestamp = account_.currentTimestamp,
        .action = action,
        .origin = origin,
        .disposition = disposition,
        .detail = std::move(detail),
    });
}

void TradingSimulationSession::evaluateCurrentBar() {
    if (!active_) {
        return;
    }
    if (!evaluator_) {
        account_.ruleDetail =
            QStringLiteral("Strategy evaluator is unavailable.");
        return;
    }
    const auto action =
        account_.quantity > 0.0
            ? SimulationAction::Exit
            : SimulationAction::Enter;
    const auto& group =
        action == SimulationAction::Exit
            ? config_.strategy.exit
            : config_.strategy.entry;
    const auto evaluation =
        evaluator_->evaluate(group, account_.currentIndex);
    account_.ruleDetail =
        QStringLiteral("%1 rule: %2")
            .arg(
                simulationActionLabel(action),
                evaluation.detail);
    if (!evaluation.available || !evaluation.matched) {
        return;
    }
    if (account_.finished) {
        appendDecision(
            action,
            SimulationDecisionOrigin::Strategy,
            SimulationDecisionDisposition::Ignored,
            QStringLiteral(
                "Rule matched on the final available candle; no next open exists."));
        return;
    }
    switch (config_.mode) {
    case SimulationMode::Automatic:
        account_.pendingAction = action;
        appendDecision(
            action,
            SimulationDecisionOrigin::Strategy,
            SimulationDecisionDisposition::Queued,
            QStringLiteral(
                "Rule matched and was queued for the next candle open."));
        break;
    case SimulationMode::Manual:
        appendDecision(
            action,
            SimulationDecisionOrigin::Strategy,
            SimulationDecisionDisposition::Observed,
            QStringLiteral(
                "Rule matched; Manual mode requires a Buy or Sell command."));
        break;
    case SimulationMode::Assisted:
        account_.proposedAction = action;
        appendDecision(
            action,
            SimulationDecisionOrigin::Strategy,
            SimulationDecisionDisposition::Proposed,
            QStringLiteral(
                "Rule matched; approve before advancing to queue the order."));
        break;
    }
}

bool TradingSimulationSession::executeEnter(const Bar& bar) {
    const auto slippage =
        config_.execution.slippageBasisPoints / 10'000.0;
    const auto executionPrice = bar.open * (1.0 + slippage);
    const auto budget =
        account_.cash *
        (config_.execution.allocationPercent / 100.0);
    if (!std::isfinite(executionPrice) || executionPrice <= 0.0 ||
        budget <= config_.execution.commissionPerSide) {
        appendDecision(
            SimulationAction::Enter,
            SimulationDecisionOrigin::Engine,
            SimulationDecisionDisposition::Ignored,
            QStringLiteral(
                "Buy could not execute because price or available capital was invalid."));
        return true;
    }
    auto quantity =
        (budget - config_.execution.commissionPerSide) /
        executionPrice;
    if (!config_.execution.allowFractionalShares) {
        quantity = std::floor(quantity);
    }
    const auto cost =
        quantity * executionPrice +
        config_.execution.commissionPerSide;
    const auto tolerance =
        std::max(1.0e-9, std::abs(account_.cash) * 1.0e-12);
    if (!std::isfinite(quantity) || quantity <= 0.0 ||
        !std::isfinite(cost) ||
        cost > account_.cash + tolerance) {
        appendDecision(
            SimulationAction::Enter,
            SimulationDecisionOrigin::Engine,
            SimulationDecisionDisposition::Ignored,
            QStringLiteral(
                "Buy could not execute because the configured allocation cannot purchase a share."));
        return true;
    }
    account_.quantity = quantity;
    account_.cash =
        std::max(0.0, account_.cash - cost);
    account_.entryPrice = executionPrice;
    entryCommission_ = config_.execution.commissionPerSide;
    entryTimestamp_ = bar.timestamp;
    positionHigh_ = std::max(executionPrice, bar.high);
    positionLow_ = std::min(executionPrice, bar.low);
    positionBars_ = 0;
    appendDecision(
        SimulationAction::Enter,
        SimulationDecisionOrigin::Engine,
        SimulationDecisionDisposition::Executed,
        QStringLiteral("Buy executed at %1 for %2 shares.")
            .arg(executionPrice, 0, 'f', 4)
            .arg(quantity, 0, 'f', 6));
    return true;
}

bool TradingSimulationSession::executeExit(const Bar& bar) {
    const auto slippage =
        config_.execution.slippageBasisPoints / 10'000.0;
    const auto executionPrice = bar.open * (1.0 - slippage);
    const auto proceeds =
        account_.quantity * executionPrice -
        config_.execution.commissionPerSide;
    const auto costBasis =
        account_.quantity * account_.entryPrice +
        entryCommission_;
    const auto profitLoss =
        account_.quantity *
            (executionPrice - account_.entryPrice) -
        entryCommission_ -
        config_.execution.commissionPerSide;
    const auto nextCash = account_.cash + proceeds;
    if (!finiteAccountValue(executionPrice) ||
        executionPrice <= 0.0 ||
        !finiteAccountValue(nextCash) ||
        nextCash < -1.0e-7) {
        return false;
    }
    trades_.push_back({
        .entryTimestamp = entryTimestamp_,
        .exitTimestamp = bar.timestamp,
        .quantity = account_.quantity,
        .entryPrice = account_.entryPrice,
        .exitPrice = executionPrice,
        .entryCommission = entryCommission_,
        .exitCommission = config_.execution.commissionPerSide,
        .profitLoss = profitLoss,
        .returnPercent =
            costBasis > 0.0
                ? profitLoss / costBasis * 100.0
                : 0.0,
        .maximumAdverseExcursionPercent =
            account_.entryPrice > 0.0
                ? (positionLow_ / account_.entryPrice - 1.0) *
                      100.0
                : 0.0,
        .maximumFavorableExcursionPercent =
            account_.entryPrice > 0.0
                ? (positionHigh_ / account_.entryPrice - 1.0) *
                      100.0
                : 0.0,
        .barsHeld = positionBars_,
    });
    account_.cash = std::max(0.0, nextCash);
    account_.realizedProfitLoss += profitLoss;
    account_.quantity = 0.0;
    account_.entryPrice = 0.0;
    entryCommission_ = 0.0;
    entryTimestamp_ = 0;
    positionHigh_ = 0.0;
    positionLow_ = 0.0;
    positionBars_ = 0;
    appendDecision(
        SimulationAction::Exit,
        SimulationDecisionOrigin::Engine,
        SimulationDecisionDisposition::Executed,
        QStringLiteral("Sell executed at %1; realized P/L %2.")
            .arg(executionPrice, 0, 'f', 4)
            .arg(profitLoss, 0, 'f', 2));
    return true;
}

void TradingSimulationSession::updateAccountAtClose(const Bar& bar) {
    if (account_.quantity > 0.0) {
        ++positionBars_;
        positionHigh_ = std::max(positionHigh_, bar.high);
        positionLow_ = std::min(positionLow_, bar.low);
        account_.unrealizedProfitLoss =
            account_.quantity *
                (bar.close - account_.entryPrice) -
            entryCommission_;
    } else {
        account_.unrealizedProfitLoss = 0.0;
    }
    account_.equity =
        account_.cash + account_.quantity * bar.close;
    highWaterEquity_ =
        std::max(highWaterEquity_, account_.equity);
    account_.currentDrawdownPercent =
        highWaterEquity_ > 0.0
            ? (highWaterEquity_ - account_.equity) /
                  highWaterEquity_ * 100.0
            : 0.0;
    account_.maximumDrawdownPercent =
        std::max(
            account_.maximumDrawdownPercent,
            account_.currentDrawdownPercent);
    account_.totalReturnPercent =
        account_.initialCapital > 0.0
            ? (account_.equity / account_.initialCapital - 1.0) *
                  100.0
            : 0.0;
}

QString TradingSimulationSession::step() {
    if (!active_) {
        return QStringLiteral("Simulation is inactive.");
    }
    if (account_.finished) {
        return QStringLiteral("Simulation has reached the final candle.");
    }
    if (account_.proposedAction != SimulationAction::None) {
        appendDecision(
            account_.proposedAction,
            SimulationDecisionOrigin::Assisted,
            SimulationDecisionDisposition::Expired,
            QStringLiteral(
                "Unresolved assisted proposal expired when the simulation advanced."));
        account_.proposedAction = SimulationAction::None;
    }
    ++account_.currentIndex;
    const auto& bar = bars_[account_.currentIndex];
    account_.currentTimestamp = bar.timestamp;
    const auto pending = account_.pendingAction;
    account_.pendingAction = SimulationAction::None;
    if (pending == SimulationAction::Enter) {
        if (account_.quantity > 0.0) {
            appendDecision(
                pending,
                SimulationDecisionOrigin::Engine,
                SimulationDecisionDisposition::Ignored,
                QStringLiteral(
                    "Queued buy was ignored because a position is already open."));
        } else if (!executeEnter(bar)) {
            return QStringLiteral(
                "Simulation buy produced an invalid account state.");
        }
    } else if (pending == SimulationAction::Exit) {
        if (account_.quantity <= 0.0) {
            appendDecision(
                pending,
                SimulationDecisionOrigin::Engine,
                SimulationDecisionDisposition::Ignored,
                QStringLiteral(
                    "Queued sell was ignored because no position is open."));
        } else if (!executeExit(bar)) {
            return QStringLiteral(
                "Simulation sell produced an invalid account state.");
        }
    }
    updateAccountAtClose(bar);
    if (!finiteAccountValue(account_.cash) ||
        !finiteAccountValue(account_.quantity) ||
        !finiteAccountValue(account_.equity) ||
        account_.cash < -1.0e-7 ||
        account_.quantity < 0.0) {
        return QStringLiteral(
            "Simulation produced an invalid account state.");
    }
    account_.finished =
        account_.currentIndex + 1 >= bars_.size();
    evaluateCurrentBar();
    return {};
}

QString TradingSimulationSession::requestManualAction(
    const SimulationAction action) {
    return requestManualAction(action, true);
}

QString TradingSimulationSession::requestManualAction(
    const SimulationAction action,
    const bool recordCommand) {
    if (!active_) {
        return QStringLiteral("Simulation is inactive.");
    }
    if (config_.mode != SimulationMode::Manual) {
        return QStringLiteral(
            "Manual Buy/Sell orders are available only in Manual mode.");
    }
    if (account_.finished) {
        return QStringLiteral(
            "No next candle open exists for this order.");
    }
    if (action != SimulationAction::Enter &&
        action != SimulationAction::Exit) {
        return QStringLiteral("Manual order action is invalid.");
    }
    if (account_.pendingAction != SimulationAction::None) {
        return QStringLiteral(
            "An order is already queued for the next open.");
    }
    if (account_.proposedAction != SimulationAction::None) {
        return QStringLiteral(
            "Approve or reject the assisted proposal first.");
    }
    if ((action == SimulationAction::Enter &&
         account_.quantity > 0.0) ||
        (action == SimulationAction::Exit &&
         account_.quantity <= 0.0)) {
        return action == SimulationAction::Enter
                   ? QStringLiteral("A position is already open.")
                   : QStringLiteral("No position is open.");
    }
    account_.pendingAction = action;
    appendDecision(
        action,
        SimulationDecisionOrigin::Manual,
        SimulationDecisionDisposition::Queued,
        QStringLiteral(
            "Manual order queued for the next candle open."));
    if (recordCommand) {
        commands_.push_back({
            .timestamp = account_.currentTimestamp,
            .type =
                action == SimulationAction::Enter
                    ? SimulationCommandType::ManualEnter
                    : SimulationCommandType::ManualExit,
        });
    }
    return {};
}

QString TradingSimulationSession::approveProposal() {
    return approveProposal(true);
}

QString TradingSimulationSession::approveProposal(
    const bool recordCommand) {
    if (!active_ ||
        config_.mode != SimulationMode::Assisted ||
        account_.proposedAction == SimulationAction::None) {
        return QStringLiteral(
            "No assisted proposal is available to approve.");
    }
    if (account_.pendingAction != SimulationAction::None) {
        return QStringLiteral(
            "An order is already queued for the next open.");
    }
    const auto action = account_.proposedAction;
    account_.proposedAction = SimulationAction::None;
    account_.pendingAction = action;
    appendDecision(
        action,
        SimulationDecisionOrigin::Assisted,
        SimulationDecisionDisposition::Approved,
        QStringLiteral(
            "Assisted proposal approved for the next candle open."));
    if (recordCommand) {
        commands_.push_back({
            .timestamp = account_.currentTimestamp,
            .type = SimulationCommandType::Approve,
        });
    }
    return {};
}

QString TradingSimulationSession::rejectProposal() {
    return rejectProposal(true);
}

QString TradingSimulationSession::rejectProposal(
    const bool recordCommand) {
    if (!active_ ||
        config_.mode != SimulationMode::Assisted ||
        account_.proposedAction == SimulationAction::None) {
        return QStringLiteral(
            "No assisted proposal is available to reject.");
    }
    const auto action = account_.proposedAction;
    account_.proposedAction = SimulationAction::None;
    appendDecision(
        action,
        SimulationDecisionOrigin::Assisted,
        SimulationDecisionDisposition::Rejected,
        QStringLiteral("Assisted proposal rejected."));
    if (recordCommand) {
        commands_.push_back({
            .timestamp = account_.currentTimestamp,
            .type = SimulationCommandType::Reject,
        });
    }
    return {};
}

SimulationSnapshot TradingSimulationSession::snapshot(
    QString symbol,
    QString provider) const {
    return {
        .symbol = std::move(symbol),
        .provider = std::move(provider),
        .timeframe = primaryTimeframe_,
        .sourceFingerprint =
            simulationSourceFingerprint(
                bars_,
                primaryTimeframe_,
                additionalSeries_),
        .config = config_,
        .currentTimestamp = account_.currentTimestamp,
        .commands = commands_,
    };
}

QString TradingSimulationSession::restore(
    Bars bars,
    const Timeframe primaryTimeframe,
    TimeframeSeries additionalSeries,
    const SimulationSnapshot& snapshotValue,
    const QString& expectedSymbol,
    const QString& expectedProvider) {
    if (const auto error = validateSnapshot(snapshotValue);
        !error.isEmpty()) {
        return error;
    }
    if (snapshotValue.symbol != expectedSymbol ||
        snapshotValue.provider != expectedProvider ||
        snapshotValue.timeframe != primaryTimeframe) {
        return QStringLiteral(
            "Saved simulation belongs to a different symbol, provider, or timeframe.");
    }
    if (simulationSourceFingerprint(
            bars,
            primaryTimeframe,
            additionalSeries) !=
        snapshotValue.sourceFingerprint) {
        return QStringLiteral(
            "Saved simulation history no longer matches the loaded primary or higher-timeframe series.");
    }
    auto config = snapshotValue.config;
    if (config.startIndex >= bars.size() ||
        bars[config.startIndex].timestamp >
            snapshotValue.currentTimestamp) {
        return QStringLiteral(
            "Saved simulation start is outside the loaded series.");
    }
    const auto target = std::ranges::find(
        bars,
        snapshotValue.currentTimestamp,
        &Bar::timestamp);
    if (target == bars.end()) {
        return QStringLiteral(
            "Saved simulation moment is unavailable in the loaded series.");
    }
    const auto targetIndex =
        static_cast<std::size_t>(
            std::distance(bars.begin(), target));
    auto commands = snapshotValue.commands;
    const auto resetError = reset(
        std::move(bars),
        primaryTimeframe,
        std::move(additionalSeries),
        std::move(config));
    if (!resetError.isEmpty()) {
        return resetError;
    }
    commands_.clear();
    auto commandIndex = std::size_t{};
    while (true) {
        while (commandIndex < commands.size() &&
               commands[commandIndex].timestamp ==
                   account_.currentTimestamp) {
            QString commandError;
            switch (commands[commandIndex].type) {
            case SimulationCommandType::ManualEnter:
                commandError = requestManualAction(
                    SimulationAction::Enter,
                    false);
                break;
            case SimulationCommandType::ManualExit:
                commandError = requestManualAction(
                    SimulationAction::Exit,
                    false);
                break;
            case SimulationCommandType::Approve:
                commandError = approveProposal(false);
                break;
            case SimulationCommandType::Reject:
                commandError = rejectProposal(false);
                break;
            }
            if (!commandError.isEmpty()) {
                clear();
                return QStringLiteral(
                    "Saved simulation command could not replay: %1")
                    .arg(commandError);
            }
            ++commandIndex;
        }
        if (account_.currentIndex == targetIndex) {
            break;
        }
        const auto stepError = step();
        if (!stepError.isEmpty()) {
            clear();
            return QStringLiteral(
                "Saved simulation could not replay: %1")
                .arg(stepError);
        }
    }
    if (commandIndex != commands.size()) {
        clear();
        return QStringLiteral(
            "Saved simulation contains commands outside its restored moment.");
    }
    commands_ = std::move(commands);
    return {};
}

void TradingSimulationSession::clear() {
    bars_.clear();
    additionalSeries_.clear();
    evaluator_.reset();
    config_ = {};
    account_ = {};
    trades_.clear();
    decisions_.clear();
    commands_.clear();
    entryCommission_ = 0.0;
    entryTimestamp_ = 0;
    positionHigh_ = 0.0;
    positionLow_ = 0.0;
    positionBars_ = 0;
    highWaterEquity_ = 0.0;
    active_ = false;
}

bool TradingSimulationSession::active() const noexcept {
    return active_;
}

const SimulationAccount& TradingSimulationSession::account() const noexcept {
    return account_;
}

const std::vector<BacktestTrade>&
TradingSimulationSession::trades() const noexcept {
    return trades_;
}

const std::vector<SimulationDecision>&
TradingSimulationSession::decisions() const noexcept {
    return decisions_;
}

Bars TradingSimulationSession::visibleBars() const {
    if (!active_ || bars_.empty()) {
        return {};
    }
    return {
        bars_.begin(),
        std::next(
            bars_.begin(),
            static_cast<std::ptrdiff_t>(
                account_.currentIndex + 1)),
    };
}

const SimulationConfig&
TradingSimulationSession::config() const noexcept {
    return config_;
}

QByteArray TradingSimulationSession::sourceFingerprint() const {
    return simulationSourceFingerprint(
        bars_,
        primaryTimeframe_,
        additionalSeries_);
}

} // namespace tvchart
