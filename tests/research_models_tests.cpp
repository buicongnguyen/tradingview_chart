#include "research/margin_risk.hpp"
#include "research/alpha_vantage_research_parser.hpp"
#include "research/event_intelligence_parser.hpp"
#include "research/research_models.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTest>

#include <cmath>
#include <stdexcept>

class ResearchModelsTests final : public QObject {
    Q_OBJECT

private slots:
    void summarizesOrganizationTargets();
    void usesLatestTargetPerOrganization();
    void keepsAggregatedConsensusSeparate();
    void rejectsInvalidResearchSymbols();
    void roundTripsResearchWorkspace();
    void rejectsMalformedResearchWorkspaceFields();
    void boundsResearchWorkspacePersistence();
    void importsQuotedTargetCsv();
    void boundsTargetsAndNeutralizesFormulaText();
    void parsesAlphaVantageResearch();
    void parsesAlphaVantageEarningsCalendar();
    void rejectsAlphaVantageProviderMessages();
    void parsesSecTickerAndFilingEvents();
    void parsesMajorFredReleaseEvents();
    void calculatesMarginScenarios();
    void rejectsInvalidMarginAssumptions();
};

void ResearchModelsTests::summarizesOrganizationTargets() {
    const std::vector<tvchart::AnalystTargetEstimate> estimates{
        {
            .id = QStringLiteral("one"),
            .symbol = QStringLiteral("AAPL"),
            .organization = QStringLiteral("Firm A"),
            .targetPrice = 180.0,
            .currency = QStringLiteral("USD"),
            .publishedDate = QDate(2026, 7, 1),
        },
        {
            .id = QStringLiteral("two"),
            .symbol = QStringLiteral("AAPL"),
            .organization = QStringLiteral("Firm B"),
            .targetPrice = 220.0,
            .currency = QStringLiteral("USD"),
            .publishedDate = QDate(2026, 7, 2),
        },
        {
            .id = QStringLiteral("three"),
            .symbol = QStringLiteral("AAPL"),
            .organization = QStringLiteral("Firm C"),
            .targetPrice = 200.0,
            .currency = QStringLiteral("USD"),
            .publishedDate = QDate(2026, 7, 3),
        },
    };
    const auto summary = tvchart::summarizeOrganizationTargets(
        estimates,
        QStringLiteral("aapl"),
        QStringLiteral("usd"));
    QVERIFY(summary.has_value());
    QCOMPARE(summary->organizationCount, std::size_t{3});
    QCOMPARE(summary->minimum, 180.0);
    QCOMPARE(summary->maximum, 220.0);
    QCOMPARE(summary->mean, 200.0);
    QCOMPARE(summary->median, 200.0);
    QCOMPARE(summary->dispersion, 0.2);
}

void ResearchModelsTests::usesLatestTargetPerOrganization() {
    const std::vector<tvchart::AnalystTargetEstimate> estimates{
        {
            .id = QStringLiteral("firm-a-old"),
            .symbol = QStringLiteral("AAPL"),
            .organization = QStringLiteral("Firm A"),
            .targetPrice = 180.0,
            .currency = QStringLiteral("USD"),
            .publishedDate = QDate(2026, 6, 1),
        },
        {
            .id = QStringLiteral("firm-a-new"),
            .symbol = QStringLiteral("AAPL"),
            .organization = QStringLiteral("firm a"),
            .targetPrice = 210.0,
            .currency = QStringLiteral("USD"),
            .publishedDate = QDate(2026, 7, 1),
        },
        {
            .id = QStringLiteral("firm-b"),
            .symbol = QStringLiteral("AAPL"),
            .organization = QStringLiteral("Firm B"),
            .targetPrice = 190.0,
            .currency = QStringLiteral("USD"),
            .publishedDate = QDate(2026, 7, 2),
        },
    };
    const auto summary = tvchart::summarizeOrganizationTargets(
        estimates,
        QStringLiteral("AAPL"),
        QStringLiteral("USD"));
    QVERIFY(summary.has_value());
    QCOMPARE(summary->organizationCount, std::size_t{2});
    QCOMPARE(summary->mean, 200.0);
    QCOMPARE(summary->minimum, 190.0);
    QCOMPARE(summary->maximum, 210.0);
}

