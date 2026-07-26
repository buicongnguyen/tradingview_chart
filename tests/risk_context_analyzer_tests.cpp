#include "analysis/risk_context_analyzer.hpp"

#include <QDateTime>
#include <QTest>
#include <QTimeZone>

#include <algorithm>
#include <cmath>
#include <cstdint>

class RiskContextAnalyzerTests final : public QObject {
    Q_OBJECT

private slots:
    void reportsAdverseEvidenceAndReconcilesScore();
    void constructiveTrendEvidenceNeverSubtractsRiskPoints();
    void missingBenchmarkReducesCoverageInsteadOfImplyingSafety();
    void futureKnownEventDoesNotLeakIntoCurrentAssessment();
    void knownNearEarningsAddsDatedEvidence();
    void oldEventDoesNotClaimCurrentCalendarCoverage();
    void smallHistoricalSampleIsExplicit();
    void historicalOutcomesNeverUseFutureBarsAsEvidence();
};

namespace {

[[nodiscard]] std::int64_t day(const int offset) {
    return QDateTime(
               QDate(2024, 1, 2).addDays(offset),
               QTime(0, 0),
               QTimeZone::UTC)
        .toSecsSinceEpoch();
}

[[nodiscard]] tvchart::Bars series(
    const std::size_t count,
    const double start,
    const double dailyChange,
    const double volume = 1'000'000.0) {
    tvchart::Bars result;
    result.reserve(count);
    auto close = start;
    for (auto index = std::size_t{}; index < count; ++index) {
        const auto open = close;
        close = std::max(1.0, close + dailyChange);
        result.push_back({
            .timestamp = day(static_cast<int>(index)),
            .open = open,
            .high = std::max(open, close) * 1.005,
            .low = std::min(open, close) * 0.995,
            .close = close,
            .volume = volume,
        });
    }
    return result;
}

[[nodiscard]] tvchart::RiskContextInput baseInput() {
    return {
        .symbol = QStringLiteral("TEST"),
        .benchmarkSymbol = QStringLiteral("SPY"),
        .securityBars = series(320, 200.0, -0.35),
        .benchmarkBars = series(320, 500.0, -0.45, 10'000'000.0),
        .eventCalendarCoverageKnown = false,
        .observationDate = QDate(2024, 11, 30),
        .includeHistoricalValidation = false,
    };
}

} // namespace

void RiskContextAnalyzerTests::reportsAdverseEvidenceAndReconcilesScore() {
    auto input = baseInput();
    auto& latest = input.securityBars.back();
    latest.open = latest.close + 4.0;
    latest.high = latest.open + 8.0;
    latest.low = latest.close - 1.0;

    const auto report = tvchart::analyzeRiskContext(input);
    QVERIFY2(report.ok(), qPrintable(report.error));
    QVERIFY(report.score >= 25);
    QVERIFY(report.availableWeight > 0);
    const auto evidencePoints = std::accumulate(
        report.evidence.begin(),
        report.evidence.end(),
        0,
        [](const int total, const tvchart::RiskEvidence& evidence) {
            return total + evidence.points;
        });
    QCOMPARE(evidencePoints, report.adversePoints);
    QCOMPARE(
        report.score,
        std::clamp(
            static_cast<int>(std::lround(
                static_cast<double>(report.adversePoints) * 100.0 /
                static_cast<double>(report.availableWeight))),
            0,
            100));
    QVERIFY(std::ranges::any_of(
        report.evidence,
        [](const tvchart::RiskEvidence& evidence) {
            return evidence.category ==
                       tvchart::RiskCategory::MarketRegime &&
                   evidence.kind == tvchart::RiskEvidenceKind::Adverse;
        }));
    QVERIFY(std::ranges::any_of(
        report.evidence,
        [](const tvchart::RiskEvidence& evidence) {
            return evidence.category ==
                       tvchart::RiskCategory::PriceAction &&
                   evidence.kind == tvchart::RiskEvidenceKind::Adverse;
        }));
}

