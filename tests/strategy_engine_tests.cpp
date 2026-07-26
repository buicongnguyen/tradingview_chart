#include "strategy/replay_session.hpp"
#include "strategy/strategy_engine.hpp"
#include "strategy/strategy_models.hpp"

#include <QJsonDocument>
#include <QJsonObject>
#include <QTest>

#include <cmath>

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

QTEST_APPLESS_MAIN(StrategyEngineTests)

#include "strategy_engine_tests.moc"
