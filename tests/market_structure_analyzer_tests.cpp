#include "analysis/market_structure_analyzer.hpp"

#include <QDateTime>
#include <QTest>
#include <QTimeZone>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <ranges>
#include <vector>

class MarketStructureAnalyzerTests final : public QObject {
    Q_OBJECT

private slots:
    void pivotUsesRightSideConfirmation();
    void equalHighPlateauIsNotAnArbitraryPivot();
    void repeatedSwingsCreateBoundedZones();
    void detectsAndConfirmsDoubleTop();
    void detectsRangeAndWedgeGeometry();
    void detectsHeadAndShoulders();
    void historicalOutcomeWindowsAreDeterministic();
    void higherTimeframeExcludesLatestUnprovenBucket();
    void explicitAnalysisBoundaryRejectsFutureLeakage();
    void resultBoundsHoldForLongNoisyHistory();
    void invalidSettingsAndBarsStayExplicit();
};

namespace {

[[nodiscard]] std::int64_t timestamp(const int day) {
    return QDateTime(
               QDate(2024, 1, 1).addDays(day),
               QTime(0, 0),
               QTimeZone::UTC)
        .toSecsSinceEpoch();
}

[[nodiscard]] tvchart::Bars fromCloses(
    const std::vector<double>& closes) {
    tvchart::Bars result;
    result.reserve(closes.size());
    for (auto index = std::size_t{}; index < closes.size(); ++index) {
        const auto close = closes[index];
        result.push_back({
            .timestamp = timestamp(static_cast<int>(index)),
            .open = close,
            .high = close + 0.2,
            .low = close - 0.2,
            .close = close,
            .volume = 1'000'000.0,
        });
    }
    return result;
}

[[nodiscard]] tvchart::Bars interpolate(
    const std::vector<std::pair<int, double>>& points,
    const int finalIndex) {
    std::vector<double> closes(
        static_cast<std::size_t>(finalIndex + 1));
    for (auto point = std::size_t{}; point + 1 < points.size(); ++point) {
        const auto [startIndex, startValue] = points[point];
        const auto [endIndex, endValue] = points[point + 1];
        for (auto index = startIndex; index <= endIndex; ++index) {
            const auto weight =
                static_cast<double>(index - startIndex) /
                static_cast<double>(endIndex - startIndex);
            closes[static_cast<std::size_t>(index)] =
                startValue + (endValue - startValue) * weight;
        }
    }
    return fromCloses(closes);
}

[[nodiscard]] tvchart::MarketStructureInput inputFor(
    tvchart::Bars bars,
    const int strength = 2) {
    return {
        .symbol = QStringLiteral("TEST"),
        .bars = std::move(bars),
        .timeframe = tvchart::Timeframe::OneDay,
        .settings = {
            .pivotStrength = strength,
            .zoneAtrMultiplier = 0.75,
            .minimumZoneTouches = 2,
            .maximumLookbackBars = 1'500,
            .maximumPivots = 64,
            .maximumZones = 12,
            .maximumPatterns = 24,
        },
        .includeHistoricalValidation = true,
    };
}

} // namespace

void MarketStructureAnalyzerTests::pivotUsesRightSideConfirmation() {
    std::vector<double> closes(35, 10.0);
    for (auto index = 0; index <= 10; ++index) {
        closes[static_cast<std::size_t>(index)] =
            10.0 + static_cast<double>(index);
    }
    for (auto index = 11; index < 35; ++index) {
        closes[static_cast<std::size_t>(index)] =
            20.0 - static_cast<double>(index - 10) * 0.4;
    }
    const auto report =
        tvchart::analyzeMarketStructure(
            inputFor(fromCloses(closes), 3));
    QVERIFY2(report.ok(), qPrintable(report.error));
    const auto pivot = std::ranges::find_if(
        report.pivots,
        [](const tvchart::ConfirmedPivot& value) {
            return value.type == tvchart::PivotType::High &&
                   value.anchorIndex == 10;
        });
    QVERIFY(pivot != report.pivots.end());
    QCOMPARE(pivot->confirmationIndex, std::size_t{13});
    QCOMPARE(
        pivot->confirmationTimestamp,
        timestamp(13));
}

