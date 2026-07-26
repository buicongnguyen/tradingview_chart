#include "analysis/comparison_analysis.hpp"

#include <QTest>

#include <cmath>

class ComparisonAnalysisTests final : public QObject {
    Q_OBJECT

private slots:
    void alignsNormalizesAndCorrelatesExactTimestamps();
    void rejectsInsufficientOverlap();
    void leavesCorrelationUnavailableForConstantReturns();
};

namespace {

[[nodiscard]] tvchart::Bar bar(const std::int64_t timestamp, const double close) {
    return {
        .timestamp = timestamp,
        .open = close,
        .high = close,
        .low = close,
        .close = close,
        .volume = 100.0,
    };
}

} // namespace

void ComparisonAnalysisTests::alignsNormalizesAndCorrelatesExactTimestamps() {
    const tvchart::Bars primary{
        bar(100, 10.0),
        bar(200, 11.0),
        bar(300, 12.65),
        bar(400, 13.2825),
    };
    const tvchart::Bars comparison{
        bar(50, 5.0),
        bar(100, 20.0),
        bar(200, 22.0),
        bar(300, 25.3),
        bar(400, 26.565),
        bar(450, 30.0),
    };

    const auto result = tvchart::compareSeries(primary, comparison);
    QVERIFY2(result.ok(), qPrintable(result.error));
    QCOMPARE(result.points.size(), std::size_t{4});
    QCOMPARE(result.points.front().timestamp, std::int64_t{100});
    QVERIFY(std::abs(result.primaryReturnPercent - 32.825) < 1e-10);
    QVERIFY(std::abs(result.comparisonReturnPercent - 32.825) < 1e-10);
    QVERIFY(std::abs(result.relativeReturnPercent) < 1e-10);
    QVERIFY(result.returnCorrelation);
    QVERIFY(std::abs(*result.returnCorrelation - 1.0) < 1e-10);
}

void ComparisonAnalysisTests::rejectsInsufficientOverlap() {
    const auto result = tvchart::compareSeries(
        tvchart::Bars{bar(100, 10.0), bar(200, 11.0)},
        tvchart::Bars{bar(200, 20.0), bar(300, 21.0)});
    QVERIFY(!result.ok());
    QVERIFY(result.points.empty());
    QVERIFY(result.error.contains(QStringLiteral("two exact common")));
}

void ComparisonAnalysisTests::leavesCorrelationUnavailableForConstantReturns() {
    const auto result = tvchart::compareSeries(
        tvchart::Bars{bar(100, 10.0), bar(200, 10.0), bar(300, 10.0)},
        tvchart::Bars{bar(100, 20.0), bar(200, 21.0), bar(300, 22.0)});
    QVERIFY2(result.ok(), qPrintable(result.error));
    QVERIFY(!result.returnCorrelation);
}

QTEST_APPLESS_MAIN(ComparisonAnalysisTests)

#include "comparison_analysis_tests.moc"