void ResearchModelsTests::keepsAggregatedConsensusSeparate() {
    const std::vector<tvchart::AnalystTargetEstimate> estimates{{
        .id = QStringLiteral("aggregate"),
        .symbol = QStringLiteral("IBM"),
        .organization = QStringLiteral("Provider consensus"),
        .targetPrice = 245.33,
        .currency = QStringLiteral("USD"),
        .publishedDate = QDate(2026, 7, 26),
        .scope = tvchart::TargetEstimateScope::AggregatedConsensus,
    }};
    QVERIFY(!tvchart::summarizeOrganizationTargets(
                 estimates,
                 QStringLiteral("IBM"),
                 QStringLiteral("USD"))
                 .has_value());
}

void ResearchModelsTests::rejectsInvalidResearchSymbols() {
    const tvchart::AnalystTargetEstimate target{
        .id = QStringLiteral("invalid-symbol"),
        .symbol = QStringLiteral("AAPL?"),
        .organization = QStringLiteral("Firm A"),
        .targetPrice = 200.0,
        .currency = QStringLiteral("USD"),
        .publishedDate = QDate(2026, 7, 1),
    };
    QVERIFY(!tvchart::validateTargetEstimate(target).isEmpty());

    const tvchart::ResearchEvent event{
        .id = QStringLiteral("invalid-event-symbol"),
        .symbol = QStringLiteral("AAPL?"),
        .scheduledDate = QDate(2026, 8, 1),
        .title = QStringLiteral("Invalid"),
        .source = QStringLiteral("Test"),
        .asOfUtc = 1'775'000'000,
    };
    QVERIFY(!tvchart::validateResearchEvent(event).isEmpty());
}

void ResearchModelsTests::roundTripsResearchWorkspace() {
    tvchart::ResearchWorkspace workspace{
        .events = {{
            .id = QStringLiteral("earnings-aapl"),
            .symbol = QStringLiteral("AAPL"),
            .type = tvchart::ResearchEventType::Earnings,
            .scheduledDate = QDate(2026, 8, 1),
            .timeOfDay = QStringLiteral("after_market"),
            .title = QStringLiteral("AAPL earnings"),
            .source = QStringLiteral("Provider"),
            .asOfUtc = 1'775'000'000,
            .confidence = tvchart::ResearchConfidence::Estimated,
            .estimate = 1.5,
            .currency = QStringLiteral("USD"),
        }},
        .targetEstimates = {{
            .id = QStringLiteral("target-aapl"),
            .symbol = QStringLiteral("AAPL"),
            .organization = QStringLiteral("Firm A"),
            .targetPrice = 210.0,
            .currency = QStringLiteral("USD"),
            .publishedDate = QDate(2026, 7, 20),
            .rating = QStringLiteral("Buy"),
            .sourceUrl = QStringLiteral("https://example.com/research"),
        }},
        .companySnapshots = {{
            .symbol = QStringLiteral("AAPL"),
            .provider = QStringLiteral("Provider"),
            .asOfUtc = 1'775'000'000,
            .name = QStringLiteral("Apple"),
            .currency = QStringLiteral("USD"),
            .analystTargetPrice = 220.0,
            .ratings = {.buy = 10, .hold = 2},
        }},
    };
    const auto loaded = tvchart::deserializeResearchWorkspace(
        tvchart::serializeResearchWorkspace(workspace));
    QVERIFY2(loaded.ok(), qPrintable(loaded.error));
    QCOMPARE(loaded.workspace, workspace);
}

void ResearchModelsTests::rejectsMalformedResearchWorkspaceFields() {
    const tvchart::ResearchWorkspace workspace{
        .companySnapshots = {{
            .symbol = QStringLiteral("AAPL"),
            .provider = QStringLiteral("Provider"),
            .asOfUtc = 1'775'000'000,
            .currency = QStringLiteral("USD"),
            .analystTargetPrice = 220.0,
            .ratings = {.buy = 10},
        }},
    };
    auto document = QJsonDocument::fromJson(
        tvchart::serializeResearchWorkspace(workspace));
    auto root = document.object();
    auto snapshots =
        root.value(QStringLiteral("companySnapshots")).toArray();
    auto snapshot = snapshots.at(0).toObject();
    snapshot.insert(
        QStringLiteral("analystTargetPrice"),
        QStringLiteral("not-a-number"));
    snapshots.replace(0, snapshot);
    root.insert(QStringLiteral("companySnapshots"), snapshots);
    const auto invalidNumber = tvchart::deserializeResearchWorkspace(
        QJsonDocument(root).toJson(QJsonDocument::Compact));
    QVERIFY(!invalidNumber.ok());
    QVERIFY(invalidNumber.error.contains(QStringLiteral("company research")));

    root.remove(QStringLiteral("events"));
    const auto missingCollection = tvchart::deserializeResearchWorkspace(
        QJsonDocument(root).toJson(QJsonDocument::Compact));
    QVERIFY(!missingCollection.ok());
    QVERIFY(missingCollection.error.contains(QStringLiteral("collections")));
}