void MarketStructureAnalyzerTests::
equalHighPlateauIsNotAnArbitraryPivot() {
    std::vector<double> closes(35, 10.0);
    closes[9] = 14.0;
    closes[10] = 15.0;
    closes[11] = 15.0;
    closes[12] = 14.0;
    const auto report =
        tvchart::analyzeMarketStructure(
            inputFor(fromCloses(closes), 2));
    QVERIFY(report.ok());
    QVERIFY(!std::ranges::any_of(
        report.pivots,
        [](const tvchart::ConfirmedPivot& value) {
            return value.type == tvchart::PivotType::High &&
                   (value.anchorIndex == 10 ||
                    value.anchorIndex == 11);
        }));
}

void MarketStructureAnalyzerTests::repeatedSwingsCreateBoundedZones() {
    std::vector<double> closes;
    for (auto index = 0; index < 100; ++index) {
        closes.push_back(
            100.0 +
            10.0 *
                std::sin(
                    static_cast<double>(index) *
                    3.14159265358979323846 / 5.0));
    }
    const auto report =
        tvchart::analyzeMarketStructure(
            inputFor(fromCloses(closes), 2));
    QVERIFY(report.ok());
    QVERIFY(report.zones.size() <= std::size_t{12});
    QVERIFY(std::ranges::any_of(
        report.zones,
        [](const tvchart::StructureZone& zone) {
            return zone.type ==
                       tvchart::StructureZoneType::Support &&
                   zone.touches >= 2;
        }));
    QVERIFY(std::ranges::any_of(
        report.zones,
        [](const tvchart::StructureZone& zone) {
            return zone.type ==
                       tvchart::StructureZoneType::Resistance &&
                   zone.touches >= 2;
        }));
}

void MarketStructureAnalyzerTests::detectsAndConfirmsDoubleTop() {
    const auto bars = interpolate(
        {{0, 90.0},
         {15, 110.0},
         {25, 95.0},
         {35, 109.5},
         {50, 88.0},
         {70, 84.0}},
        70);
    const auto report =
        tvchart::analyzeMarketStructure(
            inputFor(bars, 2));
    QVERIFY2(report.ok(), qPrintable(report.error));
    const auto pattern = std::ranges::find_if(
        report.patterns,
        [](const tvchart::ChartPattern& value) {
            return value.kind ==
                tvchart::PatternKind::DoubleTop;
        });
    QVERIFY(pattern != report.patterns.end());
    QCOMPARE(
        pattern->direction,
        tvchart::PatternDirection::Bearish);
    QVERIFY(
        pattern->status ==
            tvchart::PatternStatus::Confirmed ||
        pattern->status ==
            tvchart::PatternStatus::TargetReached);
    QVERIFY(
        pattern->detectionTimestamp >=
        pattern->anchors.back().confirmationTimestamp);
}