void RiskContextAnalyzerTests::
constructiveTrendEvidenceNeverSubtractsRiskPoints() {
    auto input = baseInput();
    input.securityBars = series(320, 100.0, 0.35);
    input.benchmarkBars =
        series(320, 200.0, 0.25, 10'000'000.0);
    const auto first = tvchart::analyzeRiskContext(input);
    const auto second = tvchart::analyzeRiskContext(input);

    QVERIFY(first.ok());
    QCOMPARE(first.score, second.score);
    QCOMPARE(first.adversePoints, second.adversePoints);
    QVERIFY(first.adversePoints >= 0);
    QVERIFY(std::ranges::any_of(
        first.evidence,
        [](const tvchart::RiskEvidence& evidence) {
            return evidence.category ==
                       tvchart::RiskCategory::TrendMomentum &&
                   evidence.kind ==
                       tvchart::RiskEvidenceKind::Constructive &&
                   evidence.points == 0;
        }));
}

void RiskContextAnalyzerTests::
missingBenchmarkReducesCoverageInsteadOfImplyingSafety() {
    auto complete = baseInput();
    const auto withBenchmark = tvchart::analyzeRiskContext(complete);
    complete.benchmarkBars.clear();
    const auto withoutBenchmark = tvchart::analyzeRiskContext(complete);

    QVERIFY(withoutBenchmark.ok());
    QVERIFY(withoutBenchmark.coveragePercent <
            withBenchmark.coveragePercent);
    QVERIFY(std::ranges::any_of(
        withoutBenchmark.missingInputs,
        [](const QString& value) {
            return value.contains(QStringLiteral("Benchmark regime"));
        }));
}

void RiskContextAnalyzerTests::
futureKnownEventDoesNotLeakIntoCurrentAssessment() {
    auto input = baseInput();
    const auto asOfDate =
        QDateTime::fromSecsSinceEpoch(
            input.securityBars.back().timestamp,
            QTimeZone::UTC)
            .date();
    input.events.push_back({
        .id = QStringLiteral("future-knowledge"),
        .symbol = QStringLiteral("TEST"),
        .type = tvchart::ResearchEventType::Earnings,
        .scheduledDate = asOfDate.addDays(2),
        .title = QStringLiteral("Earnings"),
        .source = QStringLiteral("test"),
        .asOfUtc = QDateTime(
                       asOfDate.addDays(1),
                       QTime(12, 0),
                       QTimeZone::UTC)
                       .toSecsSinceEpoch(),
        .confidence = tvchart::ResearchConfidence::Confirmed,
    });

    const auto report = tvchart::analyzeRiskContext(input);
    QVERIFY(report.ok());
    QVERIFY(!std::ranges::any_of(
        report.evidence,
        [](const tvchart::RiskEvidence& evidence) {
            return evidence.category ==
                       tvchart::RiskCategory::EventRisk &&
                   evidence.kind == tvchart::RiskEvidenceKind::Adverse;
        }));
    QVERIFY(std::ranges::any_of(
        report.missingInputs,
        [](const QString& value) {
            return value.contains(QStringLiteral("Event-calendar"));
        }));
}

void RiskContextAnalyzerTests::knownNearEarningsAddsDatedEvidence() {
    auto input = baseInput();
    const auto asOfDate =
        QDateTime::fromSecsSinceEpoch(
            input.securityBars.back().timestamp,
            QTimeZone::UTC)
            .date();
    input.events.push_back({
        .id = QStringLiteral("known-earnings"),
        .symbol = QStringLiteral("TEST"),
        .type = tvchart::ResearchEventType::Earnings,
        .scheduledDate = asOfDate.addDays(3),
        .title = QStringLiteral("Earnings"),
        .source = QStringLiteral("test calendar"),
        .asOfUtc = QDateTime(
                       asOfDate.addDays(-2),
                       QTime(12, 0),
                       QTimeZone::UTC)
                       .toSecsSinceEpoch(),
        .confidence = tvchart::ResearchConfidence::Confirmed,
    });

    const auto report = tvchart::analyzeRiskContext(input);
    QVERIFY(report.ok());
    const auto earnings = std::ranges::find_if(
        report.evidence,
        [](const tvchart::RiskEvidence& evidence) {
            return evidence.category ==
                       tvchart::RiskCategory::EventRisk &&
                   evidence.kind == tvchart::RiskEvidenceKind::Adverse;
        });
    QVERIFY(earnings != report.evidence.end());
    QCOMPARE(earnings->points, 10);
    QCOMPARE(earnings->asOfDate, asOfDate);
}