void ResearchModelsTests::boundsResearchWorkspacePersistence() {
    tvchart::ResearchWorkspace invalid{
        .companySnapshots = {{
            .symbol = QStringLiteral("AAPL"),
            .provider = QString(121, u'P'),
            .asOfUtc = 1'775'000'000,
            .currency = QStringLiteral("USD"),
        }},
    };
    QVERIFY(tvchart::serializeResearchWorkspace(invalid).isEmpty());

    const auto oversized = tvchart::deserializeResearchWorkspace(
        QByteArray(8 * 1024 * 1024 + 1, ' '));
    QVERIFY(!oversized.ok());
    QVERIFY(oversized.error.contains(QStringLiteral("8 MiB")));

    tvchart::ResearchWorkspace tooMany;
    tooMany.events.resize(tvchart::ResearchWorkspace::maximumEvents + 1);
    QVERIFY(tvchart::serializeResearchWorkspace(tooMany).isEmpty());
}

void ResearchModelsTests::importsQuotedTargetCsv() {
    const auto imported = tvchart::importTargetEstimatesCsv(
        QByteArrayLiteral(
            "symbol,organization,target,currency,published_date,rating,source_url\n"
            "AAPL,\"Firm, Inc.\",210.5,USD,2026-07-20,Buy,https://example.com/a\n"
            "AAPL,\"Firm, Inc.\",190.0,EUR,2026-07-20,Buy,https://example.com/eu\n"
            "AAPL,Bad,-1,USD,2026-07-20,Sell,\n"));
    QVERIFY2(imported.ok(), qPrintable(imported.error));
    QCOMPARE(imported.estimates.size(), std::size_t{2});
    QCOMPARE(
        imported.estimates.front().organization,
        QStringLiteral("Firm, Inc."));
    QCOMPARE(imported.rejectedLines, std::vector<std::size_t>({4}));

    const auto exported =
        tvchart::exportTargetEstimatesCsv(imported.estimates);
    const auto roundTrip = tvchart::importTargetEstimatesCsv(exported);
    QVERIFY(roundTrip.ok());
    QCOMPARE(roundTrip.estimates.size(), std::size_t{2});
    QCOMPARE(roundTrip.estimates.front().targetPrice, 210.5);
}

void ResearchModelsTests::boundsTargetsAndNeutralizesFormulaText() {
    const tvchart::AnalystTargetEstimate tooLarge{
        .id = QStringLiteral("too-large"),
        .symbol = QStringLiteral("AAPL"),
        .organization = QStringLiteral("Firm"),
        .targetPrice = 1'000'000'000.01,
        .currency = QStringLiteral("USD"),
        .publishedDate = QDate(2026, 7, 26),
    };
    QVERIFY(!tvchart::validateTargetEstimate(tooLarge).isEmpty());

    const tvchart::AnalystTargetEstimate formulaText{
        .id = QStringLiteral("safe-export"),
        .symbol = QStringLiteral("AAPL"),
        .organization = QStringLiteral("=WEBSERVICE(\"https://example.com\")"),
        .targetPrice = 250.0,
        .currency = QStringLiteral("USD"),
        .publishedDate = QDate(2026, 7, 26),
        .rating = QStringLiteral("@SUM(1,1)"),
    };
    const auto exported =
        tvchart::exportTargetEstimatesCsv({formulaText});
    QVERIFY(exported.contains(QByteArrayLiteral("\"'=WEBSERVICE(")));
    QVERIFY(exported.contains(QByteArrayLiteral("\"'@SUM(1,1)\"")));

    const auto imported = tvchart::importTargetEstimatesCsv(exported);
    QVERIFY2(imported.ok(), qPrintable(imported.error));
    QCOMPARE(imported.estimates.size(), std::size_t{1});
    QCOMPARE(imported.estimates.front().organization, formulaText.organization);
    QCOMPARE(imported.estimates.front().rating, formulaText.rating);
}