void MarketStructureAnalyzerTests::detectsRangeAndWedgeGeometry() {
    const auto triangleBars = interpolate(
        {{0, 100.0},
         {10, 120.0},
         {15, 80.0},
         {25, 115.0},
         {30, 85.0},
         {45, 130.0},
         {60, 135.0}},
        60);
    const auto triangleReport =
        tvchart::analyzeMarketStructure(
            inputFor(triangleBars, 2));
    QVERIFY2(
        triangleReport.ok(),
        qPrintable(triangleReport.error));
    const auto triangle = std::ranges::find_if(
        triangleReport.patterns,
        [](const tvchart::ChartPattern& value) {
            return value.kind ==
                tvchart::PatternKind::Triangle;
        });
    QVERIFY(triangle != triangleReport.patterns.end());
    QCOMPARE(
        triangle->direction,
        tvchart::PatternDirection::Bullish);
    QVERIFY(
        triangle->status ==
            tvchart::PatternStatus::Confirmed ||
        triangle->status ==
            tvchart::PatternStatus::TargetReached);
    QVERIFY(
        triangle->detectionIndex >=
        triangle->formationIndex);
    QCOMPARE(triangle->boundaries.size(), std::size_t{2});

    const auto wedgeBars = interpolate(
        {{0, 80.0},
         {10, 100.0},
         {15, 85.0},
         {25, 110.0},
         {30, 103.0},
         {35, 108.0},
         {45, 90.0},
         {60, 82.0}},
        60);
    const auto wedgeReport =
        tvchart::analyzeMarketStructure(
            inputFor(wedgeBars, 2));
    QVERIFY2(
        wedgeReport.ok(),
        qPrintable(wedgeReport.error));
    QVERIFY(std::ranges::any_of(
        wedgeReport.patterns,
        [](const tvchart::ChartPattern& value) {
            return value.kind ==
                       tvchart::PatternKind::RisingWedge &&
                   value.direction ==
                       tvchart::PatternDirection::Bearish;
        }));
}

void MarketStructureAnalyzerTests::detectsHeadAndShoulders() {
    const auto bars = interpolate(
        {{0, 95.0},
         {12, 110.0},
         {20, 99.0},
         {30, 122.0},
         {40, 100.0},
         {50, 111.0},
         {65, 90.0},
         {80, 86.0}},
        80);
    const auto report =
        tvchart::analyzeMarketStructure(
            inputFor(bars, 2));
    QVERIFY2(report.ok(), qPrintable(report.error));
    QVERIFY(std::ranges::any_of(
        report.patterns,
        [](const tvchart::ChartPattern& value) {
            return value.kind ==
                tvchart::PatternKind::HeadAndShoulders;
        }));
}

void MarketStructureAnalyzerTests::
historicalOutcomeWindowsAreDeterministic() {
    std::vector<double> closes;
    closes.reserve(300);
    for (auto index = 0; index < 300; ++index) {
        closes.push_back(
            100.0 +
            12.0 *
                std::sin(
                    static_cast<double>(index) *
                    3.14159265358979323846 / 10.0));
    }
    const auto bars = fromCloses(closes);
    const auto first =
        tvchart::analyzeMarketStructure(
            inputFor(bars, 2));
    const auto second =
        tvchart::analyzeMarketStructure(
            inputFor(bars, 2));
    QVERIFY(first.ok());
    QVERIFY(!first.historicalOutcomes.empty());
    QCOMPARE(first.pivots, second.pivots);
    QCOMPARE(first.zones, second.zones);
    QCOMPARE(first.lines, second.lines);
    QCOMPARE(first.patterns, second.patterns);
    QCOMPARE(
        first.historicalOutcomes.size(),
        second.historicalOutcomes.size());
    for (auto index = std::size_t{};
         index < first.historicalOutcomes.size();
         ++index) {
        const auto& left =
            first.historicalOutcomes[index];
        const auto& right =
            second.historicalOutcomes[index];
        QCOMPARE(left.kind, right.kind);
        QCOMPARE(left.direction, right.direction);
        QCOMPARE(left.samples, right.samples);
        QCOMPARE(
            left.medianSignedReturn5Percent,
            right.medianSignedReturn5Percent);
        QCOMPARE(
            left.medianSignedReturn20Percent,
            right.medianSignedReturn20Percent);
        QCOMPARE(
            left.positiveSignedReturn20Percent,
            right.positiveSignedReturn20Percent);
        QCOMPARE(
            left.medianMaximumAdverseExcursion20Percent,
            right.medianMaximumAdverseExcursion20Percent);
        QCOMPARE(left.targetHits, right.targetHits);
        QCOMPARE(left.invalidations, right.invalidations);
        QVERIFY(
            left.targetHits + left.invalidations <=
            left.samples);
    }
}

