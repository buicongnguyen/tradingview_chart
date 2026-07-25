#include "data/csv_bar_loader.hpp"
#include "data/demo_data_source.hpp"
#include "data/market_data_parser.hpp"
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
    void parsesYahooChartResponse();
    void parsesTwelveDataResponseInAscendingOrder();
    void reportsProviderErrors();
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

void CsvBarLoaderTests::parsesYahooChartResponse() {
    const auto result = tvchart::MarketDataParser::parseYahoo(R"json(
        {
          "chart": {
            "result": [{
              "timestamp": [1700000000, 1700000300, 1700000600],
              "indicators": {
                "quote": [{
                  "open": [100.0, null, 102.0],
                  "high": [104.0, null, 106.0],
                  "low": [99.0, null, 101.0],
                  "close": [102.0, null, 105.0],
                  "volume": [1000, null, 1400]
                }]
              }
            }],
            "error": null
          }
        }
    )json");

    QVERIFY2(result.ok(), qPrintable(result.error));
    QCOMPARE(result.bars.size(), std::size_t{2});
    QCOMPARE(result.bars.front().timestamp, std::int64_t{1'700'000'000});
    QCOMPARE(result.bars.back().close, 105.0);
}

void CsvBarLoaderTests::parsesTwelveDataResponseInAscendingOrder() {
    const auto result = tvchart::MarketDataParser::parseTwelveData(R"json(
        {
          "meta": {"symbol": "AAPL", "interval": "5min"},
          "values": [
            {
              "datetime": "2026-07-24 09:35:00",
              "open": "102.0", "high": "106.0", "low": "101.0",
              "close": "105.0", "volume": "1400"
            },
            {
              "datetime": "2026-07-24 09:30:00",
              "open": "100.0", "high": "104.0", "low": "99.0",
              "close": "102.0", "volume": "1000"
            }
          ],
          "status": "ok"
        }
    )json");

    QVERIFY2(result.ok(), qPrintable(result.error));
    QCOMPARE(result.bars.size(), std::size_t{2});
    QVERIFY(result.bars.front().timestamp < result.bars.back().timestamp);
    QCOMPARE(result.bars.front().open, 100.0);
    QCOMPARE(result.bars.back().close, 105.0);
}

void CsvBarLoaderTests::reportsProviderErrors() {
    const auto yahoo = tvchart::MarketDataParser::parseYahoo(R"json(
        {"chart":{"result":null,"error":{"code":"Not Found","description":"No data"}}}
    )json");
    QVERIFY(!yahoo.ok());
    QVERIFY(yahoo.error.contains(QStringLiteral("No data")));

    const auto twelve = tvchart::MarketDataParser::parseTwelveData(R"json(
        {"code":401,"message":"invalid api key","status":"error"}
    )json");
    QVERIFY(!twelve.ok());
    QVERIFY(twelve.error.contains(QStringLiteral("invalid api key")));
}

QTEST_APPLESS_MAIN(CsvBarLoaderTests)

#include "csv_bar_loader_tests.moc"