void ResearchModelsTests::parsesAlphaVantageResearch() {
    const auto parsed = tvchart::AlphaVantageResearchParser::parseOverview(
        QByteArrayLiteral(R"json({
          "Symbol":"IBM",
          "Name":"International Business Machines",
          "CIK":"51143",
          "Exchange":"NYSE",
          "Currency":"USD",
          "Sector":"TECHNOLOGY",
          "Industry":"INFORMATION TECHNOLOGY SERVICES",
          "MarketCapitalization":"201795764000",
          "EPS":"11.67",
          "PERatio":"18.35",
          "ForwardPE":"16.98",
          "Beta":"0.675",
          "52WeekHigh":"332.46",
          "52WeekLow":"199.19",
          "AnalystTargetPrice":"245.33",
          "AnalystRatingStrongBuy":"3",
          "AnalystRatingBuy":"12",
          "AnalystRatingHold":"7",
          "AnalystRatingSell":"0",
          "AnalystRatingStrongSell":"1",
          "DividendDate":"2026-09-10",
          "ExDividendDate":"2026-08-10"
        })json"),
        1'775'000'000);
    QVERIFY2(parsed.ok(), qPrintable(parsed.error));
    QCOMPARE(parsed.snapshot.symbol, QStringLiteral("IBM"));
    QCOMPARE(parsed.snapshot.analystTargetPrice, std::optional<double>{245.33});
    QCOMPARE(parsed.snapshot.ratings.total(), 23);
    QCOMPARE(parsed.corporateEvents.size(), std::size_t{2});
    QCOMPARE(
        parsed.corporateEvents.front().type,
        tvchart::ResearchEventType::ExDividend);
}

void ResearchModelsTests::parsesAlphaVantageEarningsCalendar() {
    const auto parsed =
        tvchart::AlphaVantageResearchParser::parseEarningsCalendar(
            QByteArrayLiteral(
                "symbol,name,reportDate,fiscalDateEnding,estimate,currency,timeOfTheDay\r\n"
                "IBM,\"International Business Machines\",2026-10-21,2026-09-30,2.45,USD,after_market\r\n"),
            1'775'000'000);
    QVERIFY2(parsed.ok(), qPrintable(parsed.error));
    QCOMPARE(parsed.events.size(), std::size_t{1});
    QCOMPARE(parsed.events.front().symbol, QStringLiteral("IBM"));
    QCOMPARE(parsed.events.front().scheduledDate, QDate(2026, 10, 21));
    QCOMPARE(parsed.events.front().estimate, std::optional<double>{2.45});
    QCOMPARE(
        parsed.events.front().confidence,
        tvchart::ResearchConfidence::Estimated);
}

void ResearchModelsTests::rejectsAlphaVantageProviderMessages() {
    const auto overview = tvchart::AlphaVantageResearchParser::parseOverview(
        QByteArrayLiteral(R"json({"Information":"Daily request limit reached"})json"),
        1'775'000'000);
    QVERIFY(!overview.ok());
    QVERIFY(overview.error.contains(QStringLiteral("request limit")));

    const auto calendar =
        tvchart::AlphaVantageResearchParser::parseEarningsCalendar(
            QByteArrayLiteral(R"json({"Note":"Try again later"})json"),
            1'775'000'000);
    QVERIFY(!calendar.ok());
    QVERIFY(calendar.error.contains(QStringLiteral("Try again")));
}

void ResearchModelsTests::parsesSecTickerAndFilingEvents() {
    const auto ticker = tvchart::EventIntelligenceParser::parseSecTickerMap(
        R"json({
            "0": {"cik_str": 320193, "ticker": "AAPL", "title": "Apple Inc."},
            "1": {"cik_str": 789019, "ticker": "MSFT", "title": "Microsoft"}
        })json",
        QStringLiteral("aapl"));
    QVERIFY2(ticker.ok(), qPrintable(ticker.error));
    QCOMPARE(ticker.cik, QStringLiteral("0000320193"));
    QCOMPARE(ticker.companyName, QStringLiteral("Apple Inc."));

    const auto filings =
        tvchart::EventIntelligenceParser::parseSecSubmissions(
            R"json({
                "cik": "0000320193",
                "filings": {
                    "recent": {
                        "accessionNumber": [
                            "0000320193-26-000001",
                            "0000320193-26-000002"
                        ],
                        "filingDate": ["2026-07-24", "2026-07-23"],
                        "reportDate": ["2026-06-30", "2026-06-30"],
                        "form": ["10-Q", "S-8"],
                        "primaryDocument": ["aapl-20260630.htm", "ignored.htm"]
                    }
                }
            })json",
            QStringLiteral("AAPL"),
            ticker.cik,
            1'800'000'000);
    QVERIFY2(filings.ok(), qPrintable(filings.error));
    QCOMPARE(filings.events.size(), std::size_t{1});
    const auto& event = filings.events.front();
    QCOMPARE(event.type, tvchart::ResearchEventType::Filing);
    QCOMPARE(event.source, QStringLiteral("SEC EDGAR"));
    QCOMPARE(event.confidence, tvchart::ResearchConfidence::Confirmed);
    QVERIFY(event.detail.contains(QStringLiteral("2026-06-30")));
    QVERIFY(event.detail.contains(QStringLiteral("sec.gov/Archives")));

    const auto mismatch =
        tvchart::EventIntelligenceParser::parseSecSubmissions(
            R"json({"cik":"0000789019","filings":{"recent":{}}})json",
            QStringLiteral("AAPL"),
            ticker.cik,
            1'800'000'000);
    QVERIFY(!mismatch.ok());
}

void ResearchModelsTests::parsesMajorFredReleaseEvents() {
    const auto parsed =
        tvchart::EventIntelligenceParser::parseFredReleaseDates(
            R"json({
                "release_dates": [
                    {
                        "release_id": 10,
                        "release_name": "Consumer Price Index",
                        "date": "2026-08-12"
                    },
                    {
                        "release_id": 999,
                        "release_name": "Minor weekly survey",
                        "date": "2026-08-13"
                    },
                    {
                        "release_id": 53,
                        "release_name": "Gross Domestic Product",
                        "date": "2027-01-01"
                    }
                ]
            })json",
            1'800'000'000,
            QDate(2026, 8, 1),
            QDate(2026, 9, 1));
    QVERIFY2(parsed.ok(), qPrintable(parsed.error));
    QCOMPARE(parsed.events.size(), std::size_t{1});
    QCOMPARE(
        parsed.events.front().type,
        tvchart::ResearchEventType::EconomicRelease);
    QCOMPARE(parsed.events.front().source, QStringLiteral("FRED"));
    QVERIFY(parsed.events.front().symbol.isEmpty());

    const auto providerError =
        tvchart::EventIntelligenceParser::parseFredReleaseDates(
            R"json({"error_code":400,"error_message":"Bad request"})json",
            1'800'000'000,
            QDate(2026, 8, 1),
            QDate(2026, 9, 1));
    QVERIFY(!providerError.ok());
}

