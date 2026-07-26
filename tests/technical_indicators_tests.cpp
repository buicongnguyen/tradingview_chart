#include "analysis/technical_indicators.hpp"

#include <QTest>

#include <cmath>
#include <stdexcept>

class TechnicalIndicatorsTests final : public QObject {
    Q_OBJECT

private slots:
    void calculatesMovingAverages();
    void calculatesSessionVwap();
    void calculatesRsiAndMacd();
    void calculatesRollingAndVolumeInformation();
    void acceptsConfigurableMultipleIndicators();
    void calculatesMarketStatistics();
    void keepsWarmupExplicit();
    void rejectsInvalidBars();
};

namespace {

[[nodiscard]] tvchart::Bars linearBars(
    const std::size_t count,
    const std::int64_t firstTimestamp = 1'700'000'000) {
    tvchart::Bars bars;
    bars.reserve(count);
    for (std::size_t index = 0; index < count; ++index) {
        const auto close = static_cast<double>(index + 1);
        bars.push_back({
            .timestamp =
                firstTimestamp + static_cast<std::int64_t>(index) * 60,
            .open = close,
            .high = close,
            .low = close,
            .close = close,
            .volume = static_cast<double>(index + 1),
        });
    }
    return bars;
}

} // namespace

void TechnicalIndicatorsTests::calculatesMovingAverages() {
    const auto bars = linearBars(40);
    const auto sma = tvchart::calculateIndicator(
        bars,
        tvchart::IndicatorKind::SimpleMovingAverage);
    QCOMPARE(sma.primary.size(), std::size_t{21});
    QCOMPARE(sma.primary.front().timestamp, bars[19].timestamp);
    QVERIFY(std::abs(sma.primary.front().value - 10.5) < 1e-12);
    QVERIFY(std::abs(sma.primary.back().value - 30.5) < 1e-12);

    const auto ema = tvchart::calculateIndicator(
        bars,
        tvchart::IndicatorKind::ExponentialMovingAverage);
    QCOMPARE(ema.primary.size(), std::size_t{21});
    QVERIFY(std::abs(ema.primary.front().value - 10.5) < 1e-12);
    QVERIFY(std::abs(ema.primary.back().value - 30.5) < 1e-12);
}

void TechnicalIndicatorsTests::calculatesSessionVwap() {
    constexpr auto day = std::int64_t{24 * 60 * 60};
    const tvchart::Bars bars{
        {
            .timestamp = day + 60,
            .open = 10.0,
            .high = 10.0,
            .low = 10.0,
            .close = 10.0,
            .volume = 1.0,
        },
        {
            .timestamp = day + 120,
            .open = 20.0,
            .high = 20.0,
            .low = 20.0,
            .close = 20.0,
            .volume = 2.0,
        },
        {
            .timestamp = (2 * day) + 60,
            .open = 30.0,
            .high = 30.0,
            .low = 30.0,
            .close = 30.0,
            .volume = 3.0,
        },
    };

    const auto vwap = tvchart::calculateIndicator(
        bars,
        tvchart::IndicatorKind::VolumeWeightedAveragePrice);
    QCOMPARE(vwap.primary.size(), std::size_t{3});
    QVERIFY(std::abs(vwap.primary[1].value - (50.0 / 3.0)) < 1e-12);
    QCOMPARE(vwap.primary[2].value, 30.0);
}

void TechnicalIndicatorsTests::calculatesRsiAndMacd() {
    const auto bars = linearBars(60);
    const auto rsi = tvchart::calculateIndicator(
        bars,
        tvchart::IndicatorKind::RelativeStrengthIndex);
    QCOMPARE(rsi.primary.front().timestamp, bars[14].timestamp);
    QCOMPARE(rsi.primary.back().value, 100.0);

    const auto macd = tvchart::calculateIndicator(
        bars,
        tvchart::IndicatorKind::MovingAverageConvergenceDivergence);
    QVERIFY(!macd.primary.empty());
    QVERIFY(!macd.secondary.empty());
    QCOMPARE(macd.secondary.size(), macd.histogram.size());
    QCOMPARE(macd.secondary.back().timestamp, bars.back().timestamp);
    QVERIFY(macd.primary.back().value > 0.0);
    QVERIFY(
        std::abs(
            macd.primary.back().value -
            macd.secondary.back().value -
            macd.histogram.back().value) <
        1e-12);
}

