#include "strategy/strategy_engine.hpp"

#include "analysis/technical_indicators.hpp"
#include "watchlists/watchlist_workspace.hpp"

#include <algorithm>
#include <cmath>
#include <exception>
#include <limits>
#include <ranges>
#include <utility>

namespace tvchart {
namespace {

[[nodiscard]] std::uint64_t seriesKey(const StrategyOperand& operand) noexcept {
    return (static_cast<std::uint64_t>(operand.field) << 32U) |
           static_cast<std::uint64_t>(operand.period);
}

[[nodiscard]] IndicatorKind indicatorKind(const StrategyField field) {
    switch (field) {
    case StrategyField::SimpleMovingAverage:
        return IndicatorKind::SimpleMovingAverage;
    case StrategyField::ExponentialMovingAverage:
        return IndicatorKind::ExponentialMovingAverage;
    case StrategyField::RelativeStrengthIndex:
        return IndicatorKind::RelativeStrengthIndex;
    case StrategyField::VolumeRatio:
        return IndicatorKind::VolumeSimpleMovingAverage;
    default:
        return IndicatorKind::None;
    }
}

[[nodiscard]] QString alertIdentity(
    const StrategyAlert& alert,
    const QString& normalizedSymbol) {
    return alert.id.trimmed() + u'|' + normalizedSymbol;
}

[[nodiscard]] QString validateAlert(const StrategyAlert& alert) {
    if (alert.id.trimmed().isEmpty() || alert.id.size() > 120) {
        return QStringLiteral("Alert identity is invalid.");
    }
    const NamedWatchlist candidate{
        .id = QStringLiteral("alert-validation"),
        .name = QStringLiteral("Alert validation"),
        .entries = {{.symbol = normalizeWatchlistSymbol(alert.symbol)}},
    };
    if (!validateWatchlist(candidate).isEmpty()) {
        return QStringLiteral("Alert symbol is invalid.");
    }
    return validateConditionGroup(alert.condition);
}

} // namespace

StrategyEvaluator::StrategyEvaluator(const Bars& bars)
    : bars_(bars) {}

const StrategyEvaluator::ValueSeries& StrategyEvaluator::series(
    const StrategyOperand& operand) {
    const auto key = seriesKey(operand);
    if (const auto found = series_.find(key); found != series_.end()) {
        return found->second;
    }

    ValueSeries values(bars_.size());
    switch (operand.field) {
    case StrategyField::Open:
    case StrategyField::High:
    case StrategyField::Low:
    case StrategyField::Close:
    case StrategyField::Volume:
        for (std::size_t index = 0; index < bars_.size(); ++index) {
            switch (operand.field) {
            case StrategyField::Open:
                values[index] = bars_[index].open;
                break;
            case StrategyField::High:
                values[index] = bars_[index].high;
                break;
            case StrategyField::Low:
                values[index] = bars_[index].low;
                break;
            case StrategyField::Close:
                values[index] = bars_[index].close;
                break;
            case StrategyField::Volume:
                values[index] = bars_[index].volume;
                break;
            default:
                break;
            }
        }
        break;
    case StrategyField::SimpleMovingAverage:
    case StrategyField::ExponentialMovingAverage:
    case StrategyField::RelativeStrengthIndex:
    case StrategyField::VolumeRatio: {
        try {
            auto spec = defaultIndicatorSpec(indicatorKind(operand.field));
            spec.period = operand.period;
            const auto calculation = calculateIndicator(bars_, spec);
            std::size_t barIndex = 0;
            for (const auto& point : calculation.primary) {
                while (barIndex < bars_.size() &&
                       bars_[barIndex].timestamp < point.timestamp) {
                    ++barIndex;
                }
                if (barIndex < bars_.size() &&
                    bars_[barIndex].timestamp == point.timestamp) {
                    values[barIndex] =
                        operand.field == StrategyField::VolumeRatio
                            ? (point.value > 0.0
                                   ? std::optional<double>{
                                         bars_[barIndex].volume / point.value}
                                   : std::nullopt)
                            : std::optional<double>{point.value};
                }
            }
        } catch (const std::exception& error) {
            calculationError_ =
                QStringLiteral("Strategy calculation failed: %1")
                    .arg(QString::fromUtf8(error.what()));
        }
        break;
    }
    }
    return series_.emplace(key, std::move(values)).first->second;
}

std::optional<double> StrategyEvaluator::value(
    const StrategyOperand& operand,
    const std::size_t barIndex) {
    if (barIndex >= bars_.size() ||
        !validateStrategyOperand(operand).isEmpty()) {
        return std::nullopt;
    }
    const auto& values = series(operand);
    if (!values[barIndex]) {
        return std::nullopt;
    }
    const auto scaled = *values[barIndex] * operand.multiplier;
    return std::isfinite(scaled)
               ? std::optional<double>{scaled}
               : std::nullopt;
}

RuleEvaluation StrategyEvaluator::evaluate(
    const ConditionGroup& group,
    const std::size_t barIndex) {
    if (barIndex >= bars_.size()) {
        return {.detail = QStringLiteral("Bar index is outside the series.")};
    }
    if (const auto error = validateConditionGroup(group); !error.isEmpty()) {
        return {
            .timestamp = bars_[barIndex].timestamp,
            .detail = error,
        };
    }

    bool aggregate = group.match == ConditionMatch::All;
    for (const auto& condition : group.conditions) {
        const auto leftNow = value(condition.left, barIndex);
        const auto rightNow =
            condition.right
                ? value(*condition.right, barIndex)
                : std::optional<double>{condition.constant};
        if (!leftNow || !rightNow) {
            return {
                .timestamp = bars_[barIndex].timestamp,
                .detail =
                    calculationError_.isEmpty()
                        ? QStringLiteral("Indicator warm-up is incomplete.")
                        : calculationError_,
            };
        }

        bool matched = false;
        switch (condition.comparison) {
        case StrategyComparison::GreaterThan:
            matched = *leftNow > *rightNow;
            break;
        case StrategyComparison::LessThan:
            matched = *leftNow < *rightNow;
            break;
        case StrategyComparison::CrossesAbove:
        case StrategyComparison::CrossesBelow: {
            if (barIndex == 0) {
                return {
                    .timestamp = bars_[barIndex].timestamp,
                    .detail =
                        QStringLiteral("Crossing rules require a previous bar."),
                };
            }
            const auto leftPrevious =
                value(condition.left, barIndex - 1);
            const auto rightPrevious =
                condition.right
                    ? value(*condition.right, barIndex - 1)
                    : std::optional<double>{condition.constant};
            if (!leftPrevious || !rightPrevious) {
                return {
                    .timestamp = bars_[barIndex].timestamp,
                    .detail =
                        calculationError_.isEmpty()
                            ? QStringLiteral("Indicator warm-up is incomplete.")
                            : calculationError_,
                };
            }
            matched =
                condition.comparison == StrategyComparison::CrossesAbove
                    ? *leftPrevious <= *rightPrevious &&
                          *leftNow > *rightNow
                    : *leftPrevious >= *rightPrevious &&
                          *leftNow < *rightNow;
            break;
        }
        }

        if (group.match == ConditionMatch::All) {
            aggregate = aggregate && matched;
        } else {
            aggregate = aggregate || matched;
        }
    }
    return {
        .available = true,
        .matched = aggregate,
        .timestamp = bars_[barIndex].timestamp,
        .detail =
            aggregate
                ? QStringLiteral("Rule matched.")
                : QStringLiteral("Rule did not match."),
    };
}

QString validateBacktestParameters(const BacktestParameters& parameters) {
    if (!std::isfinite(parameters.initialCapital) ||
        parameters.initialCapital <= 0.0 ||
        parameters.initialCapital > 1.0e12 ||
        !std::isfinite(parameters.allocationPercent) ||
        parameters.allocationPercent <= 0.0 ||
        parameters.allocationPercent > 100.0 ||
        !std::isfinite(parameters.commissionPerSide) ||
        parameters.commissionPerSide < 0.0 ||
        parameters.commissionPerSide > 1.0e6 ||
        !std::isfinite(parameters.slippageBasisPoints) ||
        parameters.slippageBasisPoints < 0.0 ||
        parameters.slippageBasisPoints > 1'000.0) {
        return QStringLiteral("Backtest capital, allocation, or costs are invalid.");
    }
    return {};
}

BacktestResult runBacktest(
    const Bars& bars,
    const StrategyDefinition& strategy,
    const BacktestParameters& parameters) {
    BacktestResult result{
        .initialCapital = parameters.initialCapital,
        .finalEquity = parameters.initialCapital,
    };
    if (const auto error = validateBars(bars)) {
        result.error =
            QStringLiteral("Backtest bars are invalid: %1")
                .arg(QString::fromStdString(*error));
        return result;
    }
    if (const auto error = validateStrategy(strategy); !error.isEmpty()) {
        result.error = error;
        return result;
    }
    if (const auto error = validateBacktestParameters(parameters);
        !error.isEmpty()) {
        result.error = error;
        return result;
    }

    enum class PendingOrder {
        None,
        Enter,
        Exit,
    };

    StrategyEvaluator evaluator(bars);
    auto cash = parameters.initialCapital;
    auto quantity = 0.0;
    auto entryPrice = 0.0;
    auto entryCommission = 0.0;
    auto entryTimestamp = std::int64_t{};
    auto pending = PendingOrder::None;
    std::size_t exposedBars = 0;
    const auto slippage = parameters.slippageBasisPoints / 10'000.0;

    const auto closePosition =
        [&](const Bar& bar, const double executionPrice, const bool forced) {
            const auto proceeds =
                quantity * executionPrice - parameters.commissionPerSide;
            cash += proceeds;
            const auto costBasis =
                quantity * entryPrice + entryCommission;
            const auto profitLoss =
                quantity * (executionPrice - entryPrice) -
                entryCommission - parameters.commissionPerSide;
            result.trades.push_back({
                .entryTimestamp = entryTimestamp,
                .exitTimestamp = bar.timestamp,
                .quantity = quantity,
                .entryPrice = entryPrice,
                .exitPrice = executionPrice,
                .entryCommission = entryCommission,
                .exitCommission = parameters.commissionPerSide,
                .profitLoss = profitLoss,
                .returnPercent =
                    costBasis > 0.0 ? profitLoss / costBasis * 100.0 : 0.0,
                .forcedExit = forced,
            });
            if (!std::isfinite(cash) || cash < -1.0e-7) {
                result.error =
                    QStringLiteral(
                        "Exit costs produced a negative or invalid cash balance.");
                return false;
            }
            cash = std::max(0.0, cash);
            quantity = 0.0;
            entryPrice = 0.0;
            entryCommission = 0.0;
            entryTimestamp = 0;
            return true;
        };

    for (std::size_t index = 0; index < bars.size(); ++index) {
        const auto& bar = bars[index];
        if (pending == PendingOrder::Exit && quantity > 0.0) {
            const auto price = bar.open * (1.0 - slippage);
            if (!closePosition(bar, price, false)) {
                return result;
            }
        } else if (pending == PendingOrder::Enter && quantity == 0.0) {
            const auto price = bar.open * (1.0 + slippage);
            const auto budget =
                cash * (parameters.allocationPercent / 100.0);
            if (budget > parameters.commissionPerSide && price > 0.0) {
                auto proposed =
                    (budget - parameters.commissionPerSide) / price;
                if (!parameters.allowFractionalShares) {
                    proposed = std::floor(proposed);
                }
                const auto totalCost =
                    proposed * price + parameters.commissionPerSide;
                const auto cashTolerance =
                    std::max(1.0e-9, std::abs(cash) * 1.0e-12);
                if (proposed > 0.0 &&
                    totalCost <= cash + cashTolerance &&
                    std::isfinite(totalCost)) {
                    quantity = proposed;
                    cash = std::max(0.0, cash - totalCost);
                    entryPrice = price;
                    entryCommission = parameters.commissionPerSide;
                    entryTimestamp = bar.timestamp;
                }
            }
        }
        pending = PendingOrder::None;

        if (quantity > 0.0) {
            ++exposedBars;
        }
        const auto equity = cash + quantity * bar.close;
        if (!std::isfinite(equity) || cash < -1.0e-7 || quantity < 0.0) {
            result.error =
                QStringLiteral("Backtest produced an invalid account state.");
            return result;
        }
        result.equityCurve.push_back({
            .timestamp = bar.timestamp,
            .equity = equity,
        });

        if (index + 1 >= bars.size()) {
            continue;
        }
        if (quantity > 0.0) {
            const auto evaluation = evaluator.evaluate(strategy.exit, index);
            if (evaluation.available && evaluation.matched) {
                pending = PendingOrder::Exit;
            }
        } else {
            const auto evaluation = evaluator.evaluate(strategy.entry, index);
            if (evaluation.available && evaluation.matched) {
                pending = PendingOrder::Enter;
            }
        }
    }

    if (quantity > 0.0) {
        const auto& finalBar = bars.back();
        if (!closePosition(
                finalBar,
                finalBar.close * (1.0 - slippage),
                true)) {
            return result;
        }
        result.equityCurve.back().equity = cash;
    }

    result.finalEquity = cash;
    result.netProfit = cash - parameters.initialCapital;
    result.totalReturnPercent =
        result.netProfit / parameters.initialCapital * 100.0;
    result.exposurePercent =
        static_cast<double>(exposedBars) /
        static_cast<double>(bars.size()) * 100.0;

    auto peak = -std::numeric_limits<double>::infinity();
    auto maximumDrawdown = 0.0;
    for (const auto& point : result.equityCurve) {
        peak = std::max(peak, point.equity);
        if (peak > 0.0) {
            maximumDrawdown =
                std::max(maximumDrawdown, (peak - point.equity) / peak);
        }
    }
    result.maximumDrawdownPercent = maximumDrawdown * 100.0;

    auto wins = std::size_t{};
    auto grossProfit = 0.0L;
    auto grossLoss = 0.0L;
    for (const auto& trade : result.trades) {
        if (trade.profitLoss > 0.0) {
            ++wins;
            grossProfit += trade.profitLoss;
        } else if (trade.profitLoss < 0.0) {
            grossLoss += -static_cast<long double>(trade.profitLoss);
        }
    }
    if (!result.trades.empty()) {
        result.winRatePercent =
            static_cast<double>(wins) /
            static_cast<double>(result.trades.size()) * 100.0;
    }
    if (grossLoss > 0.0L) {
        result.profitFactor =
            static_cast<double>(grossProfit / grossLoss);
    } else if (grossProfit > 0.0L) {
        result.profitFactor = std::numeric_limits<double>::infinity();
    }
    return result;
}

std::vector<ScanResult> scanLatest(
    const std::vector<ScanSeries>& series,
    const ConditionGroup& group) {
    std::vector<ScanResult> result;
    result.reserve(series.size());
    const auto groupError = validateConditionGroup(group);
    for (const auto& candidate : series) {
        ScanResult scan{
            .symbol = normalizeWatchlistSymbol(candidate.symbol),
            .provider = candidate.provider,
        };
        if (!groupError.isEmpty()) {
            scan.detail = groupError;
        } else if (const auto barsError = validateBars(candidate.bars)) {
            scan.detail = QString::fromStdString(*barsError);
        } else {
            StrategyEvaluator evaluator(candidate.bars);
            const auto evaluation =
                evaluator.evaluate(group, candidate.bars.size() - 1);
            scan.timestamp = evaluation.timestamp;
            scan.latestClose = candidate.bars.back().close;
            scan.detail = evaluation.detail;
            if (evaluation.available) {
                scan.status =
                    evaluation.matched
                        ? ScanStatus::Match
                        : ScanStatus::NoMatch;
            }
        }
        result.push_back(std::move(scan));
    }
    return result;
}

AlertEvaluation StrategyAlertEngine::evaluate(
    const StrategyAlert& alert,
    const Bars& bars) {
    if (!alert.enabled) {
        return {};
    }
    if (const auto error = validateAlert(alert); !error.isEmpty()) {
        return {.error = error};
    }
    if (const auto error = validateBars(bars)) {
        return {.error = QString::fromStdString(*error)};
    }
    StrategyEvaluator evaluator(bars);
    const auto evaluation = evaluator.evaluate(alert.condition, bars.size() - 1);
    if (!evaluation.available) {
        return {.unavailable = true};
    }
    if (!evaluation.matched) {
        return {};
    }

    const auto symbol = normalizeWatchlistSymbol(alert.symbol);
    const auto identity = alertIdentity(alert, symbol);
    if (const auto found = lastTriggered_.constFind(identity);
        found != lastTriggered_.cend() &&
        found.value() == evaluation.timestamp) {
        return {};
    }
    lastTriggered_[identity] = evaluation.timestamp;
    AlertTrigger trigger{
        .alertId = alert.id,
        .symbol = symbol,
        .timestamp = evaluation.timestamp,
        .message =
            QStringLiteral("%1 matched at the latest completed bar.")
                .arg(symbol),
    };
    auditLog_.push_back(trigger);
    constexpr auto maximumAuditEntries = std::size_t{1'000};
    if (auditLog_.size() > maximumAuditEntries) {
        auditLog_.erase(
            auditLog_.begin(),
            auditLog_.begin() +
                static_cast<std::ptrdiff_t>(
                    auditLog_.size() - maximumAuditEntries));
    }
    return {.trigger = std::move(trigger)};
}

const std::vector<AlertTrigger>& StrategyAlertEngine::auditLog() const noexcept {
    return auditLog_;
}

void StrategyAlertEngine::clear() {
    lastTriggered_.clear();
    auditLog_.clear();
}

} // namespace tvchart