void ResearchModelsTests::calculatesMarginScenarios() {
    const tvchart::MarginRiskInput input{
        .longMarketValue = 100'000.0,
        .marginDebit = 50'000.0,
        .otherEquity = 0.0,
        .maintenanceRate = 0.25,
        .stressPercent = -20.0,
    };
    const auto result = tvchart::calculateMarginRisk(input);
    QCOMPARE(result.currentEquity, 50'000.0);
    QCOMPARE(result.currentRequirement, 25'000.0);
    QCOMPARE(result.currentCushion, 25'000.0);
    QCOMPARE(result.stressedMarketValue, 80'000.0);
    QCOMPARE(result.stressedEquity, 30'000.0);
    QCOMPARE(result.stressedRequirement, 20'000.0);
    QCOMPARE(result.stressedCushion, 10'000.0);
    QVERIFY(result.callMarketValue.has_value());
    QVERIFY(
        std::abs(*result.callMarketValue - 66'666.6666666667) < 1e-8);
    QVERIFY(
        std::abs(*result.callDeclinePercent + 33.3333333333333) < 1e-8);
    QVERIFY(!result.currentDeficiency);
    QVERIFY(!result.stressedDeficiency);
}

void ResearchModelsTests::rejectsInvalidMarginAssumptions() {
    const tvchart::MarginRiskInput invalid{
        .longMarketValue = 100'000.0,
        .marginDebit = 50'000.0,
        .maintenanceRate = 1.0,
    };
    QVERIFY(tvchart::validateMarginRiskInput(invalid).has_value());
    QVERIFY_EXCEPTION_THROWN(
        static_cast<void>(tvchart::calculateMarginRisk(invalid)),
        std::invalid_argument);
}

QTEST_APPLESS_MAIN(ResearchModelsTests)

#include "research_models_tests.moc"
