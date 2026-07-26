#include "strategy/replay_session.hpp"
#include "strategy/strategy_engine.hpp"
#include "strategy/strategy_models.hpp"

#include <QTest>

#include <cmath>

class StrategyEngineTests final : public QObject {
    Q_OBJECT

private slots:
    void roundTripsReusableStrategy();
    void evaluatesWarmupAndCrossingWithoutLookAhead();
    void reportsIndicatorOverflowWithoutThrowing();
    void executesSignalsAtNextOpenAndAppliesCosts();
    void rejectsImpossibleExitCosts();
    void forceClosesAndReconcilesFinalEquity();
    void scansUnavailableSeriesAndDeduplicatesAlerts();
    void replaysWithinDeterministicBoundaries();
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

QTEST_APPLESS_MAIN(StrategyEngineTests)

#include "strategy_engine_tests.moc"
