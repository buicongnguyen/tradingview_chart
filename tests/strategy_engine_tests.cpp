#include "strategy/pine_strategy_importer.hpp"
#include "strategy/replay_session.hpp"
#include "strategy/strategy_engine.hpp"
#include "strategy/strategy_models.hpp"
#include "strategy/theory_validation.hpp"
#include "strategy/trading_simulation.hpp"

#include <QJsonDocument>
#include <QJsonObject>
#include <QTest>

#include <array>
#include <cmath>
#include <iterator>
#include <utility>
#include <vector>

class StrategyEngineTests final : public QObject {
    Q_OBJECT

private slots:
    void roundTripsReusableStrategy();
    void migratesSchemaOneStrategy();
    void roundTripsNamedStrategyLibrary();
    void evaluatesWarmupAndCrossingWithoutLookAhead();
    void resolvesDecisiveMultiConditionGroupsDuringWarmup();
    void reportsIndicatorOverflowWithoutThrowing();
    void executesSignalsAtNextOpenAndAppliesCosts();
    void rejectsImpossibleExitCosts();
    void forceClosesAndReconcilesFinalEquity();
    void calculatesProfessionalTradeAndRiskMetrics();
    void isolatesChronologicalHoldoutTrading();
    void alignsMultiTimeframeValuesWithoutLookAhead();
    void calculatesDeterministicRobustnessReports();
    void scansUnavailableSeriesAndDeduplicatesAlerts();
    void enforcesAlertFrequencyExpiryAndPersistence();
    void replaysWithinDeterministicBoundaries();
    void excludesAStillFormingProviderBar();
    void validatesTheoriesPointInTimeWithoutLookAhead();
    void appliesTheoryCostsAndRejectsInvalidAssumptions();
    void keepsTheoryEpisodesNonOverlappingAndHoldoutChronological();
    void simulatesNextOpenOrdersAcrossInteractionModes();
    void restoresSimulationSnapshotsDeterministically();
    void importsSafePineSubsetAndRejectsUnsupportedSemantics();
};