void TechnicalIndicatorsTests::calculatesRollingAndVolumeInformation() {
    const auto bars = linearBars(25);
    const tvchart::IndicatorSpec rollingHigh{
        .kind = tvchart::IndicatorKind::RollingHigh,
        .period = 5,
    };
    const tvchart::IndicatorSpec rollingLow{
        .kind = tvchart::IndicatorKind::RollingLow,
        .period = 5,
    };
    const tvchart::IndicatorSpec volumeAverage{
        .kind = tvchart::IndicatorKind::VolumeSimpleMovingAverage,
        .period = 5,
    };

    const auto highs = tvchart::calculateIndicator(bars, rollingHigh);
    const auto lows = tvchart::calculateIndicator(bars, rollingLow);
    const auto volumes = tvchart::calculateIndicator(bars, volumeAverage);
    QCOMPARE(highs.primary.size(), std::size_t{21});
    QCOMPARE(highs.primary.front().value, 5.0);
    QCOMPARE(highs.primary.back().value, 25.0);
    QCOMPARE(lows.primary.front().value, 1.0);
    QCOMPARE(lows.primary.back().value, 21.0);
    QCOMPARE(volumes.primary.front().value, 3.0);
    QCOMPARE(volumes.primary.back().value, 23.0);
}

void TechnicalIndicatorsTests::acceptsConfigurableMultipleIndicators() {
    const auto bars = linearBars(12);
    const std::vector<tvchart::IndicatorSpec> specs{
        {
            .kind = tvchart::IndicatorKind::SimpleMovingAverage,
            .period = 3,
        },
        {
            .kind = tvchart::IndicatorKind::ExponentialMovingAverage,
            .period = 5,
        },
    };
    const auto calculations = tvchart::calculateIndicators(bars, specs);
    QCOMPARE(calculations.size(), std::size_t{2});
    QCOMPARE(calculations[0].primary.size(), std::size_t{10});
    QCOMPARE(calculations[0].primary.front().value, 2.0);
    QCOMPARE(calculations[0].label, std::string("SMA (3)"));
    QCOMPARE(calculations[1].primary.size(), std::size_t{8});
    QCOMPARE(calculations[1].label, std::string("EMA (5)"));

    const tvchart::IndicatorSpec invalidMacd{
        .kind = tvchart::IndicatorKind::MovingAverageConvergenceDivergence,
        .fastPeriod = 26,
        .slowPeriod = 12,
        .signalPeriod = 9,
    };
    QVERIFY(!tvchart::validIndicatorSpec(invalidMacd));
    QVERIFY_EXCEPTION_THROWN(
        static_cast<void>(
            tvchart::calculateIndicator(bars, invalidMacd)),
        std::invalid_argument);
}

void TechnicalIndicatorsTests::calculatesMarketStatistics() {
    const tvchart::Bars bars{
        {
            .timestamp = 1'700'000'000,
            .open = 10.0,
            .high = 12.0,
            .low = 9.0,
            .close = 11.0,
            .volume = 100.0,
        },
        {
            .timestamp = 1'700'000'060,
            .open = 11.0,
            .high = 15.0,
            .low = 10.0,
            .close = 14.0,
            .volume = 200.0,
        },
    };

    const auto statistics = tvchart::calculateMarketStatistics(bars);
    QCOMPARE(statistics.latestClose, 14.0);
    QCOMPARE(statistics.barChange, 3.0);
    QVERIFY(std::abs(statistics.barChangePercent - (300.0 / 11.0)) < 1e-12);
    QCOMPARE(statistics.loadedLow, 9.0);
    QCOMPARE(statistics.loadedHigh, 15.0);
    QVERIFY(
        std::abs(statistics.loadedRangePositionPercent - (500.0 / 6.0)) <
        1e-12);
    QCOMPARE(statistics.averageVolume20, 150.0);
}

void TechnicalIndicatorsTests::keepsWarmupExplicit() {
    const auto bars = linearBars(10);
    const auto sma = tvchart::calculateIndicator(
        bars,
        tvchart::IndicatorKind::SimpleMovingAverage);
    const auto rsi = tvchart::calculateIndicator(
        bars,
        tvchart::IndicatorKind::RelativeStrengthIndex);
    const auto macd = tvchart::calculateIndicator(
        bars,
        tvchart::IndicatorKind::MovingAverageConvergenceDivergence);

    QVERIFY(sma.primary.empty());
    QVERIFY(rsi.primary.empty());
    QVERIFY(macd.primary.empty());
}

void TechnicalIndicatorsTests::rejectsInvalidBars() {
    auto bars = linearBars(3);
    bars[1].timestamp = bars[0].timestamp;

    QVERIFY_EXCEPTION_THROWN(
        static_cast<void>(
            tvchart::calculateIndicator(
                bars,
                tvchart::IndicatorKind::SimpleMovingAverage)),
        std::invalid_argument);
    QVERIFY_EXCEPTION_THROWN(
        static_cast<void>(tvchart::calculateMarketStatistics(bars)),
        std::invalid_argument);
}

QTEST_APPLESS_MAIN(TechnicalIndicatorsTests)

#include "technical_indicators_tests.moc"
