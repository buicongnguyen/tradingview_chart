#include "chart/chart_bridge.hpp"
#include "data/csv_bar_loader.hpp"
#include "data/demo_data_source.hpp"
#include "data/market_data_parser.hpp"
#include "data/market_data_quality.hpp"
#include "domain/bar.hpp"

#include <QDate>
#include <QDateTime>
#include <QSignalSpy>
#include <QTemporaryFile>
#include <QTest>
#include <QTime>
#include <QTimeZone>

class CsvBarLoaderTests final : public QObject {
    Q_OBJECT

private slots:
    void parsesUnixAndIsoTimestamps();
    void rejectsMissingHeader();
    void rejectsDuplicateHeader();
    void rejectsMalformedQuotedField();
    void rejectsInvalidOhlc();
    void rejectsDuplicateTimestamp();
    void treatsNaiveIsoTimestampAsUtc();
    void rejectsOutOfRangeTimestamp();
    void rejectsOversizedFileBeforeReading();
    void demoDataIsDeterministicAndValid();
    void demoDataIsStableAcrossRefreshes();
    void parsesYahooChartResponse();
    void parsesYahooCorporateActionsAndAdjustedClose();
    void appliesExplicitPriceAdjustments();
    void reportsMarketDataQuality();
    void rejectsUnsafeYahooTimestamp();
    void rejectsConflictingProviderTimestamps();
    void parsesTwelveDataResponseInAscendingOrder();
    void appliesTwelveDataTimezone();
    void reportsProviderErrors();
    void reportsUnexpectedJsonShape();
    void chartBridgeRepublishesStateAfterWebReload();
    void chartBridgeBoundsAndTogglesStructureOverlays();
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

void CsvBarLoaderTests::rejectsMalformedQuotedField() {
    const auto result = tvchart::CsvBarLoader::parse(QStringLiteral(
        "timestamp,open,high,low,close,volume\n"
        "1700000000,10\"0,104,99,102,1000\n"));

    QVERIFY(!result.ok());
    QVERIFY(result.error.contains(QStringLiteral("unexpected quote")));
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

void CsvBarLoaderTests::rejectsOversizedFileBeforeReading() {
    QTemporaryFile file;
    QVERIFY(file.open());
    QVERIFY(file.resize(qint64{64 * 1024 * 1024} + 1));
    const auto path = file.fileName();
    file.close();

    const auto result = tvchart::CsvBarLoader::loadFile(path);
    QVERIFY(!result.ok());
    QVERIFY(result.error.contains(QStringLiteral("64 MiB")));
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
              "meta": {
                "fullExchangeName": "NasdaqGS",
                "currency": "USD",
                "exchangeTimezoneName": "America/New_York",
                "instrumentType": "EQUITY",
                "dataGranularity": "5m",
                "exchangeDataDelayedBy": 0
              },
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
    QCOMPARE(result.metadata.exchange, QStringLiteral("NasdaqGS"));
    QCOMPARE(result.metadata.currency, QStringLiteral("USD"));
    QCOMPARE(result.metadata.timezone, QStringLiteral("America/New_York"));
    QCOMPARE(result.metadata.instrumentType, QStringLiteral("EQUITY"));
    QCOMPARE(result.metadata.interval, QStringLiteral("5m"));
    QCOMPARE(result.metadata.exchangeDelayMinutes, std::optional<int>{0});
    QCOMPARE(result.inputRows, std::size_t{3});
    QCOMPARE(result.rejectedRows, std::size_t{1});
}

void CsvBarLoaderTests::parsesYahooCorporateActionsAndAdjustedClose() {
    const auto result = tvchart::MarketDataParser::parseYahoo(R"json(
        {
          "chart": {
            "result": [{
              "timestamp": [1700000000, 1700086400, 1700172800],
              "events": {
                "dividends": {
                  "1700086400": {
                    "date": 1700086400,
                    "amount": 0.25,
                    "currency": "USD"
                  }
                },
                "splits": {
                  "1700172800": {
                    "date": 1700172800,
                    "numerator": 4,
                    "denominator": 1,
                    "splitRatio": "4:1"
                  }
                }
              },
              "indicators": {
                "quote": [{
                  "open": [96, 104, 27],
                  "high": [104, 112, 29],
                  "low": [92, 100, 26],
                  "close": [100, 108, 28],
                  "volume": [1000, 1200, 5000]
                }],
                "adjclose": [{
                  "adjclose": [25, 27, 28]
                }]
              }
            }],
            "error": null
          }
        }
    )json");

    QVERIFY2(result.ok(), qPrintable(result.error));
    QCOMPARE(result.corporateActions.size(), std::size_t{2});
    QCOMPARE(
        result.corporateActions.front().type,
        tvchart::CorporateActionType::CashDividend);
    QCOMPARE(result.corporateActions.front().amount, 0.25);
    QCOMPARE(
        result.corporateActions.back().type,
        tvchart::CorporateActionType::StockSplit);
    QCOMPARE(result.corporateActions.back().numerator, 4.0);
    QCOMPARE(result.corporateActions.back().denominator, 1.0);
    QCOMPARE(result.adjustedCloses.size(), std::size_t{3});
    QCOMPARE(result.adjustedCloses.front().adjustedClose, 25.0);
}

void CsvBarLoaderTests::appliesExplicitPriceAdjustments() {
    const tvchart::Bars raw{
        {1'700'000'000, 96, 104, 92, 100, 1000},
        {1'700'086'400, 104, 112, 100, 108, 1200},
        {1'700'172'800, 27, 29, 26, 28, 5000},
    };
    const std::vector<tvchart::CorporateAction> actions{{
        .type = tvchart::CorporateActionType::StockSplit,
        .timestamp = 1'700'172'800,
        .numerator = 4,
        .denominator = 1,
        .provider = QStringLiteral("Yahoo Finance"),
    }};
    const std::vector<tvchart::AdjustedClosePoint> adjusted{
        {1'700'000'000, 24.5},
        {1'700'086'400, 26.75},
        {1'700'172'800, 28.0},
    };

    const auto split = tvchart::applyPriceAdjustment(
        raw,
        actions,
        adjusted,
        tvchart::PriceAdjustmentMode::SplitAdjusted);
    QVERIFY2(split.ok(), qPrintable(split.error));
    QCOMPARE(
        split.appliedMode,
        tvchart::PriceAdjustmentMode::SplitAdjusted);
    QCOMPARE(split.bars.front().close, 25.0);
    QCOMPARE(split.bars.front().volume, 4000.0);
    QCOMPARE(split.bars.back(), raw.back());

    const auto total = tvchart::applyPriceAdjustment(
        raw,
        actions,
        adjusted,
        tvchart::PriceAdjustmentMode::TotalReturn);
    QVERIFY2(total.ok(), qPrintable(total.error));
    QCOMPARE(
        total.appliedMode,
        tvchart::PriceAdjustmentMode::TotalReturn);
    QCOMPARE(total.bars.front().close, 24.5);
    QCOMPARE(total.bars.front().volume, raw.front().volume);

    const auto unavailable = tvchart::applyPriceAdjustment(
        raw,
        actions,
        {},
        tvchart::PriceAdjustmentMode::TotalReturn);
    QVERIFY(unavailable.ok());
    QCOMPARE(
        unavailable.appliedMode,
        tvchart::PriceAdjustmentMode::Raw);
    QVERIFY(!unavailable.warning.isEmpty());
    QCOMPARE(unavailable.bars, raw);
}

void CsvBarLoaderTests::reportsMarketDataQuality() {
    const tvchart::Bars bars{
        {1'700'000'000, 100, 101, 99, 100, 1000},
        {1'700'000'300, 100, 101, 99, 100, 0},
        {1'700'001'200, 160, 161, 159, 160, 1000},
    };
    const auto report = tvchart::analyzeMarketDataQuality(
        bars,
        tvchart::Timeframe::FiveMinutes,
        4,
        1,
        0,
        {});
    QCOMPARE(report.acceptedRows, std::size_t{3});
    QCOMPARE(report.rejectedRows, std::size_t{1});
    QCOMPARE(report.suspiciousGaps, std::size_t{2});
    QCOMPARE(report.outlierBars, std::size_t{1});
    QCOMPARE(report.zeroVolumeBars, std::size_t{1});
    QCOMPARE(report.grade, tvchart::DataQualityGrade::Poor);
    QVERIFY(!report.issues.empty());

    const tvchart::Bars splitBars{
        {1'700'000'000, 100, 101, 99, 100, 1000},
        {1'700'000'300, 50, 51, 49, 50, 2000},
    };
    const std::vector<tvchart::CorporateAction> splitActions{{
        .type = tvchart::CorporateActionType::StockSplit,
        .timestamp = 1'699'953'200,
        .numerator = 2,
        .denominator = 1,
        .provider = QStringLiteral("Yahoo Finance"),
    }};
    const auto splitReport = tvchart::analyzeMarketDataQuality(
        splitBars,
        tvchart::Timeframe::FiveMinutes,
        splitBars.size(),
        0,
        0,
        splitActions);
    QCOMPARE(splitReport.outlierBars, std::size_t{0});
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

void CsvBarLoaderTests::rejectsConflictingProviderTimestamps() {
    const auto result = tvchart::MarketDataParser::parseYahoo(R"json(
        {
          "chart": {
            "result": [{
              "timestamp": [1700000000, 1700000000],
              "indicators": {
                "quote": [{
                  "open": [100.0, 100.0],
                  "high": [104.0, 105.0],
                  "low": [99.0, 99.0],
                  "close": [102.0, 103.0],
                  "volume": [1000, 1000]
                }]
              }
            }],
            "error": null
          }
        }
    )json");

    QVERIFY(!result.ok());
    QVERIFY(result.error.contains(QStringLiteral("conflicting bars")));
}

void CsvBarLoaderTests::parsesTwelveDataResponseInAscendingOrder() {
    const auto result = tvchart::MarketDataParser::parseTwelveData(R"json(
        {
          "meta": {
            "symbol": "AAPL",
            "interval": "5min",
            "currency": "USD",
            "exchange": "NASDAQ",
            "exchange_timezone": "America/New_York",
            "type": "Common Stock"
          },
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
    const auto expected =
        QDateTime(
            QDate(2026, 7, 24),
            QTime(9, 30),
            QTimeZone(QByteArrayLiteral("America/New_York")))
            .toSecsSinceEpoch();
    QCOMPARE(result.bars.front().timestamp, expected);
    QCOMPARE(result.bars.front().open, 100.0);
    QCOMPARE(result.bars.back().close, 105.0);
    QCOMPARE(result.metadata.exchange, QStringLiteral("NASDAQ"));
    QCOMPARE(result.metadata.currency, QStringLiteral("USD"));
    QCOMPARE(result.metadata.timezone, QStringLiteral("America/New_York"));
    QCOMPARE(result.metadata.instrumentType, QStringLiteral("Common Stock"));
    QCOMPARE(result.metadata.interval, QStringLiteral("5min"));
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
    bridge.setIndicators({{
        .kind = tvchart::IndicatorKind::SimpleMovingAverage,
        .spec = tvchart::defaultIndicatorSpec(
            tvchart::IndicatorKind::SimpleMovingAverage),
        .label = "SMA (20)",
        .primary = {{
            .timestamp = 1'700'000'000,
            .value = 101.5,
        }},
    }});
    bridge.setResearchEvents({{
        .id = QStringLiteral("event"),
        .symbol = QStringLiteral("AAPL"),
        .type = tvchart::ResearchEventType::Filing,
        .scheduledDate = QDate(2023, 11, 14),
        .timeOfDay = QStringLiteral("filed"),
        .title = QStringLiteral("AAPL 10-Q filing"),
        .source = QStringLiteral("SEC EDGAR"),
        .asOfUtc = 1'700'000'100,
        .confidence = tvchart::ResearchConfidence::Confirmed,
    }});
    bridge.setMarketStructure({
        .symbol = QStringLiteral("AAPL"),
        .timeframe = tvchart::Timeframe::FiveMinutes,
        .barsAnalyzed = 1,
        .asOfTimestamp = 1'700'000'000,
        .latestClose = 102.0,
        .zones = {{
            .type = tvchart::StructureZoneType::Support,
            .low = 99.0,
            .high = 100.0,
            .center = 99.5,
            .touches = 2,
            .strength = 2.0,
            .distanceFromClosePercent = -2.45,
            .firstAnchorTimestamp = 1'700'000'000,
            .lastAnchorTimestamp = 1'700'000'000,
            .detectionTimestamp = 1'700'000'000,
            .broken = false,
            .explanation = QStringLiteral("Confirmed support"),
        }},
        .bias = tvchart::StructureBias::Neutral,
    });
    QVERIFY(bridge.setPriceLevels({{
        .id = QStringLiteral("support"),
        .symbol = QStringLiteral("AAPL"),
        .price = 95.0,
        .title = QStringLiteral("Support"),
        .color = QStringLiteral("#2962ff"),
    }}));

    QSignalSpy seriesSpy(&bridge, &tvchart::ChartBridge::seriesChanged);
    QSignalSpy indicatorSpy(&bridge, &tvchart::ChartBridge::indicatorsChanged);
    QSignalSpy eventSpy(&bridge, &tvchart::ChartBridge::researchEventsChanged);
    QSignalSpy structureSpy(
        &bridge,
        &tvchart::ChartBridge::marketStructureChanged);
    QSignalSpy levelSpy(&bridge, &tvchart::ChartBridge::priceLevelsChanged);
    QSignalSpy readySpy(&bridge, &tvchart::ChartBridge::ready);

    bridge.webReady();
    QCOMPARE(seriesSpy.count(), 1);
    QCOMPARE(indicatorSpy.count(), 1);
    QCOMPARE(eventSpy.count(), 1);
    QCOMPARE(structureSpy.count(), 1);
    QCOMPARE(levelSpy.count(), 1);
    QCOMPARE(readySpy.count(), 1);
    const auto firstArguments = seriesSpy.takeFirst();
    QCOMPARE(firstArguments.at(0).toString(), QStringLiteral("AAPL"));
    QCOMPARE(firstArguments.at(1).toString(), QStringLiteral("5m"));
    QCOMPARE(firstArguments.at(2).toString(), QStringLiteral("Yahoo Finance"));
    QCOMPARE(firstArguments.at(3).toJsonArray().size(), 1);
    const auto indicatorArguments = indicatorSpy.takeFirst();
    const auto indicators = indicatorArguments.at(0).toJsonArray();
    QCOMPARE(indicators.size(), 1);
    const auto indicator = indicators.at(0).toObject();
    QCOMPARE(indicator.value(QStringLiteral("kind")).toString(), QStringLiteral("sma"));
    QCOMPARE(
        indicator.value(QStringLiteral("primary")).toArray().size(),
        1);
    const auto markers = eventSpy.takeFirst().at(0).toJsonArray();
    QCOMPARE(markers.size(), 1);
    QCOMPARE(
        markers.at(0).toObject().value(QStringLiteral("text")).toString(),
        QStringLiteral("SEC"));
    const auto structure =
        structureSpy.takeFirst().at(0).toJsonObject();
    QCOMPARE(
        structure.value(QStringLiteral("zones")).toArray().size(),
        1);
    const auto levels = levelSpy.takeFirst().at(0).toJsonArray();
    QCOMPARE(levels.size(), 1);
    QCOMPARE(
        levels.at(0).toObject().value(QStringLiteral("price")).toDouble(),
        95.0);

    QSignalSpy rangeSpy(&bridge, &tvchart::ChartBridge::visibleRangeReported);
    QSignalSpy crosshairSpy(
        &bridge,
        &tvchart::ChartBridge::crosshairTimeReported);
    bridge.reportVisibleRange(1'700'000'000.0, 1'700'000'300.0);
    bridge.reportVisibleRange(10.0, 5.0);
    bridge.reportCrosshairTime(1'700'000'000.0);
    QCOMPARE(rangeSpy.count(), 1);
    QCOMPARE(crosshairSpy.count(), 1);

    bridge.webReady();
    QCOMPARE(seriesSpy.count(), 1);
    QCOMPARE(indicatorSpy.count(), 1);
    QCOMPARE(eventSpy.count(), 1);
    QCOMPARE(structureSpy.count(), 1);
    QCOMPARE(levelSpy.count(), 1);
    QCOMPARE(readySpy.count(), 1);
}

void CsvBarLoaderTests::chartBridgeBoundsAndTogglesStructureOverlays() {
    tvchart::ChartBridge bridge;
    const tvchart::Bars bars{
        {
            .timestamp = 1'700'000'000,
            .open = 100.0,
            .high = 102.0,
            .low = 99.0,
            .close = 101.0,
            .volume = 1'000.0,
        },
        {
            .timestamp = 1'700'000'300,
            .open = 101.0,
            .high = 103.0,
            .low = 100.0,
            .close = 102.0,
            .volume = 1'100.0,
        },
    };
    QVERIFY(bridge.setSeries(
        QStringLiteral("AAPL"),
        QStringLiteral("5m"),
        QStringLiteral("Yahoo Finance"),
        bars));

    tvchart::MarketStructureReport report;
    report.symbol = QStringLiteral("AAPL");
    report.timeframe = tvchart::Timeframe::FiveMinutes;
    report.barsAnalyzed = bars.size();
    report.asOfTimestamp = bars.back().timestamp;
    report.latestClose = bars.back().close;
    for (auto index = 0; index < 2; ++index) {
        tvchart::ChartPattern pattern;
        pattern.id = QStringLiteral("rectangle-%1").arg(index);
        pattern.kind = tvchart::PatternKind::Rectangle;
        pattern.direction = tvchart::PatternDirection::Neutral;
        pattern.status = tvchart::PatternStatus::Confirmed;
        pattern.boundaries.push_back({
            .kind = tvchart::StructureLineKind::PatternBoundary,
            .startTimestamp = bars.front().timestamp,
            .endTimestamp = bars.back().timestamp,
            .startPrice = 100.0 + index,
            .endPrice = 100.0 + index,
            .title = QStringLiteral("Rectangle"),
            .color = QStringLiteral("#ff9800"),
            .dashed = true,
        });
        report.patterns.push_back(std::move(pattern));
    }
    bridge.setMarketStructure(std::move(report));
    bridge.setMarketStructureVisible(false);

    QSignalSpy structureSpy(
        &bridge,
        &tvchart::ChartBridge::marketStructureChanged);
    bridge.webReady();
    QCOMPARE(structureSpy.count(), 1);
    auto structure =
        structureSpy.takeFirst().at(0).toJsonObject();
    QCOMPARE(
        structure.value(QStringLiteral("zones")).toArray().size(),
        0);
    QCOMPARE(
        structure.value(QStringLiteral("lines")).toArray().size(),
        0);

    bridge.setMarketStructureVisible(true);
    QCOMPARE(structureSpy.count(), 1);
    structure = structureSpy.takeFirst().at(0).toJsonObject();
    QCOMPARE(
        structure.value(QStringLiteral("lines")).toArray().size(),
        1);

    bridge.setMarketStructureVisible(true);
    QCOMPARE(structureSpy.count(), 0);
}

QTEST_APPLESS_MAIN(CsvBarLoaderTests)

#include "csv_bar_loader_tests.moc"