namespace {

[[nodiscard]] tvchart::Bar bar(
    const std::int64_t timestamp,
    const double open,
    const double close,
    const double volume = 1'000.0) {
    return {
        .timestamp = timestamp,
        .open = open,
        .high = std::max(open, close) + 1.0,
        .low = std::min(open, close) - 1.0,
        .close = close,
        .volume = volume,
    };
}

[[nodiscard]] tvchart::Bars sampleBars() {
    return {
        bar(1'700'000'000, 9.0, 9.0),
        bar(1'700'000'300, 10.0, 10.0),
        bar(1'700'000'600, 20.0, 11.0),
        bar(1'700'000'900, 12.0, 12.0),
        bar(1'700'001'200, 7.0, 8.0),
        bar(1'700'001'500, 6.0, 7.0),
    };
}

[[nodiscard]] tvchart::StrategyDefinition crossingStrategy() {
    return {
        .id = QStringLiteral("close-cross"),
        .name = QStringLiteral("Close crossing"),
        .entry = {
            .conditions = {{
                .left = {.field = tvchart::StrategyField::Close},
                .comparison = tvchart::StrategyComparison::CrossesAbove,
                .constant = 10.0,
            }},
        },
        .exit = {
            .conditions = {{
                .left = {.field = tvchart::StrategyField::Close},
                .comparison = tvchart::StrategyComparison::CrossesBelow,
                .constant = 10.0,
            }},
        },
    };
}

[[nodiscard]] tvchart::Bars theoryBars(
    const std::size_t cycles) {
    constexpr std::array closes{
        90.0,
        101.0,
        102.0,
        103.0,
        104.0,
        105.0,
        106.0,
        99.0,
        98.0,
        97.0,
        96.0,
        95.0,
        94.0,
    };
    tvchart::Bars bars;
    bars.reserve(cycles * closes.size());
    for (std::size_t cycle = 0; cycle < cycles; ++cycle) {
        for (std::size_t offset = 0;
             offset < closes.size();
             ++offset) {
            const auto index =
                cycle * closes.size() + offset;
            bars.push_back(bar(
                1'700'000'000 +
                    static_cast<std::int64_t>(index) * 86'400,
                closes[offset],
                closes[offset]));
        }
    }
    return bars;
}

[[nodiscard]] tvchart::StrategyDefinition theory(
    QString id,
    QString name,
    const tvchart::StrategyComparison entryComparison) {
    auto result = crossingStrategy();
    result.id = std::move(id);
    result.name = std::move(name);
    result.entry.conditions.front().comparison = entryComparison;
    result.exit.conditions.front().comparison =
        entryComparison == tvchart::StrategyComparison::CrossesAbove
            ? tvchart::StrategyComparison::CrossesBelow
            : tvchart::StrategyComparison::CrossesAbove;
    result.entry.conditions.front().constant = 100.0;
    result.exit.conditions.front().constant = 100.0;
    return result;
}

} // namespace

void StrategyEngineTests::roundTripsReusableStrategy() {
    auto strategy = crossingStrategy();
    strategy.entry.conditions.push_back({
        .left = {
            .field = tvchart::StrategyField::VolumeRatio,
            .period = 3,
        },
        .comparison = tvchart::StrategyComparison::GreaterThan,
        .constant = 1.2,
    });
    const auto loaded =
        tvchart::deserializeStrategy(tvchart::serializeStrategy(strategy));
    QVERIFY2(loaded.ok(), qPrintable(loaded.error));
    QCOMPARE(loaded.strategy, strategy);
}

void StrategyEngineTests::migratesSchemaOneStrategy() {
    auto document = QJsonDocument::fromJson(
        tvchart::serializeStrategy(crossingStrategy()));
    auto object = document.object();
    object.insert(QStringLiteral("schemaVersion"), 1);
    const auto loaded = tvchart::deserializeStrategy(
        QJsonDocument(object).toJson(QJsonDocument::Compact));
    QVERIFY2(loaded.ok(), qPrintable(loaded.error));
    QCOMPARE(loaded.strategy, crossingStrategy());
    QVERIFY(
        !loaded.strategy.entry.conditions.front().left.timeframe
             .has_value());
}

void StrategyEngineTests::roundTripsNamedStrategyLibrary() {
    auto first = crossingStrategy();
    auto second = crossingStrategy();
    second.id = QStringLiteral("second");
    second.name = QStringLiteral("Second strategy");
    second.entry.match = tvchart::ConditionMatch::Any;
    const std::vector strategies{first, second};

    const auto serialized = tvchart::serializeStrategyLibrary(strategies);
    QVERIFY(!serialized.isEmpty());
    const auto loaded = tvchart::deserializeStrategyLibrary(serialized);
    QVERIFY2(loaded.ok(), qPrintable(loaded.error));
    QCOMPARE(loaded.strategies, strategies);

    second.id = first.id;
    QVERIFY(tvchart::serializeStrategyLibrary({first, second}).isEmpty());
}

void StrategyEngineTests::evaluatesWarmupAndCrossingWithoutLookAhead() {
    const auto bars = sampleBars();
    tvchart::StrategyEvaluator evaluator(bars);
    const auto strategy = crossingStrategy();

    const auto before = evaluator.evaluate(strategy.entry, 1);
    QVERIFY(before.available);
    QVERIFY(!before.matched);
    const auto crossing = evaluator.evaluate(strategy.entry, 2);
    QVERIFY(crossing.available);
    QVERIFY(crossing.matched);

    const tvchart::ConditionGroup warmup{
        .conditions = {{
            .left = {
                .field = tvchart::StrategyField::SimpleMovingAverage,
                .period = 20,
            },
            .comparison = tvchart::StrategyComparison::GreaterThan,
            .constant = 0.0,
        }},
    };
    const auto unavailable = evaluator.evaluate(warmup, bars.size() - 1);
    QVERIFY(!unavailable.available);
    QVERIFY(unavailable.detail.contains(QStringLiteral("warm-up")));
}

void StrategyEngineTests::resolvesDecisiveMultiConditionGroupsDuringWarmup() {
    const auto bars = sampleBars();
    tvchart::StrategyEvaluator evaluator(bars);
    const tvchart::StrategyCondition warmingRsi{
        .left = {
            .field = tvchart::StrategyField::RelativeStrengthIndex,
            .period = 14,
        },
        .comparison = tvchart::StrategyComparison::GreaterThan,
        .constant = 50.0,
    };
    const tvchart::StrategyCondition positiveClose{
        .left = {.field = tvchart::StrategyField::Close},
        .comparison = tvchart::StrategyComparison::GreaterThan,
        .constant = 0.0,
    };
    const tvchart::StrategyCondition impossibleClose{
        .left = {.field = tvchart::StrategyField::Close},
        .comparison = tvchart::StrategyComparison::LessThan,
        .constant = 0.0,
    };

    const auto any = evaluator.evaluate(
        {
            .match = tvchart::ConditionMatch::Any,
            .conditions = {warmingRsi, positiveClose},
        },
        bars.size() - 1);
    QVERIFY(any.available);
    QVERIFY(any.matched);

    const auto all = evaluator.evaluate(
        {
            .match = tvchart::ConditionMatch::All,
            .conditions = {warmingRsi, impossibleClose},
        },
        bars.size() - 1);
    QVERIFY(all.available);
    QVERIFY(!all.matched);

    const auto undecidable = evaluator.evaluate(
        {
            .match = tvchart::ConditionMatch::Any,
            .conditions = {warmingRsi, impossibleClose},
        },
        bars.size() - 1);
    QVERIFY(!undecidable.available);
    QVERIFY(undecidable.detail.contains(QStringLiteral("warm-up")));
}

void StrategyEngineTests::executesSignalsAtNextOpenAndAppliesCosts() {
    const auto bars = sampleBars();
    const auto strategy = crossingStrategy();
    const auto withoutCosts = tvchart::runBacktest(
        bars,
        strategy,
        {.initialCapital = 100'000.0});
    QVERIFY2(withoutCosts.ok(), qPrintable(withoutCosts.error));
    QCOMPARE(withoutCosts.trades.size(), std::size_t{1});
    QCOMPARE(withoutCosts.trades.front().entryTimestamp, bars.at(3).timestamp);
    QCOMPARE(withoutCosts.trades.front().entryPrice, bars.at(3).open);
    QCOMPARE(withoutCosts.trades.front().exitTimestamp, bars.at(5).timestamp);
    QCOMPARE(withoutCosts.trades.front().exitPrice, bars.at(5).open);
    QVERIFY(std::abs(withoutCosts.finalEquity - 50'000.0) < 1.0e-7);
    QVERIFY(
        std::abs(withoutCosts.maximumDrawdownPercent - 50.0) < 1.0e-7);

    const auto withCosts = tvchart::runBacktest(
        bars,
        strategy,
        {
            .initialCapital = 1'000.0,
            .commissionPerSide = 10.0,
            .slippageBasisPoints = 100.0,
        });
    QVERIFY2(withCosts.ok(), qPrintable(withCosts.error));
    QCOMPARE(withCosts.trades.size(), std::size_t{1});
    QVERIFY(std::abs(withCosts.trades.front().entryPrice - 12.12) < 1.0e-10);
    QVERIFY(std::abs(withCosts.trades.front().exitPrice - 5.94) < 1.0e-10);
    QCOMPARE(withCosts.trades.front().entryCommission, 10.0);
    QCOMPARE(withCosts.trades.front().exitCommission, 10.0);
    QVERIFY(withCosts.finalEquity < withoutCosts.finalEquity / 100.0);
}

void StrategyEngineTests::reportsIndicatorOverflowWithoutThrowing() {
    const tvchart::Bars bars{
        {
            .timestamp = 1'700'000'000,
            .open = 1.0e308,
            .high = 1.0e308,
            .low = 1.0e308,
            .close = 1.0e308,
            .volume = 1.0,
        },
        {
            .timestamp = 1'700'000'300,
            .open = 1.0e308,
            .high = 1.0e308,
            .low = 1.0e308,
            .close = 1.0e308,
            .volume = 1.0,
        },
    };
    tvchart::StrategyEvaluator evaluator(bars);
    const tvchart::ConditionGroup group{
        .conditions = {{
            .left = {
                .field = tvchart::StrategyField::SimpleMovingAverage,
                .period = 2,
            },
            .comparison = tvchart::StrategyComparison::GreaterThan,
            .constant = 0.0,
        }},
    };
    const auto evaluation = evaluator.evaluate(group, 1);
    QVERIFY(!evaluation.available);
    QVERIFY(evaluation.detail.contains(QStringLiteral("failed")));
}

void StrategyEngineTests::forceClosesAndReconcilesFinalEquity() {
    auto strategy = crossingStrategy();
    strategy.exit.conditions.front().comparison =
        tvchart::StrategyComparison::LessThan;
    strategy.exit.conditions.front().constant = -100.0;
    const auto result = tvchart::runBacktest(
        sampleBars(),
        strategy,
        {
            .initialCapital = 10'000.0,
            .commissionPerSide = 5.0,
            .slippageBasisPoints = 25.0,
        });
    QVERIFY2(result.ok(), qPrintable(result.error));
    QCOMPARE(result.trades.size(), std::size_t{1});
    QVERIFY(result.trades.front().forcedExit);
    QCOMPARE(result.finalEquity, result.equityCurve.back().equity);
    QCOMPARE(
        result.netProfit,
        result.trades.front().profitLoss);
}

void StrategyEngineTests::calculatesProfessionalTradeAndRiskMetrics() {
    const auto result = tvchart::runBacktest(
        sampleBars(),
        crossingStrategy(),
        {.initialCapital = 100'000.0});
    QVERIFY2(result.ok(), qPrintable(result.error));
    QCOMPARE(result.trades.size(), std::size_t{1});
    const auto& trade = result.trades.front();
    QCOMPARE(trade.barsHeld, std::size_t{2});
    QVERIFY(
        std::abs(trade.maximumFavorableExcursionPercent - 100.0 / 12.0) <
        1.0e-9);
    QVERIFY(
        std::abs(trade.maximumAdverseExcursionPercent + 50.0) <
        1.0e-9);
    QCOMPARE(result.maximumConsecutiveLosses, std::size_t{1});
    QCOMPARE(result.averageTradeReturnPercent, trade.returnPercent);
    QVERIFY(result.buyAndHoldReturnPercent < 0.0);

    tvchart::Bars daily;
    for (std::int64_t day = 0; day < 40; ++day) {
        daily.push_back(bar(
            1'700'000'000 + day * 86'400,
            100.0 + static_cast<double>(day),
            101.0 + static_cast<double>(day)));
    }
    auto alwaysLong = crossingStrategy();
    alwaysLong.entry.conditions.front().comparison =
        tvchart::StrategyComparison::GreaterThan;
    alwaysLong.entry.conditions.front().constant = 0.0;
    alwaysLong.exit.conditions.front().comparison =
        tvchart::StrategyComparison::LessThan;
    alwaysLong.exit.conditions.front().constant = 0.0;
    const auto annualized = tvchart::runBacktest(
        daily,
        alwaysLong,
        {.initialCapital = 10'000.0});
    QVERIFY2(annualized.ok(), qPrintable(annualized.error));
    QVERIFY(annualized.compoundAnnualGrowthRatePercent.has_value());
    QVERIFY(annualized.compoundAnnualGrowthRatePercent.value() > 0.0);
}

void StrategyEngineTests::isolatesChronologicalHoldoutTrading() {
    tvchart::Bars daily;
    for (std::int64_t day = 0; day < 20; ++day) {
        daily.push_back(bar(
            1'700'000'000 + day * 86'400,
            100.0 + static_cast<double>(day),
            101.0 + static_cast<double>(day)));
    }
    auto strategy = crossingStrategy();
    strategy.entry.conditions.front().comparison =
        tvchart::StrategyComparison::GreaterThan;
    strategy.entry.conditions.front().constant = 0.0;
    strategy.exit.conditions.front().comparison =
        tvchart::StrategyComparison::LessThan;
    strategy.exit.conditions.front().constant = 0.0;

    const auto validation = tvchart::runHoldoutBacktest(
        daily,
        strategy,
        {.initialCapital = 10'000.0},
        30.0);
    QVERIFY2(validation.ok(), qPrintable(validation.error));
    QVERIFY(!validation.training.trades.empty());
    QVERIFY(!validation.holdout.trades.empty());
    QVERIFY(
        validation.training.trades.back().exitTimestamp <
        validation.splitTimestamp);
    QVERIFY(
        validation.holdout.trades.front().entryTimestamp >=
        validation.splitTimestamp);
    QCOMPARE(
        validation.holdout.equityCurve.front().timestamp,
        validation.splitTimestamp);
}

void StrategyEngineTests::alignsMultiTimeframeValuesWithoutLookAhead() {
    tvchart::Bars primary;
    for (std::int64_t index = 0; index < 7; ++index) {
        primary.push_back(bar(
            1'700'000'000 + index * 300,
            10.0,
            10.0));
    }
    const tvchart::TimeframeSeries additional{{
        tvchart::Timeframe::FifteenMinutes,
        {
            bar(1'700'000'000, 50.0, 50.0),
            bar(1'700'000'900, 200.0, 200.0),
        },
    }};
    const tvchart::ConditionGroup group{
        .conditions = {{
            .left = {
                .field = tvchart::StrategyField::Close,
                .timeframe = tvchart::Timeframe::FifteenMinutes,
            },
            .comparison = tvchart::StrategyComparison::GreaterThan,
            .constant = 100.0,
        }},
    };
    tvchart::StrategyEvaluator evaluator(
        primary,
        tvchart::Timeframe::FiveMinutes,
        additional);
    QVERIFY(!evaluator.evaluate(group, 1).available);
    const auto firstClosed = evaluator.evaluate(group, 2);
    QVERIFY(firstClosed.available);
    QVERIFY(!firstClosed.matched);
    const auto secondStillForming = evaluator.evaluate(group, 4);
    QVERIFY(secondStillForming.available);
    QVERIFY(!secondStillForming.matched);
    const auto secondClosed = evaluator.evaluate(group, 5);
    QVERIFY(secondClosed.available);
    QVERIFY(secondClosed.matched);

    const tvchart::TimeframeSeries missing;
    tvchart::StrategyEvaluator unavailable(
        primary,
        tvchart::Timeframe::FiveMinutes,
        missing);
    QVERIFY(!unavailable.evaluate(group, 5).available);
}

void StrategyEngineTests::calculatesDeterministicRobustnessReports() {
    tvchart::Bars bars;
    for (std::int64_t index = 0; index < 160; ++index) {
        const auto phase = index % 8;
        const auto close =
            phase < 4
                ? 90.0 + static_cast<double>(phase) * 7.0
                : 111.0 - static_cast<double>(phase - 4) * 7.0;
        bars.push_back(bar(
            1'700'000'000 + index * 86'400,
            close,
            close));
    }
    auto strategy = crossingStrategy();
    strategy.entry.conditions.front().constant = 100.0;
    strategy.exit.conditions.front().constant = 100.0;
    const tvchart::BacktestParameters parameters{
        .initialCapital = 10'000.0,
    };
    const tvchart::TimeframeSeries noAdditional;
    const auto base = tvchart::runBacktest(
        bars,
        tvchart::Timeframe::OneDay,
        noAdditional,
        strategy,
        parameters);
    QVERIFY2(base.ok(), qPrintable(base.error));
    QVERIFY(base.trades.size() >= 5);

    const auto walk = tvchart::runWalkForwardAnalysis(
        bars,
        tvchart::Timeframe::OneDay,
        noAdditional,
        strategy,
        parameters,
        4);
    QVERIFY2(walk.ok(), qPrintable(walk.error));
    QCOMPARE(walk.folds.size(), std::size_t{4});
    for (std::size_t index = 1; index < walk.folds.size(); ++index) {
        QCOMPARE(
            walk.folds[index - 1].endIndex,
            walk.folds[index].startIndex);
    }
    QCOMPARE(walk.folds.back().endIndex, bars.size());

    const auto firstMonteCarlo =
        tvchart::runTradeMonteCarlo(base, 1'000, 42);
    const auto secondMonteCarlo =
        tvchart::runTradeMonteCarlo(base, 1'000, 42);
    QVERIFY2(firstMonteCarlo.ok(), qPrintable(firstMonteCarlo.error));
    QCOMPARE(
        firstMonteCarlo.medianTerminalReturnPercent,
        secondMonteCarlo.medianTerminalReturnPercent);
    QCOMPARE(
        firstMonteCarlo.percentile95MaximumDrawdownPercent,
        secondMonteCarlo.percentile95MaximumDrawdownPercent);

    auto indicatorStrategy = strategy;
    indicatorStrategy.entry.conditions.front().right =
        tvchart::StrategyOperand{
            .field = tvchart::StrategyField::SimpleMovingAverage,
            .period = 10,
        };
    indicatorStrategy.entry.conditions.front().comparison =
        tvchart::StrategyComparison::GreaterThan;
    const auto original = indicatorStrategy;
    const auto stability = tvchart::runPrimaryPeriodStability(
        bars,
        tvchart::Timeframe::OneDay,
        noAdditional,
        indicatorStrategy,
        parameters,
        {5, 10, 20});
    QVERIFY2(stability.ok(), qPrintable(stability.error));
    QCOMPARE(stability.points.size(), std::size_t{3});
    QCOMPARE(indicatorStrategy, original);

    const auto regimes =
        tvchart::analyzeTradeRegimes(bars, base);
    QVERIFY2(regimes.ok(), qPrintable(regimes.error));
    auto attributed = regimes.unavailableTrades;
    for (const auto& regime : regimes.regimes) {
        attributed += regime.trades;
    }
    QCOMPARE(attributed, base.trades.size());

    const auto batch = tvchart::runStrategyBatch(
        {
            {
                .symbol = QStringLiteral("ONE"),
                .provider = QStringLiteral("Fixture"),
                .bars = bars,
                .timeframe = tvchart::Timeframe::OneDay,
            },
            {
                .symbol = QStringLiteral("EMPTY"),
                .provider = QStringLiteral("Fixture"),
                .timeframe = tvchart::Timeframe::OneDay,
            },
        },
        strategy,
        parameters);
    QCOMPARE(batch.size(), std::size_t{2});
    QVERIFY(batch.front().result.ok());
    QVERIFY(!batch.back().result.ok());
}

void StrategyEngineTests::rejectsImpossibleExitCosts() {
    const auto result = tvchart::runBacktest(
        sampleBars(),
        crossingStrategy(),
        {
            .initialCapital = 100.0,
            .commissionPerSide = 40.0,
        });
    QVERIFY(!result.ok());
    QVERIFY(result.error.contains(QStringLiteral("cash balance")));
}

void StrategyEngineTests::scansUnavailableSeriesAndDeduplicatesAlerts() {
    auto bars = sampleBars();
    const tvchart::ConditionGroup warmup{
        .conditions = {{
            .left = {
                .field = tvchart::StrategyField::RelativeStrengthIndex,
                .period = 14,
            },
            .comparison = tvchart::StrategyComparison::GreaterThan,
            .constant = 50.0,
        }},
    };
    const auto unavailable = tvchart::scanLatest(
        {{
            .symbol = QStringLiteral("AAPL"),
            .provider = QStringLiteral("Yahoo Finance"),
            .bars = bars,
        }},
        warmup);
    QCOMPARE(unavailable.front().status, tvchart::ScanStatus::Unavailable);

    const tvchart::ConditionGroup positiveClose{
        .conditions = {{
            .left = {.field = tvchart::StrategyField::Close},
            .comparison = tvchart::StrategyComparison::GreaterThan,
            .constant = 0.0,
        }},
    };
    tvchart::StrategyAlertEngine alerts;
    const tvchart::StrategyAlert alert{
        .id = QStringLiteral("positive-close"),
        .symbol = QStringLiteral("AAPL"),
        .condition = positiveClose,
    };
    const auto first = alerts.evaluate(alert, bars);
    QVERIFY(first.trigger.has_value());
    QVERIFY(!alerts.evaluate(alert, bars).trigger.has_value());

    bars.push_back(bar(1'700'001'800, 8.0, 8.5));
    const auto nextBar = alerts.evaluate(alert, bars);
    QVERIFY(nextBar.trigger.has_value());
    QCOMPARE(alerts.auditLog().size(), std::size_t{2});
}

void StrategyEngineTests::enforcesAlertFrequencyExpiryAndPersistence() {
    const tvchart::ConditionGroup positiveClose{
        .conditions = {{
            .left = {.field = tvchart::StrategyField::Close},
            .comparison = tvchart::StrategyComparison::GreaterThan,
            .constant = 0.0,
        }},
    };
    const auto bars = sampleBars();
    const auto now = bars.back().timestamp + 60;

    tvchart::StrategyAlertEngine onceEngine;
    const tvchart::StrategyAlert once{
        .id = QStringLiteral("once"),
        .symbol = QStringLiteral("AAPL"),
        .condition = positiveClose,
        .name = QStringLiteral("Only once"),
        .frequency = tvchart::AlertFrequency::Once,
    };
    const auto onceTrigger = onceEngine.evaluate(once, bars, now);
    QVERIFY(onceTrigger.trigger.has_value());
    QVERIFY(!onceEngine.evaluate(once, bars, now + 60).trigger.has_value());

    tvchart::StrategyAlertEngine cooldownEngine;
    const tvchart::StrategyAlert cooldown{
        .id = QStringLiteral("cooldown"),
        .symbol = QStringLiteral("AAPL"),
        .condition = positiveClose,
        .name = QStringLiteral("Cooldown"),
        .frequency = tvchart::AlertFrequency::Cooldown,
        .cooldownSeconds = 300,
    };
    QVERIFY(cooldownEngine.evaluate(cooldown, bars, now).trigger.has_value());
    QVERIFY(
        !cooldownEngine.evaluate(cooldown, bars, now + 299)
             .trigger.has_value());
    QVERIFY(
        cooldownEngine.evaluate(cooldown, bars, now + 300)
            .trigger.has_value());

    auto expiring = cooldown;
    expiring.id = QStringLiteral("expiry");
    expiring.frequency = tvchart::AlertFrequency::OncePerBar;
    expiring.cooldownSeconds = 0;
    expiring.expiresAtUtc = now + 10;
    const auto expired =
        cooldownEngine.evaluate(expiring, bars, now + 11);
    QVERIFY(expired.expired);
    QVERIFY(!expired.trigger.has_value());

    tvchart::StrategyAlertEngine transitionEngine;
    auto transition = once;
    transition.id = QStringLiteral("transition");
    transition.frequency = tvchart::AlertFrequency::OnTransition;
    transition.condition.conditions.front().constant = 7.5;
    auto transitionBars = bars;
    transitionBars.push_back(bar(
        bars.back().timestamp + 300,
        8.0,
        8.0));
    QVERIFY(
        transitionEngine.evaluate(transition, transitionBars, now + 300)
            .trigger.has_value());
    transitionBars.push_back(bar(
        bars.back().timestamp + 600,
        7.0,
        7.0));
    QVERIFY(
        !transitionEngine.evaluate(transition, transitionBars, now + 600)
             .trigger.has_value());
    transitionBars.push_back(bar(
        bars.back().timestamp + 900,
        8.0,
        8.0));
    QVERIFY(
        transitionEngine.evaluate(transition, transitionBars, now + 900)
            .trigger.has_value());

    const tvchart::AlertWorkspace workspace{
        .alerts = {once, cooldown, transition},
        .history = cooldownEngine.auditLog(),
    };
    const auto serialized = tvchart::serializeAlertWorkspace(workspace);
    QVERIFY(!serialized.isEmpty());
    const auto loaded = tvchart::deserializeAlertWorkspace(serialized);
    QVERIFY2(loaded.ok(), qPrintable(loaded.error));
    QCOMPARE(loaded.workspace.alerts.size(), std::size_t{3});
    QCOMPARE(
        loaded.workspace.history.size(),
        cooldownEngine.auditLog().size());

    tvchart::StrategyAlertEngine restored;
    restored.restoreAuditLog(loaded.workspace.history);
    QVERIFY(
        !restored.evaluate(cooldown, bars, now + 301)
             .trigger.has_value());

    tvchart::AlertTrigger external{
        .alertId = QStringLiteral("event-id"),
        .symbol = QStringLiteral("MARKET"),
        .timestamp = now + 86'400,
        .triggeredAtUtc = now,
        .message = QStringLiteral("Sourced event reminder"),
    };
    QVERIFY(restored.recordExternalTrigger(external));
    QVERIFY(!restored.recordExternalTrigger(external));
}

void StrategyEngineTests::replaysWithinDeterministicBoundaries() {
    const auto bars = sampleBars();
    tvchart::ReplaySession replay;
    QVERIFY(replay.reset(bars, 2).isEmpty());
    QCOMPARE(replay.visibleBars(), tvchart::Bars({bars[0], bars[1]}));
    QVERIFY(replay.step(2));
    QCOMPARE(replay.visibleCount(), std::size_t{4});
    QCOMPARE(replay.currentTimestamp(), bars[3].timestamp);
    QVERIFY(replay.step(100));
    QVERIFY(replay.finished());
    QCOMPARE(replay.visibleBars(), bars);
    QVERIFY(!replay.step());
}

void StrategyEngineTests::excludesAStillFormingProviderBar() {
    const auto bars = sampleBars();
    const auto finalStart = bars.back().timestamp;
    QCOMPARE(
        tvchart::completedBarCount(
            bars,
            tvchart::Timeframe::FiveMinutes,
            finalStart + 299),
        bars.size() - 1);
    QCOMPARE(
        tvchart::completedBarCount(
            bars,
            tvchart::Timeframe::FiveMinutes,
            finalStart + 300),
        bars.size());
}

void StrategyEngineTests::validatesTheoriesPointInTimeWithoutLookAhead() {
    const auto bars = theoryBars(60);
    constexpr auto barsPerCycle = std::size_t{13};
    const auto selectedIndex = 50 * barsPerCycle + 1;
    const auto selectedTimestamp = bars[selectedIndex].timestamp;
    const std::vector theories{
        theory(
            QStringLiteral("profitable"),
            QStringLiteral("Profitable cross"),
            tvchart::StrategyComparison::CrossesAbove),
        theory(
            QStringLiteral("unprofitable"),
            QStringLiteral("Unprofitable cross"),
            tvchart::StrategyComparison::CrossesBelow),
    };
    const tvchart::TheoryValidationInput input{
        .symbol = QStringLiteral("TEST"),
        .bars = bars,
        .primaryTimeframe = tvchart::Timeframe::OneDay,
        .theories = theories,
        .analysisThroughTimestamp = selectedTimestamp,
        .shortHorizonBars = 2,
        .longHorizonBars = 5,
        .holdoutPercent = 30.0,
        .minimumSamples = 10,
    };
    const auto full = tvchart::validateTheories(input);
    QVERIFY2(full.ok(), qPrintable(full.error));
    QCOMPARE(full.asOfTimestamp, selectedTimestamp);
    QCOMPARE(full.barsAnalyzed, selectedIndex + 1);
    QCOMPARE(full.results.size(), std::size_t{2});
    QCOMPARE(full.recommendedTheoryId, QStringLiteral("profitable"));

    const auto& profitable = full.results.front();
    QVERIFY(profitable.evaluationAvailable);
    QVERIFY(profitable.matchesAtAsOf);
    QCOMPARE(profitable.longHorizon.samples, std::size_t{50});
    QCOMPARE(profitable.longHorizon.positiveOutcomes, std::size_t{50});
    QCOMPARE(
        profitable.evidence,
        tvchart::TheoryEvidence::Positive);
    QCOMPARE(
        profitable.reliability,
        tvchart::TheoryReliability::Low);
    QVERIFY(profitable.longHorizon.holdoutSamples >= 8);
    QVERIFY(
        profitable.longHorizon.holdoutConfidenceLowerPercent >= 0.0);
    QVERIFY(
        profitable.longHorizon.holdoutConfidenceUpperPercent <= 100.0);

    const auto& unprofitable = full.results.back();
    QVERIFY(!unprofitable.matchesAtAsOf);
    QCOMPARE(
        unprofitable.evidence,
        tvchart::TheoryEvidence::Negative);

    auto prefixInput = input;
    prefixInput.bars = {
        bars.begin(),
        std::next(
            bars.begin(),
            static_cast<std::ptrdiff_t>(selectedIndex + 1)),
    };
    const auto prefix = tvchart::validateTheories(prefixInput);
    QVERIFY2(prefix.ok(), qPrintable(prefix.error));
    QCOMPARE(prefix.results.size(), full.results.size());
    for (std::size_t index = 0;
         index < full.results.size();
         ++index) {
        QCOMPARE(
            prefix.results[index].longHorizon.samples,
            full.results[index].longHorizon.samples);
        QCOMPARE(
            prefix.results[index].longHorizon.averageReturnPercent,
            full.results[index].longHorizon.averageReturnPercent);
        QCOMPARE(
            prefix.results[index].longHorizon.holdoutHitRatePercent,
            full.results[index].longHorizon.holdoutHitRatePercent);
        QCOMPARE(
            prefix.results[index].evidence,
            full.results[index].evidence);
    }
}

void StrategyEngineTests::appliesTheoryCostsAndRejectsInvalidAssumptions() {
    const auto bars = theoryBars(20);
    tvchart::TheoryValidationInput input{
        .symbol = QStringLiteral("TEST"),
        .bars = bars,
        .primaryTimeframe = tvchart::Timeframe::OneDay,
        .theories = {
            theory(
                QStringLiteral("profitable"),
                QStringLiteral("Profitable cross"),
                tvchart::StrategyComparison::CrossesAbove),
        },
        .analysisThroughTimestamp = bars.back().timestamp,
        .shortHorizonBars = 2,
        .longHorizonBars = 5,
        .holdoutPercent = 30.0,
        .minimumSamples = 10,
    };
    const auto withoutCosts = tvchart::validateTheories(input);
    QVERIFY2(withoutCosts.ok(), qPrintable(withoutCosts.error));
    input.roundTripCostBasisPoints = 100.0;
    const auto withCosts = tvchart::validateTheories(input);
    QVERIFY2(withCosts.ok(), qPrintable(withCosts.error));
    QVERIFY(
        withCosts.results.front().longHorizon.averageReturnPercent <
        withoutCosts.results.front().longHorizon.averageReturnPercent);

    input.holdoutPercent = 5.0;
    const auto invalid = tvchart::validateTheories(input);
    QVERIFY(!invalid.ok());
    QVERIFY(invalid.error.contains(QStringLiteral("assumptions")));
}

void StrategyEngineTests::
keepsTheoryEpisodesNonOverlappingAndHoldoutChronological() {
    tvchart::Bars frequentSignals;
    constexpr std::array frequentCloses{
        90.0,
        101.0,
        102.0,
        90.0,
    };
    for (std::size_t index = 0; index < 40; ++index) {
        const auto close =
            frequentCloses[index % frequentCloses.size()];
        frequentSignals.push_back(bar(
            1'700'000'000 +
                static_cast<std::int64_t>(index) * 86'400,
            close,
            close));
    }
    auto input = tvchart::TheoryValidationInput{
        .symbol = QStringLiteral("TEST"),
        .bars = frequentSignals,
        .primaryTimeframe = tvchart::Timeframe::OneDay,
        .theories = {
            theory(
                QStringLiteral("frequent"),
                QStringLiteral("Frequent cross"),
                tvchart::StrategyComparison::CrossesAbove),
        },
        .analysisThroughTimestamp =
            frequentSignals.back().timestamp,
        .shortHorizonBars = 2,
        .longHorizonBars = 5,
        .holdoutPercent = 30.0,
        .minimumSamples = 10,
    };
    const auto nonOverlapping = tvchart::validateTheories(input);
    QVERIFY2(
        nonOverlapping.ok(),
        qPrintable(nonOverlapping.error));
    QCOMPARE(
        nonOverlapping.results.front().longHorizon.samples,
        std::size_t{5});

    tvchart::Bars chronological;
    constexpr std::array positiveCycle{
        90.0,
        101.0,
        102.0,
        103.0,
        104.0,
        105.0,
        106.0,
        99.0,
        98.0,
        97.0,
        96.0,
        95.0,
        94.0,
    };
    constexpr std::array negativeCycle{
        90.0,
        101.0,
        99.0,
        98.0,
        97.0,
        96.0,
        95.0,
        94.0,
        93.0,
        92.0,
        91.0,
        90.0,
        89.0,
    };
    for (std::size_t cycle = 0; cycle < 10; ++cycle) {
        const auto& closes =
            cycle < 7 ? positiveCycle : negativeCycle;
        for (const auto close : closes) {
            chronological.push_back(bar(
                1'800'000'000 +
                    static_cast<std::int64_t>(
                        chronological.size()) *
                        86'400,
                close,
                close));
        }
    }
    input.bars = chronological;
    input.analysisThroughTimestamp =
        chronological.back().timestamp;
    const auto chronologicalReport =
        tvchart::validateTheories(input);
    QVERIFY2(
        chronologicalReport.ok(),
        qPrintable(chronologicalReport.error));
    const auto& metrics =
        chronologicalReport.results.front().longHorizon;
    QCOMPARE(metrics.samples, std::size_t{10});
    QCOMPARE(metrics.trainingSamples, std::size_t{7});
    QCOMPARE(metrics.holdoutSamples, std::size_t{3});
    QVERIFY(metrics.trainingAverageReturnPercent > 0.0);
    QVERIFY(metrics.holdoutAverageReturnPercent < 0.0);
}

void StrategyEngineTests::
simulatesNextOpenOrdersAcrossInteractionModes() {
    const auto bars = sampleBars();
    const tvchart::SimulationConfig automaticConfig{
        .strategy = crossingStrategy(),
        .execution = {
            .initialCapital = 1'000.0,
            .allocationPercent = 100.0,
            .commissionPerSide = 1.0,
            .slippageBasisPoints = 100.0,
        },
        .mode = tvchart::SimulationMode::Automatic,
        .startIndex = 1,
    };
    tvchart::TradingSimulationSession automatic;
    auto error = automatic.reset(
        bars,
        tvchart::Timeframe::FiveMinutes,
        {},
        automaticConfig);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QCOMPARE(
        automatic.account().pendingAction,
        tvchart::SimulationAction::None);

    error = automatic.step();
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QCOMPARE(
        automatic.account().currentTimestamp,
        bars[2].timestamp);
    QCOMPARE(
        automatic.account().pendingAction,
        tvchart::SimulationAction::Enter);
    QCOMPARE(automatic.account().quantity, 0.0);

    const tvchart::Bars prefix(
        bars.cbegin(),
        bars.cbegin() + 4);
    tvchart::TradingSimulationSession withoutFutureBars;
    error = withoutFutureBars.reset(
        prefix,
        tvchart::Timeframe::FiveMinutes,
        {},
        automaticConfig);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    error = withoutFutureBars.step();
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QCOMPARE(
        withoutFutureBars.account().pendingAction,
        automatic.account().pendingAction);
    QCOMPARE(
        withoutFutureBars.account().ruleDetail,
        automatic.account().ruleDetail);
    QCOMPARE(
        withoutFutureBars.account().equity,
        automatic.account().equity);

    error = automatic.step();
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QVERIFY(automatic.account().quantity > 0.0);
    QCOMPARE(automatic.account().entryPrice, 12.12);
    QCOMPARE(
        automatic.account().currentTimestamp,
        bars[3].timestamp);

    error = automatic.step();
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QCOMPARE(
        automatic.account().pendingAction,
        tvchart::SimulationAction::Exit);
    error = automatic.step();
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QCOMPARE(automatic.trades().size(), std::size_t{1});
    QCOMPARE(
        automatic.trades().front().entryTimestamp,
        bars[3].timestamp);
    QCOMPARE(
        automatic.trades().front().exitTimestamp,
        bars[5].timestamp);
    QCOMPARE(automatic.trades().front().entryPrice, 12.12);
    QCOMPARE(automatic.trades().front().exitPrice, 5.94);
    QCOMPARE(automatic.account().quantity, 0.0);
    QVERIFY(automatic.account().finished);
    QVERIFY(std::abs(
                automatic.account().equity -
                automatic.account().cash) <
            1.0e-9);
    QVERIFY(std::abs(
                automatic.account().equity -
                automatic.account().initialCapital -
                automatic.account().realizedProfitLoss -
                automatic.account().unrealizedProfitLoss) <
            1.0e-9);

    auto manualConfig = automaticConfig;
    manualConfig.mode = tvchart::SimulationMode::Manual;
    manualConfig.startIndex = 2;
    manualConfig.execution.slippageBasisPoints = 0.0;
    tvchart::TradingSimulationSession manual;
    error = manual.reset(
        bars,
        tvchart::Timeframe::FiveMinutes,
        {},
        manualConfig);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QCOMPARE(
        manual.account().pendingAction,
        tvchart::SimulationAction::None);
    error = manual.requestManualAction(
        tvchart::SimulationAction::Enter);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    error = manual.step();
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QVERIFY(manual.account().quantity > 0.0);
    QCOMPARE(manual.account().entryPrice, bars[3].open);

    auto assistedConfig = manualConfig;
    assistedConfig.mode = tvchart::SimulationMode::Assisted;
    tvchart::TradingSimulationSession expires;
    error = expires.reset(
        bars,
        tvchart::Timeframe::FiveMinutes,
        {},
        assistedConfig);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QCOMPARE(
        expires.account().proposedAction,
        tvchart::SimulationAction::Enter);
    QVERIFY(!expires
                 .requestManualAction(
                     tvchart::SimulationAction::Enter)
                 .isEmpty());
    error = expires.step();
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QCOMPARE(expires.account().quantity, 0.0);
    QVERIFY(std::ranges::any_of(
        expires.decisions(),
        [](const tvchart::SimulationDecision& decision) {
            return decision.disposition ==
                   tvchart::SimulationDecisionDisposition::Expired;
        }));

    tvchart::TradingSimulationSession approved;
    error = approved.reset(
        bars,
        tvchart::Timeframe::FiveMinutes,
        {},
        assistedConfig);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    error = approved.approveProposal();
    QVERIFY2(error.isEmpty(), qPrintable(error));
    error = approved.step();
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QVERIFY(approved.account().quantity > 0.0);
}

void StrategyEngineTests::
restoresSimulationSnapshotsDeterministically() {
    const auto bars = sampleBars();
    const tvchart::TimeframeSeries additionalSeries{{
        tvchart::Timeframe::OneDay,
        {
            bar(1'699'900'000, 100.0, 101.0),
            bar(1'700'100'000, 101.0, 102.0),
        },
    }};
    const tvchart::SimulationConfig config{
        .strategy = crossingStrategy(),
        .execution = {
            .initialCapital = 25'000.0,
            .allocationPercent = 40.0,
            .commissionPerSide = 2.5,
            .slippageBasisPoints = 15.0,
            .allowFractionalShares = false,
        },
        .mode = tvchart::SimulationMode::Manual,
        .startIndex = 2,
    };
    tvchart::TradingSimulationSession original;
    auto error = original.reset(
        bars,
        tvchart::Timeframe::FiveMinutes,
        additionalSeries,
        config);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    error = original.requestManualAction(
        tvchart::SimulationAction::Enter);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    error = original.step();
    QVERIFY2(error.isEmpty(), qPrintable(error));
    error = original.requestManualAction(
        tvchart::SimulationAction::Exit);
    QVERIFY2(error.isEmpty(), qPrintable(error));

    const auto snapshot =
        original.snapshot(
            QStringLiteral("TEST"),
            QStringLiteral("CSV"));
    const auto encoded =
        tvchart::serializeSimulationSnapshot(snapshot);
    const auto decoded =
        tvchart::deserializeSimulationSnapshot(encoded);
    QVERIFY2(decoded.ok(), qPrintable(decoded.error));

    tvchart::TradingSimulationSession restored;
    error = restored.restore(
        bars,
        tvchart::Timeframe::FiveMinutes,
        additionalSeries,
        decoded.snapshot,
        QStringLiteral("TEST"),
        QStringLiteral("CSV"));
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QCOMPARE(
        restored.account().currentTimestamp,
        original.account().currentTimestamp);
    QCOMPARE(
        restored.account().pendingAction,
        original.account().pendingAction);
    QCOMPARE(
        restored.account().cash,
        original.account().cash);
    QCOMPARE(
        restored.account().quantity,
        original.account().quantity);
    QCOMPARE(
        restored.trades().size(),
        original.trades().size());

    auto changed = bars;
    changed.back().volume += 1.0;
    tvchart::TradingSimulationSession incompatible;
    error = incompatible.restore(
        changed,
        tvchart::Timeframe::FiveMinutes,
        {},
        decoded.snapshot,
        QStringLiteral("TEST"),
        QStringLiteral("CSV"));
    QVERIFY(!error.isEmpty());
    QVERIFY(!incompatible.active());

    auto changedAdditional = additionalSeries;
    changedAdditional.begin()->second.back().volume += 1.0;
    error = incompatible.restore(
        bars,
        tvchart::Timeframe::FiveMinutes,
        changedAdditional,
        decoded.snapshot,
        QStringLiteral("TEST"),
        QStringLiteral("CSV"));
    QVERIFY(!error.isEmpty());
    QVERIFY(!incompatible.active());
}

void StrategyEngineTests::
importsSafePineSubsetAndRejectsUnsupportedSemantics() {
    const auto imported =
        tvchart::importPineStrategy(
            tvchart::pineStrategyExample());
    QVERIFY2(
        imported.ok(),
        qPrintable(
            tvchart::pineNativeStrategyPreview(imported)));
    QCOMPARE(
        imported.strategy.name,
        QStringLiteral("EMA Cross Simulation"));
    QCOMPARE(
        imported.execution.initialCapital,
        100'000.0);
    QCOMPARE(
        imported.execution.allocationPercent,
        25.0);
    QCOMPARE(
        imported.strategy.entry.conditions.size(),
        std::size_t{1});
    QCOMPARE(
        imported.strategy.entry.conditions.front().comparison,
        tvchart::StrategyComparison::CrossesAbove);
    QCOMPARE(
        imported.strategy.entry.conditions.front().left.field,
        tvchart::StrategyField::ExponentialMovingAverage);
    QCOMPARE(
        imported.strategy.entry.conditions.front().left.period,
        std::uint32_t{20});
    QVERIFY(
        imported.strategy.entry.conditions.front().right.has_value());
    QCOMPARE(
        imported.strategy.entry.conditions.front().right->period,
        std::uint32_t{50});

    const auto grouped = tvchart::importPineStrategy(
        QStringLiteral(
            "//@version=6\n"
            "strategy(\"Filtered\")\n"
            "fast = ta.sma(close, 5)\n"
            "slow = ta.sma(close, 20)\n"
            "entryRule = ta.crossover(fast, slow) and close > slow\n"
            "exitRule = ta.crossunder(fast, slow) or close < 5\n"
            "if entryRule\n"
            "    strategy.entry(\"Long\", strategy.long)\n"
            "if exitRule\n"
            "    strategy.close(\"Long\")\n"));
    QVERIFY2(
        grouped.ok(),
        qPrintable(
            tvchart::pineNativeStrategyPreview(grouped)));
    QCOMPARE(
        grouped.strategy.entry.match,
        tvchart::ConditionMatch::All);
    QCOMPARE(
        grouped.strategy.entry.conditions.size(),
        std::size_t{2});
    QCOMPARE(
        grouped.strategy.exit.match,
        tvchart::ConditionMatch::Any);
    QCOMPARE(
        grouped.strategy.exit.conditions.size(),
        std::size_t{2});

    const auto namedDirection =
        tvchart::importPineStrategy(
            QStringLiteral(
                "//@version=5\n"
                "strategy(\"Named direction\")\n"
                "enter = ta.crossover(close, ta.sma(close, 5))\n"
                "leave = ta.crossunder(close, ta.sma(close, 5))\n"
                "strategy.entry(id=\"Long\", direction=strategy.long, when=enter)\n"
                "strategy.close(id=\"Long\", when=leave)\n"));
    QVERIFY2(
        namedDirection.ok(),
        qPrintable(
            tvchart::pineNativeStrategyPreview(namedDirection)));

    const auto ambiguousSizing =
        tvchart::importPineStrategy(
            QStringLiteral(
                "//@version=6\n"
                "strategy(\"Ambiguous sizing\", default_qty_value=10)\n"
                "enter = close > 1\n"
                "leave = close < 1\n"
                "strategy.entry(\"Long\", strategy.long, when=enter)\n"
                "strategy.close(\"Long\", when=leave)\n"));
    QVERIFY(!ambiguousSizing.ok());

    const auto unsupportedLimit =
        tvchart::importPineStrategy(
            QStringLiteral(
                "//@version=6\n"
                "strategy(\"Limit order\")\n"
                "enter = close > 1\n"
                "leave = close < 1\n"
                "strategy.entry(\"Long\", strategy.long, limit=close, when=enter)\n"
                "strategy.close(\"Long\", when=leave)\n"));
    QVERIFY(!unsupportedLimit.ok());

    const auto unsupported = tvchart::importPineStrategy(
        QStringLiteral(
            "//@version=6\n"
            "strategy(\"Unsafe\", calc_on_every_tick=true)\n"
            "prior = close[1]\n"
            "go = close > prior\n"
            "if go\n"
            "    strategy.entry(\"Short\", strategy.short)\n"
            "if go\n"
            "    strategy.exit(\"Exit\", stop=low)\n"));
    QVERIFY(!unsupported.ok());
    QVERIFY(std::ranges::count_if(
                unsupported.diagnostics,
                [](const tvchart::PineDiagnostic& diagnostic) {
                    return diagnostic.severity ==
                           tvchart::PineDiagnosticSeverity::Error;
                }) >= 3);
    QVERIFY(std::ranges::all_of(
        unsupported.diagnostics,
        [](const tvchart::PineDiagnostic& diagnostic) {
            return diagnostic.line >= 1 &&
                   diagnostic.column >= 1 &&
                   !diagnostic.message.isEmpty();
        }));
}

QTEST_APPLESS_MAIN(StrategyEngineTests)

#include "strategy_engine_tests.moc"
