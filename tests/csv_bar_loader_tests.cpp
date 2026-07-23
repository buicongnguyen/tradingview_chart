#include "data/csv_bar_loader.hpp"
#include "data/demo_data_source.hpp"
#include "domain/bar.hpp"

#include <QTest>

class CsvBarLoaderTests final : public QObject {
    Q_OBJECT

private slots:
    void parsesUnixAndIsoTimestamps();
    void rejectsMissingHeader();
    void rejectsInvalidOhlc();
    void rejectsDuplicateTimestamp();
    void demoDataIsDeterministicAndValid();
};

void CsvBarLoaderTests::parsesUnixAndIsoTimestamps() {
    const auto result = tvchart::CsvBarLoader::parse(QStringLiteral(
        "timestamp,open,high,low,close,volume\n"
        "1700000000,100,104,99,102,1000\n"
        "2026-07-24T09:31:00Z,102,105,101,104,1200\n"));

    QVERIFY2(result.ok(), qPrintable(result.error));
    QCOMPARE(result.bars.size(), std::size_t{2});
    QCOMPARE(result.bars.front().timestamp, std::int64_t{1'700'000'000});
    QCOMPARE(result.bars.back().timestamp, std::int64_t{1'784'885'460});
}

void CsvBarLoaderTests::rejectsMissingHeader() {
    const auto result = tvchart::CsvBarLoader::parse(QStringLiteral(
        "timestamp,open,high,low,close\n"
        "1700000000,100,104,99,102\n"));

    QVERIFY(!result.ok());
    QVERIFY(result.error.contains(QStringLiteral("volume")));
}

void CsvBarLoaderTests::rejectsInvalidOhlc() {
    const auto result = tvchart::CsvBarLoader::parse(QStringLiteral(
        "timestamp,open,high,low,close,volume\n"
        "1700000000,100,101,99,102,1000\n"));

    QVERIFY(!result.ok());
    QVERIFY(result.error.contains(QStringLiteral("high")));
}

void CsvBarLoaderTests::rejectsDuplicateTimestamp() {
    const auto result = tvchart::CsvBarLoader::parse(QStringLiteral(
        "timestamp,open,high,low,close,volume\n"
        "1700000000,100,104,99,102,1000\n"
        "1700000000,102,105,101,104,1200\n"));

    QVERIFY(!result.ok());
    QVERIFY(result.error.contains(QStringLiteral("strictly increasing")));
}

void CsvBarLoaderTests::demoDataIsDeterministicAndValid() {
    const auto first = tvchart::DemoDataSource::generate(
        "AAPL", tvchart::Timeframe::FiveMinutes, 500, 1'774'603'860);
    const auto second = tvchart::DemoDataSource::generate(
        "AAPL", tvchart::Timeframe::FiveMinutes, 500, 1'774'603'860);

    QCOMPARE(first, second);
    QCOMPARE(first.size(), std::size_t{500});
    const auto error = tvchart::validateBars(first);
    QVERIFY2(!error.has_value(), error ? error->c_str() : "");
}

QTEST_APPLESS_MAIN(CsvBarLoaderTests)

#include "csv_bar_loader_tests.moc"
