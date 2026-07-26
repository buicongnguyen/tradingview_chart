#include "data/historical_data_store.hpp"

#include <QTemporaryDir>
#include <QTest>

class HistoricalDataStoreTests final : public QObject {
    Q_OBJECT

private slots:
    void roundTripsProvenanceAndGapSafeUpserts();
    void rejectsSyntheticAndInvalidSeries();
    void selectsLatestProviderSeries();
    void prefersTheProviderWithTheNewestBar();
};

namespace {

[[nodiscard]] tvchart::Bar bar(
    const std::int64_t timestamp,
    const double close) {
    return {
        .timestamp = timestamp,
        .open = close - 0.5,
        .high = close + 1.0,
        .low = close - 1.0,
        .close = close,
        .volume = 1'000.0 + close,
    };
}

[[nodiscard]] tvchart::MarketDataMetadata metadata(
    const std::int64_t retrievedAt,
    const tvchart::DataDeliveryMode mode =
        tvchart::DataDeliveryMode::Polled) {
    return {
        .deliveryMode = mode,
        .exchange = QStringLiteral("Nasdaq"),
        .currency = QStringLiteral("USD"),
        .timezone = QStringLiteral("America/New_York"),
        .instrumentType = QStringLiteral("Equity"),
        .interval = QStringLiteral("5m"),
        .exchangeDelayMinutes = 15,
        .retrievedAtUtc = retrievedAt,
    };
}

} // namespace

