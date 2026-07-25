#include "chart/chart_bridge.hpp"
#include "data/csv_bar_loader.hpp"
#include "data/demo_data_source.hpp"
#include "data/market_data_parser.hpp"
#include "domain/bar.hpp"

#include <QDate>
#include <QDateTime>
#include <QSignalSpy>
#include <QTest>
#include <QTime>
#include <QTimeZone>

class CsvBarLoaderTests final : public QObject {
    Q_OBJECT

private slots:
    void parsesUnixAndIsoTimestamps();
    void rejectsMissingHeader();
    void rejectsDuplicateHeader();
    void rejectsInvalidOhlc();
    void rejectsDuplicateTimestamp();
    void treatsNaiveIsoTimestampAsUtc();
    void rejectsOutOfRangeTimestamp();
    void demoDataIsDeterministicAndValid();
    void demoDataIsStableAcrossRefreshes();
    void parsesYahooChartResponse();
    void rejectsUnsafeYahooTimestamp();
    void parsesTwelveDataResponseInAscendingOrder();
    void appliesTwelveDataTimezone();
    void reportsProviderErrors();
    void reportsUnexpectedJsonShape();
    void chartBridgeRepublishesStateAfterWebReload();
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

void CsvBarLoaderTests::rejectsDuplicateHeader() {
    const auto result = tvchart::CsvBarLoader::parse(QStringLiteral(
        "timestamp,open,high,low,close,volume,close\n"
        "1700000000,100,104,99,102,1000,103\n"));

    QVERIFY(!result.ok());
    QVERIFY(result.error.contains(QStringLiteral("Duplicate column")));
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

void CsvBarLoaderTests::treatsNaiveIsoTimestampAsUtc() {
    const auto result = tvchart::CsvBarLoader::parse(QStringLiteral(
        "timestamp,open,high,low,close,volume\r"
        "2026-07-24T09:30:00,100,104,99,102,1000\r"));

    QVERIFY2(result.ok(), qPrintable(result.error));
    const auto expected =
        QDateTime(QDate(2026, 7, 24), QTime(9, 30), QTimeZone::utc())
            .toSecsSinceEpoch();
    QCOMPARE(result.bars.front().timestamp, expected);
}

void CsvBarLoaderTests::rejectsOutOfRangeTimestamp() {
    const auto result = tvchart::CsvBarLoader::parse(QStringLiteral(
        "timestamp,open,high,low,close,volume\n"
        "999999999999999999,100,104,99,102,1000\n"));

    QVERIFY(!result.ok());
    QVERIFY(result.error.contains(QStringLiteral("invalid timestamp")));
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

void CsvBarLoaderTests::demoDataIsStableAcrossRefreshes() {
    constexpr auto interval = std::int64_t{300};
    const auto first = tvchart::DemoDataSource::generate(
        "AAPL", tvchart::Timeframe::FiveMinutes, 20, 1'774'603'800);
    const auto refreshed = tvchart::DemoDataSource::generate(
        "AAPL",
        tvchart::Timeframe::FiveMinutes,
        20,
        1'774'603'800 + interval);

    QCOMPARE(first.size(), std::size_t{20});
    QCOMPARE(refreshed.size(), std::size_t{20});
    for (std::size_t index = 1; index < first.size(); ++index) {
        QCOMPARE(first[index], refreshed[index - 1]);
    }
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

void CsvBarLoaderTests::rejectsUnsafeYahooTimestamp() {
    const auto result = tvchart::MarketDataParser::parseYahoo(R"json(
        {
          "chart": {
            "result": [{
              "timestamp": [1e100],
              "indicators": {
                "quote": [{
                  "open": [100.0], "high": [104.0], "low": [99.0],
                  "close": [102.0], "volume": [1000]
                }]
              }
            }],
            "error": null
          }
        }
    )json");

    QVERIFY(!result.ok());
    QVERIFY(result.error.contains(QStringLiteral("no usable")));
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

void CsvBarLoaderTests::appliesTwelveDataTimezone() {
    const auto result = tvchart::MarketDataParser::parseTwelveData(R"json(
        {
          "meta": {
            "symbol": "AAPL",
            "interval": "5min",
            "timezone": "America/New_York"
          },
          "values": [{
            "datetime": "2026-07-24 09:30:00",
            "open": "100.0", "high": "104.0", "low": "99.0",
            "close": "102.0", "volume": "1000"
          }],
          "status": "ok"
        }
    )json");

    QVERIFY2(result.ok(), qPrintable(result.error));
    const auto expected =
        QDateTime(
            QDate(2026, 7, 24),
            QTime(9, 30),
            QTimeZone(QByteArrayLiteral("America/New_York")))
            .toSecsSinceEpoch();
    QCOMPARE(result.bars.front().timestamp, expected);
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

void CsvBarLoaderTests::reportsUnexpectedJsonShape() {
    const auto yahoo = tvchart::MarketDataParser::parseYahoo(QByteArrayLiteral("[]"));
    QVERIFY(!yahoo.ok());
    QVERIFY(yahoo.error.contains(QStringLiteral("top-level")));
    QVERIFY(!yahoo.error.contains(QStringLiteral("no error occurred")));

    const auto twelve =
        tvchart::MarketDataParser::parseTwelveData(QByteArrayLiteral("[]"));
    QVERIFY(!twelve.ok());
    QVERIFY(twelve.error.contains(QStringLiteral("top-level")));
}

void CsvBarLoaderTests::chartBridgeRepublishesStateAfterWebReload() {
    tvchart::ChartBridge bridge;
    const tvchart::Bars bars{
        {
            .timestamp = 1'700'000'000,
            .open = 100.0,
            .high = 104.0,
            .low = 99.0,
            .close = 102.0,
            .volume = 1'000.0,
        },
    };
    QVERIFY(bridge.setSeries(
        QStringLiteral("AAPL"),
        QStringLiteral("5m"),
        QStringLiteral("Yahoo Finance"),
        bars));
    bridge.setIndicator({
        .kind = tvchart::IndicatorKind::SimpleMovingAverage,
        .primary = {{
            .timestamp = 1'700'000'000,
            .value = 101.5,
        }},
    });

    QSignalSpy seriesSpy(&bridge, &tvchart::ChartBridge::seriesChanged);
    QSignalSpy indicatorSpy(&bridge, &tvchart::ChartBridge::indicatorChanged);
    QSignalSpy readySpy(&bridge, &tvchart::ChartBridge::ready);

    bridge.webReady();
    QCOMPARE(seriesSpy.count(), 1);
    QCOMPARE(indicatorSpy.count(), 1);
    QCOMPARE(readySpy.count(), 1);
    const auto firstArguments = seriesSpy.takeFirst();
    QCOMPARE(firstArguments.at(0).toString(), QStringLiteral("AAPL"));
    QCOMPARE(firstArguments.at(1).toString(), QStringLiteral("5m"));
    QCOMPARE(firstArguments.at(2).toString(), QStringLiteral("Yahoo Finance"));
    QCOMPARE(firstArguments.at(3).toJsonArray().size(), 1);
    const auto indicatorArguments = indicatorSpy.takeFirst();
    const auto indicator = indicatorArguments.at(0).toJsonObject();
    QCOMPARE(indicator.value(QStringLiteral("kind")).toString(), QStringLiteral("sma"));
    QCOMPARE(
        indicator.value(QStringLiteral("primary")).toArray().size(),
        1);

    bridge.webReady();
    QCOMPARE(seriesSpy.count(), 1);
    QCOMPARE(indicatorSpy.count(), 1);
    QCOMPARE(readySpy.count(), 1);
}

QTEST_APPLESS_MAIN(CsvBarLoaderTests)

#include "csv_bar_loader_tests.moc"