void RiskContextAnalyzerTests::
oldEventDoesNotClaimCurrentCalendarCoverage() {
    auto input = baseInput();
    const auto asOfDate =
        QDateTime::fromSecsSinceEpoch(
            input.securityBars.back().timestamp,
            QTimeZone::UTC)
            .date();
    input.events.push_back({
        .id = QStringLiteral("stale-calendar-record"),
        .symbol = QStringLiteral("TEST"),
        .type = tvchart::ResearchEventType::Earnings,
        .scheduledDate = asOfDate.addDays(-120),
        .title = QStringLiteral("Old earnings"),
        .source = QStringLiteral("test"),
        .asOfUtc = QDateTime(
                       asOfDate.addDays(-130),
                       QTime(12, 0),
                       QTimeZone::UTC)
                       .toSecsSinceEpoch(),
        .confidence = tvchart::ResearchConfidence::Confirmed,
    });

    const auto report = tvchart::analyzeRiskContext(input);
    QVERIFY(report.ok());
    QVERIFY(std::ranges::any_of(
        report.missingInputs,
        [](const QString& value) {
            return value.contains(QStringLiteral("Event-calendar"));
        }));
}

void RiskContextAnalyzerTests::smallHistoricalSampleIsExplicit() {
    auto input = baseInput();
    input.securityBars = series(240, 100.0, 0.1);
    input.benchmarkBars =
        series(240, 200.0, 0.1, 10'000'000.0);
    input.includeHistoricalValidation = true;

    const auto report = tvchart::analyzeRiskContext(input);
    QVERIFY(report.ok());
    QVERIFY(!report.historical.sampleAdequate);
    QVERIFY(report.historical.comparableSetups < 30);
    QVERIFY(report.historical.note.contains(
        QStringLiteral("not a probability")));
}

void RiskContextAnalyzerTests::
historicalOutcomesNeverUseFutureBarsAsEvidence() {
    auto input = baseInput();
    input.securityBars = series(420, 100.0, 0.08);
    input.benchmarkBars = series(420, 200.0, 0.05, 10'000'000.0);
    input.includeHistoricalValidation = true;
    input.analysisThroughDate =
        QDateTime::fromSecsSinceEpoch(
            input.securityBars.back().timestamp,
            QTimeZone::UTC)
            .date();
    const auto original = tvchart::analyzeRiskContext(input);
    QVERIFY(original.ok());

    // Bars after the explicit analysis date must not alter either the score or
    // the historical validation window.
    auto changed = input;
    auto extraSecurity = series(30, 150.0, -2.0);
    auto extraBenchmark = series(30, 250.0, -1.0, 10'000'000.0);
    const auto timestampOffset =
        changed.securityBars.back().timestamp - extraSecurity.front().timestamp +
        24 * 60 * 60;
    for (auto& bar : extraSecurity) {
        bar.timestamp += timestampOffset;
        changed.securityBars.push_back(bar);
    }
    for (auto& bar : extraBenchmark) {
        bar.timestamp += timestampOffset;
        changed.benchmarkBars.push_back(bar);
    }
    const auto changedReport = tvchart::analyzeRiskContext(changed);
    QVERIFY(changedReport.ok());
    QCOMPARE(changedReport.securityAsOfDate, original.securityAsOfDate);
    QCOMPARE(changedReport.score, original.score);
    QCOMPARE(
        changedReport.historical.comparableSetups,
        original.historical.comparableSetups);
    QCOMPARE(
        changedReport.historical.medianForwardReturn20Percent,
        original.historical.medianForwardReturn20Percent);
}

QTEST_APPLESS_MAIN(RiskContextAnalyzerTests)

#include "risk_context_analyzer_tests.moc"