void HistoricalDataStoreTests::roundTripsProvenanceAndGapSafeUpserts() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    tvchart::HistoricalDataStore store(
        directory.filePath(QStringLiteral("history.sqlite")));
    QVERIFY2(store.open(), qPrintable(store.lastError()));

    const tvchart::Bars first{
        bar(1'700'000'000, 100.0),
        bar(1'700'000'300, 101.0),
        bar(1'700'000'600, 102.0),
    };
    QVERIFY(store
                .upsertSeries(
                    QStringLiteral("Yahoo Finance"),
                    QStringLiteral("aapl"),
                    tvchart::Timeframe::FiveMinutes,
                    first,
                    metadata(1'700'001'000))
                .isEmpty());

    const tvchart::Bars overlap{
        bar(1'700'000'600, 112.0),
        bar(1'700'000'900, 113.0),
    };
    const auto newerMetadata = metadata(1'700'002'000);
    QVERIFY(store
                .upsertSeries(
                    QStringLiteral("Yahoo Finance"),
                    QStringLiteral("AAPL"),
                    tvchart::Timeframe::FiveMinutes,
                    overlap,
                    newerMetadata)
                .isEmpty());

    const auto loaded = store.loadSeries(
        QStringLiteral("Yahoo Finance"),
        QStringLiteral("AAPL"),
        tvchart::Timeframe::FiveMinutes);
    QVERIFY2(loaded.ok(), qPrintable(loaded.error));
    QCOMPARE(loaded.bars.size(), std::size_t{4});
    QCOMPARE(loaded.bars.at(2).close, 112.0);
    QCOMPARE(loaded.bars.front(), first.front());
    QCOMPARE(loaded.metadata.exchange, newerMetadata.exchange);
    QCOMPARE(loaded.metadata.currency, newerMetadata.currency);
    QCOMPARE(loaded.metadata.exchangeDelayMinutes, std::optional<int>{15});
    QCOMPARE(loaded.metadata.retrievedAtUtc, std::int64_t{1'700'002'000});
    QVERIFY(loaded.cachedAtUtcMilliseconds > 0);

    const auto range = store.loadSeries(
        QStringLiteral("Yahoo Finance"),
        QStringLiteral("AAPL"),
        tvchart::Timeframe::FiveMinutes,
        1'700'000'300,
        1'700'000'600);
    QVERIFY2(range.ok(), qPrintable(range.error));
    QCOMPARE(range.bars.size(), std::size_t{2});

    const auto available = store.availableSeries();
    QCOMPARE(available.size(), std::size_t{1});
    QCOMPARE(available.front().barCount, std::size_t{4});
    QCOMPARE(available.front().firstTimestamp, std::int64_t{1'700'000'000});
    QCOMPARE(available.front().lastTimestamp, std::int64_t{1'700'000'900});
}

void HistoricalDataStoreTests::rejectsSyntheticAndInvalidSeries() {
    tvchart::HistoricalDataStore store(QStringLiteral(":memory:"));
    QVERIFY2(store.open(), qPrintable(store.lastError()));
    const tvchart::Bars bars{bar(1'700'000'000, 100.0)};

    QVERIFY(store
                .upsertSeries(
                    QStringLiteral("Offline demo"),
                    QStringLiteral("AAPL"),
                    tvchart::Timeframe::FiveMinutes,
                    bars,
                    metadata(
                        1'700'001'000,
                        tvchart::DataDeliveryMode::Synthetic))
                .contains(QStringLiteral("Synthetic")));

    auto invalid = bars;
    invalid.front().high = 1.0;
    QVERIFY(!store
                 .upsertSeries(
                     QStringLiteral("Yahoo Finance"),
                     QStringLiteral("AAPL"),
                     tvchart::Timeframe::FiveMinutes,
                     invalid,
                     metadata(1'700'001'000))
                 .isEmpty());
    QVERIFY(!store
                 .upsertSeries(
                     QString(121, u'P'),
                     QStringLiteral("AAPL"),
                     tvchart::Timeframe::FiveMinutes,
                     bars,
                     metadata(1'700'001'000))
                 .isEmpty());
    auto oversizedMetadata = metadata(1'700'001'000);
    oversizedMetadata.exchange = QString(161, u'X');
    QVERIFY(!store
                 .upsertSeries(
                     QStringLiteral("Yahoo Finance"),
                     QStringLiteral("AAPL"),
                     tvchart::Timeframe::FiveMinutes,
                     bars,
                     oversizedMetadata)
                 .isEmpty());
}

void HistoricalDataStoreTests::selectsLatestProviderSeries() {
    tvchart::HistoricalDataStore store(QStringLiteral(":memory:"));
    QVERIFY2(store.open(), qPrintable(store.lastError()));
    const tvchart::Bars yahoo{bar(1'700'000'000, 100.0)};
    const tvchart::Bars twelve{bar(1'700'000'000, 200.0)};
    QVERIFY(store
                .upsertSeries(
                    QStringLiteral("Yahoo Finance"),
                    QStringLiteral("MSFT"),
                    tvchart::Timeframe::OneDay,
                    yahoo,
                    metadata(1'700'001'000))
                .isEmpty());
    QVERIFY(store
                .upsertSeries(
                    QStringLiteral("Twelve Data"),
                    QStringLiteral("MSFT"),
                    tvchart::Timeframe::OneDay,
                    twelve,
                    metadata(1'700'002'000))
                .isEmpty());

    const auto latest =
        store.loadLatestSeries(QStringLiteral("MSFT"), tvchart::Timeframe::OneDay);
    QVERIFY2(latest.ok(), qPrintable(latest.error));
    QCOMPARE(latest.key.provider, QStringLiteral("Twelve Data"));
    QCOMPARE(latest.bars.front().close, 200.0);
}

void HistoricalDataStoreTests::prefersTheProviderWithTheNewestBar() {
    tvchart::HistoricalDataStore store(QStringLiteral(":memory:"));
    QVERIFY2(store.open(), qPrintable(store.lastError()));
    QVERIFY(store
                .upsertSeries(
                    QStringLiteral("Fresh Provider"),
                    QStringLiteral("NVDA"),
                    tvchart::Timeframe::FiveMinutes,
                    {bar(1'700'000'600, 300.0)},
                    metadata(1'700'001'000))
                .isEmpty());
    QVERIFY(store
                .upsertSeries(
                    QStringLiteral("Recently Written But Stale"),
                    QStringLiteral("NVDA"),
                    tvchart::Timeframe::FiveMinutes,
                    {bar(1'700'000'000, 200.0)},
                    metadata(1'700'002'000))
                .isEmpty());

    const auto latest = store.loadLatestSeries(
        QStringLiteral("NVDA"),
        tvchart::Timeframe::FiveMinutes);
    QVERIFY2(latest.ok(), qPrintable(latest.error));
    QCOMPARE(latest.key.provider, QStringLiteral("Fresh Provider"));
    QCOMPARE(latest.bars.back().timestamp, std::int64_t{1'700'000'600});
}

QTEST_GUILESS_MAIN(HistoricalDataStoreTests)

#include "historical_data_store_tests.moc"