void MarketStructureAnalyzerTests::
higherTimeframeExcludesLatestUnprovenBucket() {
    std::vector<double> closes(15, 100.0);
    for (auto index = std::size_t{}; index < closes.size(); ++index) {
        closes[index] += static_cast<double>(index);
    }
    auto bars = fromCloses(closes);
    // 2024-01-01 is Monday. Fifteen calendar days cover three ISO
    // weeks; only the first two have a later bucket proving completion.
    const auto aggregate =
        tvchart::aggregateCompletedHigherTimeframe(
            bars,
            tvchart::Timeframe::OneDay);
    QCOMPARE(aggregate.size(), std::size_t{2});
    QCOMPARE(aggregate.front().open, bars.front().open);
    QCOMPARE(aggregate.front().close, bars[6].close);
    QCOMPARE(aggregate.back().close, bars[13].close);
}

void MarketStructureAnalyzerTests::
explicitAnalysisBoundaryRejectsFutureLeakage() {
    auto prefix = interpolate(
        {{0, 90.0},
         {15, 110.0},
         {25, 95.0},
         {35, 109.5},
         {50, 88.0},
         {70, 84.0}},
        70);
    auto withFuture = prefix;
    auto future = interpolate(
        {{0, 84.0}, {10, 140.0}, {20, 60.0}},
        20);
    const auto offset =
        prefix.back().timestamp -
        future.front().timestamp +
        24 * 60 * 60;
    for (auto& bar : future) {
        bar.timestamp += offset;
        withFuture.push_back(bar);
    }
    auto bounded = inputFor(withFuture, 2);
    bounded.analysisThroughTimestamp =
        prefix.back().timestamp;
    const auto expected =
        tvchart::analyzeMarketStructure(
            inputFor(prefix, 2));
    const auto actual =
        tvchart::analyzeMarketStructure(bounded);

    QVERIFY(expected.ok());
    QVERIFY(actual.ok());
    QCOMPARE(actual.asOfTimestamp, expected.asOfTimestamp);
    QCOMPARE(actual.pivots, expected.pivots);
    QCOMPARE(actual.zones, expected.zones);
    QCOMPARE(actual.lines, expected.lines);
    QCOMPARE(actual.patterns, expected.patterns);
}

void MarketStructureAnalyzerTests::
resultBoundsHoldForLongNoisyHistory() {
    std::vector<double> closes;
    closes.reserve(2'000);
    for (auto index = 0; index < 2'000; ++index) {
        closes.push_back(
            100.0 +
            std::sin(static_cast<double>(index) * 0.31) * 8.0 +
            std::sin(static_cast<double>(index) * 0.071) * 4.0);
    }
    const auto report =
        tvchart::analyzeMarketStructure(
            inputFor(fromCloses(closes), 2));
    QVERIFY(report.ok());
    QCOMPARE(report.barsAnalyzed, std::size_t{1'500});
    QVERIFY(report.pivots.size() <= std::size_t{64});
    QVERIFY(report.zones.size() <= std::size_t{12});
    QVERIFY(report.patterns.size() <= std::size_t{24});
}

void MarketStructureAnalyzerTests::
invalidSettingsAndBarsStayExplicit() {
    auto invalidSettings =
        inputFor(fromCloses(std::vector<double>(35, 100.0)));
    invalidSettings.settings.pivotStrength = 1;
    const auto settingsReport =
        tvchart::analyzeMarketStructure(invalidSettings);
    QVERIFY(!settingsReport.ok());
    QVERIFY(settingsReport.error.contains(
        QStringLiteral("Pivot strength")));

    auto invalidBars =
        inputFor(fromCloses(std::vector<double>(35, 100.0)));
    invalidBars.bars[10].low =
        invalidBars.bars[10].high + 1.0;
    const auto barsReport =
        tvchart::analyzeMarketStructure(invalidBars);
    QVERIFY(!barsReport.ok());
    QVERIFY(barsReport.error.contains(
        QStringLiteral("invalid")));
}

QTEST_APPLESS_MAIN(MarketStructureAnalyzerTests)

#include "market_structure_analyzer_tests.moc"
