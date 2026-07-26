#include "strategy/strategy_engine.hpp"

#include "analysis/technical_indicators.hpp"
#include "watchlists/watchlist_workspace.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QSet>

#include <algorithm>
#include <array>
#include <cmath>
#include <exception>
#include <limits>
#include <numeric>
#include <random>
#include <ranges>
#include <set>
#include <utility>

namespace tvchart {
namespace {

[[nodiscard]] std::uint64_t seriesKey(const StrategyOperand& operand) noexcept {
    const auto timeframe =
        operand.timeframe
            ? static_cast<std::uint64_t>(*operand.timeframe) + 1U
            : 0U;
    return (static_cast<std::uint64_t>(operand.field) << 48U) |
           (static_cast<std::uint64_t>(operand.period) << 16U) |
           timeframe;
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

[[nodiscard]] std::optional<AlertFrequency> parseAlertFrequency(
    const QString& id) {
    constexpr std::array values{
        AlertFrequency::Once,
        AlertFrequency::OncePerBar,
        AlertFrequency::OnTransition,
        AlertFrequency::Cooldown,
    };
    for (const auto value : values) {
        if (alertFrequencyId(value) == id) {
            return value;
        }
    }
    return std::nullopt;
}

[[nodiscard]] bool validAlertTrigger(const AlertTrigger& trigger) {
    if (trigger.alertId.trimmed().isEmpty() ||
        trigger.alertId.size() > 160 ||
        trigger.timestamp <= 0 || trigger.triggeredAtUtc <= 0 ||
        trigger.message.isEmpty() || trigger.message.size() > 512) {
        return false;
    }
    const NamedWatchlist candidate{
        .id = QStringLiteral("alert-history-validation"),
        .name = QStringLiteral("Alert history validation"),
        .entries = {{
            .symbol = normalizeWatchlistSymbol(trigger.symbol),
        }},
    };
    return validateWatchlist(candidate).isEmpty();
}

} // namespace

StrategyEvaluator::StrategyEvaluator(const Bars& bars)
    : bars_(bars) {}

StrategyEvaluator::StrategyEvaluator(
    const Bars& bars,
    const Timeframe primaryTimeframe,
    const TimeframeSeries& additionalSeries)
    : bars_(bars),
      primaryTimeframe_(primaryTimeframe),
      additionalSeries_(&additionalSeries) {}

const StrategyEvaluator::ValueSeries& StrategyEvaluator::series(
    const StrategyOperand& operand) {
    const auto key = seriesKey(operand);
    if (const auto found = series_.find(key); found != series_.end()) {
        return found->second;
    }

    const auto sourceTimeframe =
        operand.timeframe.value_or(primaryTimeframe_);
    const Bars* sourceBars = &bars_;
    if (sourceTimeframe != primaryTimeframe_) {
        if (!additionalSeries_) {
            calculationError_ =
                QStringLiteral("Requested timeframe history is unavailable.");
            return series_
                .emplace(key, ValueSeries(bars_.size()))
                .first->second;
        }
        const auto found = additionalSeries_->find(sourceTimeframe);
        if (found == additionalSeries_->end() ||
            validateBars(found->second).has_value()) {
            calculationError_ =
                QStringLiteral("Requested timeframe history is unavailable.");
            return series_
                .emplace(key, ValueSeries(bars_.size()))
                .first->second;
        }
        sourceBars = &found->second;
    }

    ValueSeries sourceValues(sourceBars->size());
    switch (operand.field) {
    case StrategyField::Open:
    case StrategyField::High:
    case StrategyField::Low:
    case StrategyField::Close:
    case StrategyField::Volume:
        for (std::size_t index = 0; index < sourceBars->size(); ++index) {
            switch (operand.field) {
            case StrategyField::Open:
                sourceValues[index] = (*sourceBars)[index].open;
                break;
            case StrategyField::High:
                sourceValues[index] = (*sourceBars)[index].high;
                break;
            case StrategyField::Low:
                sourceValues[index] = (*sourceBars)[index].low;
                break;
            case StrategyField::Close:
                sourceValues[index] = (*sourceBars)[index].close;
                break;
            case StrategyField::Volume:
                sourceValues[index] = (*sourceBars)[index].volume;
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
            const auto calculation = calculateIndicator(*sourceBars, spec);
            std::size_t barIndex = 0;
            for (const auto& point : calculation.primary) {
                while (barIndex < sourceBars->size() &&
                       (*sourceBars)[barIndex].timestamp < point.timestamp) {
                    ++barIndex;
                }
                if (barIndex < sourceBars->size() &&
                    (*sourceBars)[barIndex].timestamp == point.timestamp) {
                    sourceValues[barIndex] =
                        operand.field == StrategyField::VolumeRatio
                            ? (point.value > 0.0
                                   ? std::optional<double>{
                                         (*sourceBars)[barIndex].volume /
                                         point.value}
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
    ValueSeries values(bars_.size());
    if (sourceBars == &bars_) {
        values = std::move(sourceValues);
    } else {
        const auto sourceDuration = timeframeSeconds(sourceTimeframe);
        const auto primaryDuration = timeframeSeconds(primaryTimeframe_);
        auto sourceIndex = std::size_t{};
        for (std::size_t primaryIndex = 0;
             primaryIndex < bars_.size();
             ++primaryIndex) {
            const auto availableThrough =
                bars_[primaryIndex].timestamp + primaryDuration -
                sourceDuration;
            while (sourceIndex + 1 < sourceBars->size() &&
                   (*sourceBars)[sourceIndex + 1].timestamp <=
                       availableThrough) {
                ++sourceIndex;
            }
            if (!sourceBars->empty() &&
                (*sourceBars)[sourceIndex].timestamp <= availableThrough) {
                values[primaryIndex] = sourceValues[sourceIndex];
            }
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

    QString unavailableDetail;
    for (const auto& condition : group.conditions) {
        const auto leftNow = value(condition.left, barIndex);
        const auto rightNow =
            condition.right
                ? value(*condition.right, barIndex)
                : std::optional<double>{condition.constant};
        if (!leftNow || !rightNow) {
            if (unavailableDetail.isEmpty()) {
                unavailableDetail =
                    calculationError_.isEmpty()
                        ? QStringLiteral("Indicator warm-up is incomplete.")
                        : calculationError_;
            }
            continue;
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
                if (unavailableDetail.isEmpty()) {
                    unavailableDetail =
                        QStringLiteral("Crossing rules require a previous bar.");
                }
                continue;
            }
            const auto leftPrevious =
                value(condition.left, barIndex - 1);
            const auto rightPrevious =
                condition.right
                    ? value(*condition.right, barIndex - 1)
                    : std::optional<double>{condition.constant};
            if (!leftPrevious || !rightPrevious) {
                if (unavailableDetail.isEmpty()) {
                    unavailableDetail =
                        calculationError_.isEmpty()
                            ? QStringLiteral("Indicator warm-up is incomplete.")
                            : calculationError_;
                }
                continue;
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

        if ((group.match == ConditionMatch::All && !matched) ||
            (group.match == ConditionMatch::Any && matched)) {
            return {
                .available = true,
                .matched = matched,
                .timestamp = bars_[barIndex].timestamp,
                .detail =
                    matched
                        ? QStringLiteral("Rule matched.")
                        : QStringLiteral("Rule did not match."),
            };
        }
    }
    if (!unavailableDetail.isEmpty()) {
        return {
            .timestamp = bars_[barIndex].timestamp,
            .detail = std::move(unavailableDetail),
        };
    }
    const auto matched = group.match == ConditionMatch::All;
    return {
        .available = true,
        .matched = matched,
        .timestamp = bars_[barIndex].timestamp,
        .detail =
            matched
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

namespace {

void finalizeBacktestAnalytics(
    BacktestResult& result,
    const Bars& bars,
    const std::size_t startIndex,
    const std::size_t exposedBars) {
    result.finalEquity =
        result.equityCurve.empty()
            ? result.initialCapital
            : result.equityCurve.back().equity;
    result.netProfit = result.finalEquity - result.initialCapital;
    result.totalReturnPercent =
        result.netProfit / result.initialCapital * 100.0;
    const auto measuredBars = bars.size() - startIndex;
    result.exposurePercent =
        measuredBars > 0
            ? static_cast<double>(exposedBars) /
                  static_cast<double>(measuredBars) * 100.0
            : 0.0;
    if (measuredBars > 0 && bars[startIndex].open > 0.0) {
        result.buyAndHoldReturnPercent =
            (bars.back().close / bars[startIndex].open - 1.0) * 100.0;
    }

    auto peak = -std::numeric_limits<double>::infinity();
    auto maximumDrawdown = 0.0;
    auto underwaterStart = std::optional<std::int64_t>{};
    for (const auto& point : result.equityCurve) {
        if (point.equity >= peak) {
            if (underwaterStart) {
                result.longestUnderwaterDays = std::max(
                    result.longestUnderwaterDays,
                    static_cast<double>(point.timestamp - *underwaterStart) /
                        86'400.0);
                underwaterStart.reset();
            }
            peak = point.equity;
        } else if (peak > 0.0) {
            if (!underwaterStart) {
                underwaterStart = point.timestamp;
            }
            maximumDrawdown =
                std::max(maximumDrawdown, (peak - point.equity) / peak);
        }
    }
    if (underwaterStart && !result.equityCurve.empty()) {
        result.longestUnderwaterDays = std::max(
            result.longestUnderwaterDays,
            static_cast<double>(
                result.equityCurve.back().timestamp - *underwaterStart) /
                86'400.0);
    }
    result.maximumDrawdownPercent = maximumDrawdown * 100.0;

    auto wins = std::size_t{};
    auto grossProfit = 0.0L;
    auto grossLoss = 0.0L;
    auto tradeReturnSum = 0.0L;
    auto consecutiveLosses = std::size_t{};
    result.largestWinPercent = 0.0;
    result.largestLossPercent = 0.0;
    for (const auto& trade : result.trades) {
        tradeReturnSum += trade.returnPercent;
        if (trade.profitLoss > 0.0) {
            result.largestWinPercent =
                std::max(result.largestWinPercent, trade.returnPercent);
            ++wins;
            grossProfit += trade.profitLoss;
            consecutiveLosses = 0;
        } else if (trade.profitLoss < 0.0) {
            result.largestLossPercent =
                std::min(result.largestLossPercent, trade.returnPercent);
            grossLoss += -static_cast<long double>(trade.profitLoss);
            ++consecutiveLosses;
            result.maximumConsecutiveLosses =
                std::max(result.maximumConsecutiveLosses, consecutiveLosses);
        } else {
            consecutiveLosses = 0;
        }
    }
    if (!result.trades.empty()) {
        result.winRatePercent =
            static_cast<double>(wins) /
            static_cast<double>(result.trades.size()) * 100.0;
        result.averageTradeReturnPercent = static_cast<double>(
            tradeReturnSum /
            static_cast<long double>(result.trades.size()));
    }
    if (grossLoss > 0.0L) {
        result.profitFactor =
            static_cast<double>(grossProfit / grossLoss);
    } else if (grossProfit > 0.0L) {
        result.profitFactor = std::numeric_limits<double>::infinity();
    }

    if (result.equityCurve.size() >= 2) {
        const auto seconds =
            result.equityCurve.back().timestamp -
            result.equityCurve.front().timestamp;
        constexpr auto minimumAnnualizationWindow = std::int64_t{
            30 * 86'400};
        if (seconds >= minimumAnnualizationWindow &&
            result.initialCapital > 0.0 && result.finalEquity > 0.0) {
            const auto years =
                static_cast<double>(seconds) /
                (365.2425 * 86'400.0);
            result.compoundAnnualGrowthRatePercent =
                (std::pow(
                     result.finalEquity / result.initialCapital,
                     1.0 / years) -
                 1.0) *
                100.0;
            if (result.maximumDrawdownPercent > 0.0) {
                result.calmarRatio =
                    *result.compoundAnnualGrowthRatePercent /
                    result.maximumDrawdownPercent;
            }
        }

        std::vector<double> dailyEquity;
        auto currentDay = std::numeric_limits<std::int64_t>::min();
        for (const auto& point : result.equityCurve) {
            const auto day = point.timestamp / 86'400;
            if (day != currentDay) {
                dailyEquity.push_back(point.equity);
                currentDay = day;
            } else {
                dailyEquity.back() = point.equity;
            }
        }
        std::vector<double> dailyReturns;
        dailyReturns.reserve(dailyEquity.size());
        for (std::size_t index = 1; index < dailyEquity.size(); ++index) {
            if (dailyEquity[index - 1] > 0.0) {
                dailyReturns.push_back(
                    dailyEquity[index] / dailyEquity[index - 1] - 1.0);
            }
        }
        if (dailyReturns.size() >= 2) {
            const auto mean =
                std::accumulate(
                    dailyReturns.begin(),
                    dailyReturns.end(),
                    0.0) /
                static_cast<double>(dailyReturns.size());
            auto squaredDeviation = 0.0;
            auto downsideSquares = 0.0;
            for (const auto dailyReturn : dailyReturns) {
                const auto deviation = dailyReturn - mean;
                squaredDeviation += deviation * deviation;
                const auto downside = std::min(0.0, dailyReturn);
                downsideSquares += downside * downside;
            }
            const auto standardDeviation = std::sqrt(
                squaredDeviation /
                static_cast<double>(dailyReturns.size() - 1));
            const auto downsideDeviation = std::sqrt(
                downsideSquares /
                static_cast<double>(dailyReturns.size()));
            constexpr auto annualization = 15.8745078664; // sqrt(252)
            if (standardDeviation > 1.0e-15) {
                result.sharpeRatio =
                    mean / standardDeviation * annualization;
            }
            if (downsideDeviation > 1.0e-15) {
                result.sortinoRatio =
                    mean / downsideDeviation * annualization;
            }
        }
    }
}

[[nodiscard]] BacktestResult runBacktestFromIndex(
    const Bars& bars,
    const Timeframe primaryTimeframe,
    const TimeframeSeries& additionalSeries,
    const StrategyDefinition& strategy,
    const BacktestParameters& parameters,
    const std::size_t startIndex) {
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
    if (startIndex >= bars.size()) {
        result.error = QStringLiteral("Backtest start is outside the series.");
        return result;
    }

    enum class PendingOrder {
        None,
        Enter,
        Exit,
    };

    StrategyEvaluator evaluator(
        bars,
        primaryTimeframe,
        additionalSeries);
    auto cash = parameters.initialCapital;
    auto quantity = 0.0;
    auto entryPrice = 0.0;
    auto entryCommission = 0.0;
    auto entryTimestamp = std::int64_t{};
    auto positionHigh = 0.0;
    auto positionLow = 0.0;
    auto positionBars = std::size_t{};
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
                .maximumAdverseExcursionPercent =
                    entryPrice > 0.0
                        ? (positionLow / entryPrice - 1.0) * 100.0
                        : 0.0,
                .maximumFavorableExcursionPercent =
                    entryPrice > 0.0
                        ? (positionHigh / entryPrice - 1.0) * 100.0
                        : 0.0,
                .barsHeld = positionBars,
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
            positionHigh = 0.0;
            positionLow = 0.0;
            positionBars = 0;
            return true;
        };

    for (std::size_t index = startIndex; index < bars.size(); ++index) {
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
                    positionHigh = std::max(price, bar.high);
                    positionLow = std::min(price, bar.low);
                }
            }
        }
        pending = PendingOrder::None;

        if (quantity > 0.0) {
            ++exposedBars;
            ++positionBars;
            positionHigh = std::max(positionHigh, bar.high);
            positionLow = std::min(positionLow, bar.low);
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

    finalizeBacktestAnalytics(result, bars, startIndex, exposedBars);
    return result;
}

} // namespace

BacktestResult runBacktest(
    const Bars& bars,
    const StrategyDefinition& strategy,
    const BacktestParameters& parameters) {
    static const TimeframeSeries noAdditionalSeries;
    return runBacktestFromIndex(
        bars,
        Timeframe::OneMinute,
        noAdditionalSeries,
        strategy,
        parameters,
        0);
}

BacktestResult runBacktest(
    const Bars& bars,
    const Timeframe primaryTimeframe,
    const TimeframeSeries& additionalSeries,
    const StrategyDefinition& strategy,
    const BacktestParameters& parameters) {
    return runBacktestFromIndex(
        bars,
        primaryTimeframe,
        additionalSeries,
        strategy,
        parameters,
        0);
}

HoldoutBacktestResult runHoldoutBacktest(
    const Bars& bars,
    const StrategyDefinition& strategy,
    const BacktestParameters& parameters,
    const double holdoutPercent) {
    static const TimeframeSeries noAdditionalSeries;
    return runHoldoutBacktest(
        bars,
        Timeframe::OneMinute,
        noAdditionalSeries,
        strategy,
        parameters,
        holdoutPercent);
}

HoldoutBacktestResult runHoldoutBacktest(
    const Bars& bars,
    const Timeframe primaryTimeframe,
    const TimeframeSeries& additionalSeries,
    const StrategyDefinition& strategy,
    const BacktestParameters& parameters,
    const double holdoutPercent) {
    HoldoutBacktestResult result{.holdoutPercent = holdoutPercent};
    if (!std::isfinite(holdoutPercent) || holdoutPercent < 10.0 ||
        holdoutPercent > 50.0) {
        result.error =
            QStringLiteral("Holdout percentage must be between 10 and 50.");
        return result;
    }
    if (const auto error = validateBars(bars)) {
        result.error =
            QStringLiteral("Holdout bars are invalid: %1")
                .arg(QString::fromStdString(*error));
        return result;
    }
    constexpr auto minimumBarsPerPartition = std::size_t{3};
    if (bars.size() < minimumBarsPerPartition * 2) {
        result.error =
            QStringLiteral("Holdout validation requires at least six bars.");
        return result;
    }
    const auto rawSplit = static_cast<std::size_t>(
        std::floor(
            static_cast<double>(bars.size()) *
            (1.0 - holdoutPercent / 100.0)));
    result.splitIndex = std::clamp(
        rawSplit,
        minimumBarsPerPartition,
        bars.size() - minimumBarsPerPartition);
    result.splitTimestamp = bars[result.splitIndex].timestamp;
    const Bars trainingBars{
        bars.begin(),
        std::next(
            bars.begin(),
            static_cast<std::ptrdiff_t>(result.splitIndex)),
    };
    TimeframeSeries trainingAdditional;
    for (const auto& [timeframe, series] : additionalSeries) {
        const auto trainingEnd = bars[result.splitIndex - 1].timestamp +
                                 timeframeSeconds(primaryTimeframe);
        Bars subset;
        for (const auto& bar : series) {
            if (bar.timestamp + timeframeSeconds(timeframe) <= trainingEnd) {
                subset.push_back(bar);
            }
        }
        if (!subset.empty()) {
            trainingAdditional.emplace(timeframe, std::move(subset));
        }
    }
    result.training = runBacktest(
        trainingBars,
        primaryTimeframe,
        trainingAdditional,
        strategy,
        parameters);
    result.holdout = runBacktestFromIndex(
        bars,
        primaryTimeframe,
        additionalSeries,
        strategy,
        parameters,
        result.splitIndex);
    if (!result.training.ok()) {
        result.error =
            QStringLiteral("Training backtest failed: %1")
                .arg(result.training.error);
    } else if (!result.holdout.ok()) {
        result.error =
            QStringLiteral("Holdout backtest failed: %1")
                .arg(result.holdout.error);
    }
    return result;
}

namespace {

[[nodiscard]] double percentile(
    std::vector<double> values,
    const double probability) {
    if (values.empty()) {
        return 0.0;
    }
    std::ranges::sort(values);
    const auto position =
        std::clamp(probability, 0.0, 1.0) *
        static_cast<double>(values.size() - 1);
    const auto lower = static_cast<std::size_t>(std::floor(position));
    const auto upper = static_cast<std::size_t>(std::ceil(position));
    const auto fraction = position - static_cast<double>(lower);
    return values[lower] +
           (values[upper] - values[lower]) * fraction;
}

[[nodiscard]] TimeframeSeries seriesThrough(
    const TimeframeSeries& source,
    const std::int64_t availableThrough) {
    TimeframeSeries result;
    for (const auto& [timeframe, bars] : source) {
        Bars subset;
        for (const auto& bar : bars) {
            if (bar.timestamp + timeframeSeconds(timeframe) <=
                availableThrough) {
                subset.push_back(bar);
            }
        }
        if (!subset.empty()) {
            result.emplace(timeframe, std::move(subset));
        }
    }
    return result;
}

[[nodiscard]] StrategyOperand* firstTunableOperand(
    StrategyDefinition& strategy) {
    const auto findIn =
        [](ConditionGroup& group) -> StrategyOperand* {
        for (auto& condition : group.conditions) {
            if (indicatorKind(condition.left.field) !=
                IndicatorKind::None) {
                return &condition.left;
            }
            if (condition.right &&
                indicatorKind(condition.right->field) !=
                    IndicatorKind::None) {
                return &*condition.right;
            }
        }
        return nullptr;
    };
    if (auto* operand = findIn(strategy.entry)) {
        return operand;
    }
    return findIn(strategy.exit);
}

} // namespace

WalkForwardAnalysis runWalkForwardAnalysis(
    const Bars& bars,
    const Timeframe primaryTimeframe,
    const TimeframeSeries& additionalSeries,
    const StrategyDefinition& strategy,
    const BacktestParameters& parameters,
    const std::size_t foldCount) {
    WalkForwardAnalysis analysis;
    if (foldCount < 2 || foldCount > 10) {
        analysis.error =
            QStringLiteral("Walk-forward fold count must be between 2 and 10.");
        return analysis;
    }
    if (const auto error = validateBars(bars)) {
        analysis.error =
            QStringLiteral("Walk-forward bars are invalid: %1")
                .arg(QString::fromStdString(*error));
        return analysis;
    }
    if (!validateStrategy(strategy).isEmpty() ||
        !validateBacktestParameters(parameters).isEmpty()) {
        analysis.error =
            QStringLiteral("Walk-forward strategy or assumptions are invalid.");
        return analysis;
    }
    constexpr auto minimumTestBars = std::size_t{3};
    const auto initialTraining = bars.size() / 2;
    if (initialTraining < minimumTestBars ||
        bars.size() - initialTraining <
            foldCount * minimumTestBars) {
        analysis.error =
            QStringLiteral(
                "Walk-forward analysis needs enough bars for three test bars "
                "per fold after a 50% warm-up/training prefix.");
        return analysis;
    }
    const auto availableTestBars = bars.size() - initialTraining;
    const auto baseFoldSize = availableTestBars / foldCount;
    std::vector<double> returns;
    for (std::size_t fold = 0; fold < foldCount; ++fold) {
        const auto start = initialTraining + fold * baseFoldSize;
        const auto end =
            fold + 1 == foldCount
                ? bars.size()
                : start + baseFoldSize;
        Bars prefix{
            bars.begin(),
            std::next(
                bars.begin(),
                static_cast<std::ptrdiff_t>(end)),
        };
        const auto availableThrough =
            prefix.back().timestamp +
            timeframeSeconds(primaryTimeframe);
        auto foldSeries =
            seriesThrough(additionalSeries, availableThrough);
        auto result = runBacktestFromIndex(
            prefix,
            primaryTimeframe,
            foldSeries,
            strategy,
            parameters,
            start);
        if (!result.ok()) {
            analysis.error =
                QStringLiteral("Walk-forward fold %1 failed: %2")
                    .arg(fold + 1)
                    .arg(result.error);
            analysis.folds.clear();
            return analysis;
        }
        returns.push_back(result.totalReturnPercent);
        analysis.folds.push_back({
            .index = fold + 1,
            .startIndex = start,
            .endIndex = end,
            .startTimestamp = bars[start].timestamp,
            .endTimestamp = bars[end - 1].timestamp,
            .result = std::move(result),
        });
    }
    analysis.medianReturnPercent = percentile(returns, 0.5);
    analysis.worstFoldReturnPercent =
        *std::ranges::min_element(returns);
    analysis.positiveFoldPercent =
        static_cast<double>(std::ranges::count_if(
            returns,
            [](const double value) { return value > 0.0; })) /
        static_cast<double>(returns.size()) * 100.0;
    return analysis;
}

MonteCarloAnalysis runTradeMonteCarlo(
    const BacktestResult& backtest,
    const std::size_t simulationCount,
    const std::uint64_t seed) {
    MonteCarloAnalysis analysis{
        .simulationCount = simulationCount,
        .tradeCount = backtest.trades.size(),
    };
    if (!backtest.ok()) {
        analysis.error =
            QStringLiteral("Monte Carlo requires a successful backtest.");
        return analysis;
    }
    if (backtest.trades.size() < 5) {
        analysis.error =
            QStringLiteral("Monte Carlo requires at least five observed trades.");
        return analysis;
    }
    if (simulationCount < 100 || simulationCount > 100'000) {
        analysis.error =
            QStringLiteral("Monte Carlo simulations must be between 100 and 100000.");
        return analysis;
    }
    std::vector<double> tradeReturns;
    tradeReturns.reserve(backtest.trades.size());
    for (const auto& trade : backtest.trades) {
        const auto value = trade.returnPercent / 100.0;
        if (!std::isfinite(value) || value <= -1.0) {
            analysis.error =
                QStringLiteral("Monte Carlo observed an invalid trade return.");
            return analysis;
        }
        tradeReturns.push_back(value);
    }
    std::mt19937_64 generator(seed);
    std::uniform_int_distribution<std::size_t> pick(
        0,
        tradeReturns.size() - 1);
    std::vector<double> terminalReturns;
    std::vector<double> drawdowns;
    terminalReturns.reserve(simulationCount);
    drawdowns.reserve(simulationCount);
    auto losing = std::size_t{};
    for (std::size_t simulation = 0;
         simulation < simulationCount;
         ++simulation) {
        auto equity = 1.0;
        auto peak = 1.0;
        auto maximumDrawdown = 0.0;
        for (std::size_t trade = 0;
             trade < tradeReturns.size();
             ++trade) {
            equity *= 1.0 + tradeReturns[pick(generator)];
            peak = std::max(peak, equity);
            if (peak > 0.0) {
                maximumDrawdown = std::max(
                    maximumDrawdown,
                    (peak - equity) / peak);
            }
        }
        const auto terminal = (equity - 1.0) * 100.0;
        terminalReturns.push_back(terminal);
        drawdowns.push_back(maximumDrawdown * 100.0);
        if (terminal < 0.0) {
            ++losing;
        }
    }
    analysis.medianTerminalReturnPercent =
        percentile(terminalReturns, 0.5);
    analysis.percentile5TerminalReturnPercent =
        percentile(terminalReturns, 0.05);
    analysis.percentile95TerminalReturnPercent =
        percentile(terminalReturns, 0.95);
    analysis.percentile95MaximumDrawdownPercent =
        percentile(drawdowns, 0.95);
    analysis.probabilityOfLossPercent =
        static_cast<double>(losing) /
        static_cast<double>(simulationCount) * 100.0;
    return analysis;
}

ParameterStabilityAnalysis runPrimaryPeriodStability(
    const Bars& bars,
    const Timeframe primaryTimeframe,
    const TimeframeSeries& additionalSeries,
    const StrategyDefinition& strategy,
    const BacktestParameters& parameters,
    const std::vector<std::uint32_t>& periods) {
    ParameterStabilityAnalysis analysis;
    if (periods.empty() || periods.size() > 21) {
        analysis.error =
            QStringLiteral("Parameter stability requires 1 to 21 period values.");
        return analysis;
    }
    std::set<std::uint32_t> uniquePeriods;
    for (const auto period : periods) {
        if (period == 0 || period > 500 ||
            !uniquePeriods.insert(period).second) {
            analysis.error =
                QStringLiteral("Parameter stability periods must be unique and between 1 and 500.");
            return analysis;
        }
    }
    auto probe = strategy;
    auto* tunable = firstTunableOperand(probe);
    if (!tunable) {
        analysis.error =
            QStringLiteral("The strategy has no period-bearing indicator operand.");
        return analysis;
    }
    analysis.operandLabel =
        strategyFieldLabel(tunable->field);
    for (const auto period : periods) {
        auto candidate = strategy;
        auto* operand = firstTunableOperand(candidate);
        operand->period = period;
        auto result = runBacktest(
            bars,
            primaryTimeframe,
            additionalSeries,
            candidate,
            parameters);
        if (!result.ok()) {
            analysis.error =
                QStringLiteral("Period %1 failed: %2")
                    .arg(period)
                    .arg(result.error);
            analysis.points.clear();
            return analysis;
        }
        analysis.points.push_back({
            .period = period,
            .result = std::move(result),
        });
    }
    return analysis;
}

QString marketRegimeLabel(const MarketRegime regime) {
    switch (regime) {
    case MarketRegime::UptrendLowVolatility:
        return QStringLiteral("Uptrend · lower volatility");
    case MarketRegime::UptrendHighVolatility:
        return QStringLiteral("Uptrend · higher volatility");
    case MarketRegime::DowntrendLowVolatility:
        return QStringLiteral("Downtrend · lower volatility");
    case MarketRegime::DowntrendHighVolatility:
        return QStringLiteral("Downtrend · higher volatility");
    case MarketRegime::Unavailable:
        return QStringLiteral("Unavailable");
    }
    return QStringLiteral("Unavailable");
}

RegimeAnalysis analyzeTradeRegimes(
    const Bars& bars,
    const BacktestResult& backtest) {
    RegimeAnalysis analysis;
    if (const auto error = validateBars(bars)) {
        analysis.error =
            QStringLiteral("Regime bars are invalid: %1")
                .arg(QString::fromStdString(*error));
        return analysis;
    }
    if (!backtest.ok()) {
        analysis.error =
            QStringLiteral("Regime analysis requires a successful backtest.");
        return analysis;
    }
    constexpr auto trendPeriod = std::size_t{50};
    constexpr auto volatilityPeriod = std::size_t{20};
    std::vector<std::optional<double>> trend(bars.size());
    std::vector<std::optional<double>> volatility(bars.size());
    auto rollingClose = 0.0L;
    for (std::size_t index = 0; index < bars.size(); ++index) {
        rollingClose += bars[index].close;
        if (index >= trendPeriod) {
            rollingClose -= bars[index - trendPeriod].close;
        }
        if (index + 1 >= trendPeriod) {
            trend[index] = static_cast<double>(
                rollingClose /
                static_cast<long double>(trendPeriod));
        }
        if (index + 1 >= volatilityPeriod + 1) {
            std::vector<double> returns;
            returns.reserve(volatilityPeriod);
            for (std::size_t cursor =
                     index + 1 - volatilityPeriod;
                 cursor <= index;
                 ++cursor) {
                returns.push_back(
                    std::log(
                        bars[cursor].close /
                        bars[cursor - 1].close));
            }
            const auto mean =
                std::accumulate(
                    returns.begin(),
                    returns.end(),
                    0.0) /
                static_cast<double>(returns.size());
            auto sum = 0.0;
            for (const auto value : returns) {
                const auto deviation = value - mean;
                sum += deviation * deviation;
            }
            volatility[index] = std::sqrt(
                sum / static_cast<double>(returns.size() - 1));
        }
    }
    std::vector<double> availableVolatility;
    for (const auto& value : volatility) {
        if (value) {
            availableVolatility.push_back(*value);
        }
    }
    if (availableVolatility.empty()) {
        analysis.error =
            QStringLiteral("Regime analysis requires at least 21 bars.");
        return analysis;
    }
    const auto medianVolatility =
        percentile(availableVolatility, 0.5);
    struct Aggregate {
        std::size_t trades{};
        std::size_t wins{};
        long double returns{};
        long double profitLoss{};
    };
    std::map<MarketRegime, Aggregate> aggregates;
    for (const auto regime : {
             MarketRegime::UptrendLowVolatility,
             MarketRegime::UptrendHighVolatility,
             MarketRegime::DowntrendLowVolatility,
             MarketRegime::DowntrendHighVolatility,
         }) {
        aggregates.emplace(regime, Aggregate{});
    }
    for (const auto& trade : backtest.trades) {
        const auto found = std::ranges::lower_bound(
            bars,
            trade.entryTimestamp,
            {},
            &Bar::timestamp);
        if (found == bars.end() ||
            found->timestamp != trade.entryTimestamp) {
            ++analysis.unavailableTrades;
            continue;
        }
        const auto index = static_cast<std::size_t>(
            std::distance(bars.begin(), found));
        if (!trend[index] || !volatility[index]) {
            ++analysis.unavailableTrades;
            continue;
        }
        const auto uptrend = bars[index].close >= *trend[index];
        const auto highVolatility =
            *volatility[index] > medianVolatility;
        const auto regime =
            uptrend
                ? (highVolatility
                       ? MarketRegime::UptrendHighVolatility
                       : MarketRegime::UptrendLowVolatility)
                : (highVolatility
                       ? MarketRegime::DowntrendHighVolatility
                       : MarketRegime::DowntrendLowVolatility);
        auto& aggregate = aggregates[regime];
        ++aggregate.trades;
        aggregate.wins += trade.profitLoss > 0.0 ? 1U : 0U;
        aggregate.returns += trade.returnPercent;
        aggregate.profitLoss += trade.profitLoss;
    }
    for (const auto& [regime, aggregate] : aggregates) {
        analysis.regimes.push_back({
            .regime = regime,
            .trades = aggregate.trades,
            .winRatePercent =
                aggregate.trades > 0
                    ? static_cast<double>(aggregate.wins) /
                          static_cast<double>(aggregate.trades) *
                          100.0
                    : 0.0,
            .averageReturnPercent =
                aggregate.trades > 0
                    ? static_cast<double>(
                          aggregate.returns /
                          static_cast<long double>(
                              aggregate.trades))
                    : 0.0,
            .netProfitLoss =
                static_cast<double>(aggregate.profitLoss),
        });
    }
    return analysis;
}

std::vector<StrategyBatchResult> runStrategyBatch(
    const std::vector<StrategyBatchSeries>& series,
    const StrategyDefinition& strategy,
    const BacktestParameters& parameters) {
    std::vector<StrategyBatchResult> results;
    results.reserve(series.size());
    for (const auto& candidate : series) {
        results.push_back({
            .symbol = normalizeWatchlistSymbol(candidate.symbol),
            .provider = candidate.provider,
            .result = runBacktest(
                candidate.bars,
                candidate.timeframe,
                candidate.additionalSeries,
                strategy,
                parameters),
        });
    }
    return results;
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
            StrategyEvaluator evaluator(
                candidate.bars,
                candidate.timeframe,
                candidate.additionalSeries);
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

QString alertFrequencyId(const AlertFrequency frequency) {
    switch (frequency) {
    case AlertFrequency::Once:
        return QStringLiteral("once");
    case AlertFrequency::OncePerBar:
        return QStringLiteral("once-per-bar");
    case AlertFrequency::OnTransition:
        return QStringLiteral("on-transition");
    case AlertFrequency::Cooldown:
        return QStringLiteral("cooldown");
    }
    return QStringLiteral("once-per-bar");
}

QString alertFrequencyLabel(const AlertFrequency frequency) {
    switch (frequency) {
    case AlertFrequency::Once:
        return QStringLiteral("Only once");
    case AlertFrequency::OncePerBar:
        return QStringLiteral("Once per completed bar");
    case AlertFrequency::OnTransition:
        return QStringLiteral("False → true transition");
    case AlertFrequency::Cooldown:
        return QStringLiteral("Cooldown");
    }
    return QStringLiteral("Once per completed bar");
}

QString validateStrategyAlert(const StrategyAlert& alert) {
    if (alert.id.trimmed().isEmpty() || alert.id.size() > 160 ||
        alert.name.size() > 120 || alert.expiresAtUtc < 0 ||
        alert.cooldownSeconds < 0 ||
        alert.cooldownSeconds > 31 * 86'400) {
        return QStringLiteral("Alert identity, expiry, or cooldown is invalid.");
    }
    if (alert.frequency == AlertFrequency::Cooldown) {
        if (alert.cooldownSeconds < 60) {
            return QStringLiteral(
                "Cooldown alerts require at least 60 seconds.");
        }
    } else if (alert.cooldownSeconds != 0) {
        return QStringLiteral(
            "Only cooldown alerts may define a cooldown duration.");
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

QByteArray serializeAlertWorkspace(const AlertWorkspace& workspace) {
    if (workspace.alerts.size() > AlertWorkspace::maximumAlerts ||
        workspace.history.size() > AlertWorkspace::maximumHistory) {
        return {};
    }
    QSet<QString> identities;
    QJsonArray alerts;
    for (const auto& alert : workspace.alerts) {
        if (!validateStrategyAlert(alert).isEmpty() ||
            identities.contains(alert.id.trimmed())) {
            return {};
        }
        identities.insert(alert.id.trimmed());
        const StrategyDefinition strategy{
            .id = alert.id,
            .name =
                alert.name.trimmed().isEmpty()
                    ? alert.id
                    : alert.name,
            .entry = alert.condition,
            .exit = alert.condition,
        };
        const auto strategyDocument =
            QJsonDocument::fromJson(serializeStrategy(strategy));
        if (!strategyDocument.isObject()) {
            return {};
        }
        alerts.append(QJsonObject{
            {QStringLiteral("id"), alert.id},
            {QStringLiteral("symbol"), alert.symbol},
            {
                QStringLiteral("condition"),
                strategyDocument.object().value(QStringLiteral("entry")),
            },
            {QStringLiteral("enabled"), alert.enabled},
            {QStringLiteral("name"), alert.name},
            {
                QStringLiteral("frequency"),
                alertFrequencyId(alert.frequency),
            },
            {
                QStringLiteral("cooldownSeconds"),
                QString::number(alert.cooldownSeconds),
            },
            {
                QStringLiteral("expiresAtUtc"),
                QString::number(alert.expiresAtUtc),
            },
        });
    }
    QJsonArray history;
    for (const auto& trigger : workspace.history) {
        if (!validAlertTrigger(trigger)) {
            return {};
        }
        history.append(QJsonObject{
            {QStringLiteral("alertId"), trigger.alertId},
            {QStringLiteral("symbol"), trigger.symbol},
            {
                QStringLiteral("timestamp"),
                QString::number(trigger.timestamp),
            },
            {
                QStringLiteral("triggeredAtUtc"),
                QString::number(trigger.triggeredAtUtc),
            },
            {QStringLiteral("message"), trigger.message},
        });
    }
    const auto payload = QJsonDocument(QJsonObject{
        {
            QStringLiteral("schemaVersion"),
            AlertWorkspace::currentSchemaVersion,
        },
        {QStringLiteral("alerts"), alerts},
        {QStringLiteral("history"), history},
    }).toJson(QJsonDocument::Compact);
    constexpr auto maximumPayloadBytes = qsizetype{2 * 1024 * 1024};
    return payload.size() <= maximumPayloadBytes ? payload : QByteArray{};
}

AlertWorkspaceLoadResult deserializeAlertWorkspace(
    const QByteArray& json) {
    if (json.isEmpty()) {
        return {};
    }
    constexpr auto maximumPayloadBytes = qsizetype{2 * 1024 * 1024};
    if (json.size() > maximumPayloadBytes) {
        return {.error = QStringLiteral("Saved alert workspace is too large.")};
    }
    QJsonParseError parseError;
    const auto document = QJsonDocument::fromJson(json, &parseError);
    if (parseError.error != QJsonParseError::NoError ||
        !document.isObject()) {
        return {.error = QStringLiteral("Saved alert JSON is invalid.")};
    }
    const auto root = document.object();
    const auto alerts = root.value(QStringLiteral("alerts"));
    const auto history = root.value(QStringLiteral("history"));
    if (root.value(QStringLiteral("schemaVersion")).toInt(-1) !=
            AlertWorkspace::currentSchemaVersion ||
        !alerts.isArray() || !history.isArray() ||
        alerts.toArray().size() >
            static_cast<qsizetype>(AlertWorkspace::maximumAlerts) ||
        history.toArray().size() >
            static_cast<qsizetype>(AlertWorkspace::maximumHistory)) {
        return {.error = QStringLiteral("Saved alert schema is unsupported.")};
    }

    AlertWorkspaceLoadResult result;
    QSet<QString> identities;
    for (const auto& value : alerts.toArray()) {
        if (!value.isObject()) {
            return {.error = QStringLiteral("Saved alert entry is invalid.")};
        }
        const auto object = value.toObject();
        const auto frequency = parseAlertFrequency(
            object.value(QStringLiteral("frequency")).toString());
        bool cooldownOk = false;
        bool expiryOk = false;
        const auto cooldown = object
                                  .value(QStringLiteral("cooldownSeconds"))
                                  .toString()
                                  .toLongLong(&cooldownOk);
        const auto expiry = object
                                .value(QStringLiteral("expiresAtUtc"))
                                .toString()
                                .toLongLong(&expiryOk);
        const auto condition = object.value(QStringLiteral("condition"));
        const auto enabled = object.value(QStringLiteral("enabled"));
        if (!frequency || !cooldownOk || !expiryOk ||
            !condition.isObject() || !enabled.isBool()) {
            return {.error = QStringLiteral("Saved alert fields are invalid.")};
        }
        const auto id = object.value(QStringLiteral("id")).toString();
        const auto name = object.value(QStringLiteral("name")).toString();
        const QJsonObject strategyObject{
            {
                QStringLiteral("schemaVersion"),
                StrategyDefinition::currentSchemaVersion,
            },
            {QStringLiteral("id"), id},
            {
                QStringLiteral("name"),
                name.trimmed().isEmpty() ? id : name,
            },
            {QStringLiteral("entry"), condition},
            {QStringLiteral("exit"), condition},
        };
        const auto strategy = deserializeStrategy(
            QJsonDocument(strategyObject).toJson(QJsonDocument::Compact));
        if (!strategy.ok()) {
            return {.error = strategy.error};
        }
        StrategyAlert alert{
            .id = id,
            .symbol =
                object.value(QStringLiteral("symbol")).toString(),
            .condition = strategy.strategy.entry,
            .enabled = enabled.toBool(),
            .name = name,
            .frequency = *frequency,
            .cooldownSeconds = cooldown,
            .expiresAtUtc = expiry,
        };
        if (const auto error = validateStrategyAlert(alert);
            !error.isEmpty()) {
            return {.error = error};
        }
        if (identities.contains(alert.id.trimmed())) {
            return {.error = QStringLiteral("Saved alert identities must be unique.")};
        }
        identities.insert(alert.id.trimmed());
        result.workspace.alerts.push_back(std::move(alert));
    }
    for (const auto& value : history.toArray()) {
        if (!value.isObject()) {
            return {.error = QStringLiteral("Saved alert history is invalid.")};
        }
        const auto object = value.toObject();
        bool timestampOk = false;
        bool triggeredOk = false;
        AlertTrigger trigger{
            .alertId =
                object.value(QStringLiteral("alertId")).toString(),
            .symbol = object.value(QStringLiteral("symbol")).toString(),
            .timestamp =
                object.value(QStringLiteral("timestamp"))
                    .toString()
                    .toLongLong(&timestampOk),
            .triggeredAtUtc =
                object.value(QStringLiteral("triggeredAtUtc"))
                    .toString()
                    .toLongLong(&triggeredOk),
            .message =
                object.value(QStringLiteral("message")).toString(),
        };
        if (!timestampOk || !triggeredOk ||
            !validAlertTrigger(trigger)) {
            return {.error = QStringLiteral("Saved alert history fields are invalid.")};
        }
        result.workspace.history.push_back(std::move(trigger));
    }
    return result;
}

AlertEvaluation StrategyAlertEngine::evaluate(
    const StrategyAlert& alert,
    const Bars& bars,
    std::int64_t evaluatedAtUtc) {
    static const TimeframeSeries noAdditionalSeries;
    return evaluate(
        alert,
        bars,
        Timeframe::OneMinute,
        noAdditionalSeries,
        evaluatedAtUtc);
}

AlertEvaluation StrategyAlertEngine::evaluate(
    const StrategyAlert& alert,
    const Bars& bars,
    const Timeframe primaryTimeframe,
    const TimeframeSeries& additionalSeries,
    std::int64_t evaluatedAtUtc) {
    if (!alert.enabled) {
        return {};
    }
    if (const auto error = validateStrategyAlert(alert); !error.isEmpty()) {
        return {.error = error};
    }
    if (const auto error = validateBars(bars)) {
        return {.error = QString::fromStdString(*error)};
    }
    if (evaluatedAtUtc <= 0) {
        evaluatedAtUtc = bars.back().timestamp;
    }
    if (evaluatedAtUtc < bars.back().timestamp) {
        return {
            .error =
                QStringLiteral(
                    "Alert evaluation time precedes the completed bar."),
        };
    }
    if (alert.expiresAtUtc > 0 &&
        evaluatedAtUtc > alert.expiresAtUtc) {
        return {.expired = true};
    }
    StrategyEvaluator evaluator(
        bars,
        primaryTimeframe,
        additionalSeries);
    const auto evaluation =
        evaluator.evaluate(alert.condition, bars.size() - 1);
    if (!evaluation.available) {
        return {.unavailable = true};
    }

    const auto symbol = normalizeWatchlistSymbol(alert.symbol);
    const auto identity = alertIdentity(alert, symbol);
    auto& state = states_[identity];
    if (!evaluation.matched) {
        state.lastMatched = false;
        return {};
    }

    auto shouldTrigger = false;
    switch (alert.frequency) {
    case AlertFrequency::Once:
        shouldTrigger = !state.hasTriggered;
        break;
    case AlertFrequency::OncePerBar:
        shouldTrigger =
            state.lastBarTimestamp != evaluation.timestamp;
        break;
    case AlertFrequency::OnTransition:
        shouldTrigger = !state.lastMatched;
        break;
    case AlertFrequency::Cooldown:
        shouldTrigger =
            !state.hasTriggered ||
            (evaluatedAtUtc >= state.lastTriggeredAtUtc &&
             evaluatedAtUtc - state.lastTriggeredAtUtc >=
                 alert.cooldownSeconds);
        break;
    }
    state.lastMatched = true;
    if (!shouldTrigger) {
        return {};
    }
    state.hasTriggered = true;
    state.lastBarTimestamp = evaluation.timestamp;
    state.lastTriggeredAtUtc = evaluatedAtUtc;
    const auto alertName =
        alert.name.trimmed().isEmpty() ? alert.id : alert.name.trimmed();
    AlertTrigger trigger{
        .alertId = alert.id,
        .symbol = symbol,
        .timestamp = evaluation.timestamp,
        .triggeredAtUtc = evaluatedAtUtc,
        .message =
            QStringLiteral("%1 · %2 matched at the latest completed bar.")
                .arg(alertName, symbol),
    };
    auditLog_.push_back(trigger);
    if (auditLog_.size() > AlertWorkspace::maximumHistory) {
        auditLog_.erase(
            auditLog_.begin(),
            auditLog_.begin() +
                static_cast<std::ptrdiff_t>(
                    auditLog_.size() -
                    AlertWorkspace::maximumHistory));
    }
    return {.trigger = std::move(trigger)};
}

const std::vector<AlertTrigger>&
StrategyAlertEngine::auditLog() const noexcept {
    return auditLog_;
}

bool StrategyAlertEngine::recordExternalTrigger(AlertTrigger trigger) {
    if (!validAlertTrigger(trigger)) {
        return false;
    }
    const auto duplicate = std::ranges::any_of(
        auditLog_,
        [&](const AlertTrigger& existing) {
            return existing.alertId == trigger.alertId &&
                   normalizeWatchlistSymbol(existing.symbol) ==
                       normalizeWatchlistSymbol(trigger.symbol) &&
                   existing.timestamp == trigger.timestamp;
        });
    if (duplicate) {
        return false;
    }
    auto& state = states_[
        trigger.alertId.trimmed() + u'|' +
        normalizeWatchlistSymbol(trigger.symbol)];
    state.hasTriggered = true;
    state.lastMatched = true;
    state.lastBarTimestamp = trigger.timestamp;
    state.lastTriggeredAtUtc = trigger.triggeredAtUtc;
    auditLog_.push_back(std::move(trigger));
    if (auditLog_.size() > AlertWorkspace::maximumHistory) {
        auditLog_.erase(auditLog_.begin());
    }
    return true;
}

void StrategyAlertEngine::restoreAuditLog(
    std::vector<AlertTrigger> history) {
    std::erase_if(
        history,
        [](const AlertTrigger& trigger) {
            return !validAlertTrigger(trigger);
        });
    if (history.size() > AlertWorkspace::maximumHistory) {
        history.erase(
            history.begin(),
            history.begin() +
                static_cast<std::ptrdiff_t>(
                    history.size() - AlertWorkspace::maximumHistory));
    }
    states_.clear();
    for (const auto& trigger : history) {
        auto& state = states_[
            trigger.alertId.trimmed() + u'|' +
            normalizeWatchlistSymbol(trigger.symbol)];
        state.hasTriggered = true;
        state.lastMatched = true;
        state.lastBarTimestamp =
            std::max(state.lastBarTimestamp, trigger.timestamp);
        state.lastTriggeredAtUtc =
            std::max(
                state.lastTriggeredAtUtc,
                trigger.triggeredAtUtc);
    }
    auditLog_ = std::move(history);
}

void StrategyAlertEngine::clear() {
    states_.clear();
    auditLog_.clear();
}

} // namespace tvchart
