#include "fundamentals/event_impact.hpp"
#include "fundamentals/fundamental_analysis.hpp"
#include "fundamentals/fundamental_screener.hpp"
#include "fundamentals/fundamental_store.hpp"
#include "fundamentals/sec_companyfacts_parser.hpp"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTest>
#include <QTimeZone>

#include <algorithm>
#include <cmath>
#include <iterator>

class FundamentalAnalysisTests final : public QObject {
    Q_OBJECT

private slots:
    void parsesAndPersistsSecCompanyFacts();
    void enforcesPointInTimeAndDerivesQuarters();
    void calculatesTransparentValuationAndScreen();
    void alignsEventImpactToTradingDate();
};

namespace {

[[nodiscard]] QJsonObject durationFact(
    const char* start,
    const char* end,
    const char* filed,
    const char* form,
    const int fiscalYear,
    const char* fiscalPeriod,
    const char* accession,
    const double value) {
    return {
        {QStringLiteral("start"), QString::fromLatin1(start)},
        {QStringLiteral("end"), QString::fromLatin1(end)},
        {QStringLiteral("filed"), QString::fromLatin1(filed)},
        {QStringLiteral("form"), QString::fromLatin1(form)},
        {QStringLiteral("fy"), fiscalYear},
        {QStringLiteral("fp"), QString::fromLatin1(fiscalPeriod)},
        {QStringLiteral("accn"), QString::fromLatin1(accession)},
        {QStringLiteral("val"), value},
    };
}

[[nodiscard]] QJsonObject instantFact(
    const char* end,
    const char* filed,
    const int fiscalYear,
    const char* fiscalPeriod,
    const char* accession,
    const double value) {
    return {
        {QStringLiteral("end"), QString::fromLatin1(end)},
        {QStringLiteral("filed"), QString::fromLatin1(filed)},
        {QStringLiteral("form"), QStringLiteral("10-K")},
        {QStringLiteral("fy"), fiscalYear},
        {QStringLiteral("fp"), QString::fromLatin1(fiscalPeriod)},
        {QStringLiteral("accn"), QString::fromLatin1(accession)},
        {QStringLiteral("val"), value},
    };
}

void addConcept(
    QJsonObject& concepts,
    const QString& tag,
    const QString& unit,
    const QJsonArray& facts) {
    concepts.insert(
        tag,
        QJsonObject{{
            QStringLiteral("units"),
            QJsonObject{{unit, facts}},
        }});
}

[[nodiscard]] QByteArray companyFactsFixture() {
    const QJsonArray revenue{
        durationFact(
            "2023-01-01", "2023-09-30", "2023-11-01",
            "10-Q", 2023, "Q3", "0000320193-23-000100", 35),
        durationFact(
            "2023-01-01", "2023-12-31", "2024-02-01",
            "10-K", 2023, "FY", "0000320193-24-000001", 50),
        durationFact(
            "2024-01-01", "2024-03-31", "2024-05-01",
            "10-Q", 2024, "Q1", "0000320193-24-000010", 10),
        durationFact(
            "2024-01-01", "2024-06-30", "2024-08-01",
            "10-Q", 2024, "Q2", "0000320193-24-000020", 25),
        durationFact(
            "2024-01-01", "2024-09-30", "2024-11-01",
            "10-Q", 2024, "Q3", "0000320193-24-000030", 45),
        durationFact(
            "2024-01-01", "2024-12-31", "2025-02-01",
            "10-K", 2024, "FY", "0000320193-25-000001", 70),
        durationFact(
            "2025-01-01", "2025-03-31", "2026-05-01",
            "10-Q", 2025, "Q1", "0000320193-26-000010", 999),
    };
    const auto scaled =
        [&](const double scale) {
            QJsonArray values;
            for (const auto& value : revenue) {
                auto fact = value.toObject();
                fact.insert(
                    QStringLiteral("val"),
                    fact.value(QStringLiteral("val")).toDouble() *
                        scale);
                values.append(fact);
            }
            return values;
        };
    QJsonObject concepts;
    addConcept(
        concepts,
        QStringLiteral(
            "RevenueFromContractWithCustomerExcludingAssessedTax"),
        QStringLiteral("USD"),
        revenue);
    addConcept(
        concepts,
        QStringLiteral("GrossProfit"),
        QStringLiteral("USD"),
        scaled(0.5));
    addConcept(
        concepts,
        QStringLiteral("OperatingIncomeLoss"),
        QStringLiteral("USD"),
        scaled(0.25));
    addConcept(
        concepts,
        QStringLiteral("NetIncomeLoss"),
        QStringLiteral("USD"),
        scaled(0.2));
    addConcept(
        concepts,
        QStringLiteral("NetCashProvidedByUsedInOperatingActivities"),
        QStringLiteral("USD"),
        scaled(0.5));
    addConcept(
        concepts,
        QStringLiteral("PaymentsToAcquirePropertyPlantAndEquipment"),
        QStringLiteral("USD"),
        scaled(1.0 / 7.0));

    QJsonArray eps;
    for (const auto fiscalYear : {2023, 2024}) {
        const auto year = QByteArray::number(fiscalYear);
        const auto accessionYear =
            QByteArray::number(fiscalYear % 100).rightJustified(2, '0');
        eps.append(durationFact(
            (year + "-01-01").constData(),
            (year + "-03-31").constData(),
            (year + "-05-01").constData(),
            "10-Q",
            fiscalYear,
            "Q1",
            ("0000320193-" + accessionYear + "-000010").constData(),
            0.10));
        eps.append(durationFact(
            (year + "-04-01").constData(),
            (year + "-06-30").constData(),
            (year + "-08-01").constData(),
            "10-Q",
            fiscalYear,
            "Q2",
            ("0000320193-" + accessionYear + "-000020").constData(),
            fiscalYear == 2023 ? 0.15 : 0.20));
        eps.append(durationFact(
            (year + "-07-01").constData(),
            (year + "-09-30").constData(),
            (year + "-11-01").constData(),
            "10-Q",
            fiscalYear,
            "Q3",
            ("0000320193-" + accessionYear + "-000030").constData(),
            fiscalYear == 2023 ? 0.15 : 0.25));
        eps.append(durationFact(
            (year + "-01-01").constData(),
            (year + "-12-31").constData(),
            ((year.toInt() + 1 == 2024
                  ? QByteArray("2024")
                  : QByteArray("2025")) +
             "-02-01")
                .constData(),
            "10-K",
            fiscalYear,
            "FY",
            ("0000320193-" +
             QByteArray::number((fiscalYear + 1) % 100)
                 .rightJustified(2, '0') +
             "-000001")
                .constData(),
            fiscalYear == 2023 ? 0.55 : 0.90));
    }
    addConcept(
        concepts,
        QStringLiteral("EarningsPerShareDiluted"),
        QStringLiteral("USD/shares"),
        eps);
    addConcept(
        concepts,
        QStringLiteral(
            "WeightedAverageNumberOfDilutedSharesOutstanding"),
        QStringLiteral("shares"),
        QJsonArray{
            durationFact(
                "2024-01-01", "2024-12-31", "2025-02-01",
                "10-K", 2024, "FY", "0000320193-25-000001", 100),
        });
    const QJsonArray instant{
        instantFact(
            "2024-12-31", "2025-02-01", 2024, "FY",
            "0000320193-25-000001", 20),
    };
    addConcept(
        concepts,
        QStringLiteral("CashAndCashEquivalentsAtCarryingValue"),
        QStringLiteral("USD"),
        instant);
    auto debt = instant;
    debt[0] = instantFact(
        "2024-12-31", "2025-02-01", 2024, "FY",
        "0000320193-25-000001", 10);
    addConcept(
        concepts,
        QStringLiteral("LongTermDebt"),
        QStringLiteral("USD"),
        debt);
    auto equity = instant;
    equity[0] = instantFact(
        "2024-12-31", "2025-02-01", 2024, "FY",
        "0000320193-25-000001", 50);
    addConcept(
        concepts,
        QStringLiteral("StockholdersEquity"),
        QStringLiteral("USD"),
        equity);
    auto assets = instant;
    assets[0] = instantFact(
        "2024-12-31", "2025-02-01", 2024, "FY",
        "0000320193-25-000001", 100);
    addConcept(
        concepts,
        QStringLiteral("Assets"),
        QStringLiteral("USD"),
        assets);
    auto liabilities = instant;
    liabilities[0] = instantFact(
        "2024-12-31", "2025-02-01", 2024, "FY",
        "0000320193-25-000001", 50);
    addConcept(
        concepts,
        QStringLiteral("Liabilities"),
        QStringLiteral("USD"),
        liabilities);

    return QJsonDocument(QJsonObject{
        {QStringLiteral("cik"), 320193},
        {QStringLiteral("entityName"), QStringLiteral("Example Corp")},
        {
            QStringLiteral("facts"),
            QJsonObject{{
                QStringLiteral("us-gaap"),
                concepts,
            }},
        },
    }).toJson(QJsonDocument::Compact);
}

[[nodiscard]] tvchart::FundamentalCompany parsedCompany() {
    const auto result = tvchart::SecCompanyFactsParser::parse(
        companyFactsFixture(),
        QStringLiteral("AAPL"),
        QStringLiteral("0000320193"),
        1'750'000'000);
    return result.company;
}

[[nodiscard]] tvchart::Bars dailyBars(
    QDate first,
    const int count,
    const double startPrice,
    const double dailyReturn) {
    tvchart::Bars result;
    auto price = startPrice;
    while (static_cast<int>(result.size()) < count) {
        if (first.dayOfWeek() <= 5) {
            const auto timestamp =
                QDateTime(
                    first,
                    QTime(20, 0),
                    QTimeZone::UTC)
                    .toSecsSinceEpoch();
            result.push_back({
                timestamp,
                price,
                price * 1.01,
                price * 0.99,
                price * (1.0 + dailyReturn),
                1'000.0 +
                    static_cast<double>(result.size()) * 10.0,
            });
            price *= 1.0 + dailyReturn;
        }
        first = first.addDays(1);
    }
    return result;
}

} // namespace

void FundamentalAnalysisTests::parsesAndPersistsSecCompanyFacts() {
    const auto parsed = tvchart::SecCompanyFactsParser::parse(
        companyFactsFixture(),
        QStringLiteral("AAPL"),
        QStringLiteral("320193"),
        1'750'000'000);
    QVERIFY2(parsed.ok(), qPrintable(parsed.error));
    QCOMPARE(parsed.company.symbol, QStringLiteral("AAPL"));
    QCOMPARE(parsed.company.cik, QStringLiteral("0000320193"));
    QVERIFY(parsed.company.facts.size() > 30);
    for (const auto& fact : parsed.company.facts) {
        QVERIFY2(
            tvchart::validateFundamentalFact(fact).isEmpty(),
            qPrintable(tvchart::validateFundamentalFact(fact)));
    }
    auto nonCanonical = parsed.company.facts.front();
    nonCanonical.symbol = QStringLiteral("aapl");
    QVERIFY(!tvchart::validateFundamentalFact(nonCanonical).isEmpty());

    tvchart::FundamentalStore store(QStringLiteral(":memory:"));
    QVERIFY2(store.open(), qPrintable(store.lastError()));
    const auto upsertError = store.upsertCompany(parsed.company);
    QVERIFY2(upsertError.isEmpty(), qPrintable(upsertError));
    const auto loaded = store.loadCompany(QStringLiteral("aapl"));
    QVERIFY2(loaded.ok(), qPrintable(loaded.error));
    QCOMPARE(loaded.company, parsed.company);
    const auto summaries = store.availableCompanies();
    QCOMPARE(summaries.size(), std::size_t{1});
    QCOMPARE(summaries.front().symbol, QStringLiteral("AAPL"));

    auto replacement = parsed.company;
    replacement.cik = QStringLiteral("0000000001");
    replacement.name = QStringLiteral("Replacement Corp");
    replacement.facts.resize(1);
    replacement.facts.front().cik = replacement.cik;
    replacement.facts.front().sourceUrl.replace(
        QStringLiteral("/data/320193/"),
        QStringLiteral("/data/1/"));
    QVERIFY2(
        store.upsertCompany(replacement).isEmpty(),
        qPrintable(store.lastError()));
    const auto replaced = store.loadCompany(QStringLiteral("AAPL"));
    QVERIFY2(replaced.ok(), qPrintable(replaced.error));
    QCOMPARE(replaced.company.cik, replacement.cik);
    QCOMPARE(replaced.company.facts.size(), std::size_t{1});
}

void FundamentalAnalysisTests::enforcesPointInTimeAndDerivesQuarters() {
    const auto company = parsedCompany();
    const auto beforeAnnual = tvchart::fundamentalSeries(
        company.facts,
        tvchart::FundamentalMetric::Revenue,
        tvchart::FundamentalPeriodMode::Annual,
        QDate(2025, 1, 15));
    QVERIFY(!beforeAnnual.empty());
    QCOMPARE(beforeAnnual.back().periodEnd, QDate(2023, 12, 31));

    const auto quarters = tvchart::fundamentalSeries(
        company.facts,
        tvchart::FundamentalMetric::Revenue,
        tvchart::FundamentalPeriodMode::Quarterly,
        QDate(2025, 3, 1));
    QCOMPARE(quarters.size(), std::size_t{5});
    QCOMPARE(quarters[1].value, 10.0);
    QCOMPARE(quarters[2].value, 15.0);
    QCOMPARE(quarters[3].value, 20.0);
    QCOMPARE(quarters[4].value, 25.0);
    QVERIFY(quarters[4].derived);

    const auto ttm = tvchart::fundamentalSeries(
        company.facts,
        tvchart::FundamentalMetric::Revenue,
        tvchart::FundamentalPeriodMode::TrailingTwelveMonths,
        QDate(2025, 3, 1));
    QVERIFY(!ttm.empty());
    QCOMPARE(ttm.back().value, 70.0);
    auto missingQuarterFacts = company.facts;
    std::erase_if(
        missingQuarterFacts,
        [](const tvchart::FundamentalFact& fact) {
            return fact.metric ==
                       tvchart::FundamentalMetric::Revenue &&
                   fact.periodEnd == QDate(2024, 6, 30);
        });
    const auto nonConsecutiveTtm =
        tvchart::fundamentalSeries(
            missingQuarterFacts,
            tvchart::FundamentalMetric::Revenue,
            tvchart::FundamentalPeriodMode::TrailingTwelveMonths,
            QDate(2025, 3, 1));
    QVERIFY(nonConsecutiveTtm.empty());
    const auto noLookahead = tvchart::fundamentalSeries(
        company.facts,
        tvchart::FundamentalMetric::Revenue,
        tvchart::FundamentalPeriodMode::Quarterly,
        QDate(2025, 12, 31));
    QCOMPARE(noLookahead.back().value, 25.0);
}

void FundamentalAnalysisTests::calculatesTransparentValuationAndScreen() {
    const auto company = parsedCompany();
    const auto snapshot = tvchart::buildFundamentalSnapshot(
        company,
        QDate(2025, 3, 1),
        2.0);
    QVERIFY2(snapshot.ok(), qPrintable(snapshot.error));
    QVERIFY(snapshot.derived.freeCashFlow.has_value());
    QCOMPARE(*snapshot.derived.freeCashFlow, 25.0);
    QCOMPARE(*snapshot.derived.marketCapitalization, 200.0);
    QVERIFY(snapshot.derived.revenueGrowthYoYPercent.has_value());
    QVERIFY(*snapshot.derived.revenueGrowthYoYPercent > 60.0);

    const auto dcf = tvchart::calculateDcf(
        snapshot,
        {
            .annualGrowthPercent = 5.0,
            .discountRatePercent = 10.0,
            .terminalGrowthPercent = 2.5,
            .forecastYears = 5,
        },
        2.0);
    QVERIFY2(dcf.ok(), qPrintable(dcf.error));
    QVERIFY(dcf.valuePerShare > 0.0);
    QVERIFY(dcf.impliedAnnualGrowthPercent.has_value());

    const tvchart::FundamentalScreenDefinition definition{
        .name = QStringLiteral("Quality growth"),
        .conditions = {{
            .field =
                tvchart::FundamentalScreenField::RevenueGrowthYoY,
            .comparison =
                tvchart::FundamentalScreenOperator::GreaterThanOrEqual,
            .threshold = 10.0,
        }},
        .sortField =
            tvchart::FundamentalScreenField::RevenueGrowthYoY,
    };
    const auto payload =
        tvchart::serializeFundamentalScreen(definition);
    QVERIFY(!payload.isEmpty());
    const auto loaded =
        tvchart::deserializeFundamentalScreen(payload);
    QVERIFY2(loaded.ok(), qPrintable(loaded.error));
    QCOMPARE(loaded.definition, definition);

    const auto screen = tvchart::runFundamentalScreen(
        definition,
        {{
            .company = company,
            .dailyBars =
                dailyBars(
                    QDate(2024, 9, 1),
                    100,
                    1.0,
                    0.001),
            .priceCurrency = QStringLiteral("USD"),
        }},
        QDate(2025, 3, 1));
    QVERIFY2(screen.ok(), qPrintable(screen.error));
    QCOMPARE(screen.rows.size(), std::size_t{1});
    QVERIFY(screen.rows.front().matched);
    QVERIFY(
        screen.rows.front()
            .values.at(tvchart::FundamentalScreenField::Rsi14)
            .has_value());

    auto barsWithFutureData =
        dailyBars(
            QDate(2024, 9, 1),
            180,
            1.0,
            0.001);
    const auto expectedAsOfPrice =
        std::ranges::find_if(
            barsWithFutureData,
            [](const tvchart::Bar& bar) {
                return QDateTime::fromSecsSinceEpoch(
                           bar.timestamp,
                           QTimeZone::UTC)
                           .date() >
                       QDate(2025, 3, 1);
            });
    QVERIFY(expectedAsOfPrice != barsWithFutureData.begin());
    const auto expectedBar = *std::prev(expectedAsOfPrice);
    barsWithFutureData.back().close = 1'000'000.0;
    barsWithFutureData.back().high = 1'000'000.0;
    const auto pointInTimeScreen = tvchart::runFundamentalScreen(
        definition,
        {{
            .company = company,
            .dailyBars = barsWithFutureData,
            .priceCurrency = QStringLiteral("USD"),
        }},
        QDate(2025, 3, 1));
    QVERIFY2(
        pointInTimeScreen.ok(),
        qPrintable(pointInTimeScreen.error));
    QCOMPARE(
        *pointInTimeScreen.rows.front()
             .values.at(tvchart::FundamentalScreenField::Price),
        expectedBar.close);
    QVERIFY(
        pointInTimeScreen.rows.front().priceDate <=
        QDate(2025, 3, 1));

    const tvchart::FundamentalScreenDefinition valuationScreen{
        .name = QStringLiteral("Currency-safe valuation"),
        .conditions = {{
            .field =
                tvchart::FundamentalScreenField::PriceToEarnings,
            .comparison =
                tvchart::FundamentalScreenOperator::LessThanOrEqual,
            .threshold = 100.0,
        }},
        .sortField =
            tvchart::FundamentalScreenField::PriceToEarnings,
    };
    const auto currencyMismatch =
        tvchart::runFundamentalScreen(
            valuationScreen,
            {{
                .company = company,
                .dailyBars = barsWithFutureData,
                .priceCurrency = QStringLiteral("EUR"),
            }},
            QDate(2025, 3, 1));
    QVERIFY2(
        currencyMismatch.ok(),
        qPrintable(currencyMismatch.error));
    QVERIFY(!currencyMismatch.rows.front().matched);
    QVERIFY(
        !currencyMismatch.rows.front()
             .values.at(
                 tvchart::FundamentalScreenField::PriceToEarnings)
             .has_value());

    const auto unavailableFutureEvent =
        tvchart::runFundamentalScreen(
            {
                .name = QStringLiteral("Known earnings only"),
                .conditions = {{
                    .field =
                        tvchart::FundamentalScreenField::DaysToEarnings,
                    .comparison =
                        tvchart::FundamentalScreenOperator::LessThanOrEqual,
                    .threshold = 100.0,
                }},
                .sortField =
                    tvchart::FundamentalScreenField::DaysToEarnings,
            },
            {{
                .company = company,
                .dailyBars = barsWithFutureData,
                .priceCurrency = QStringLiteral("USD"),
                .events = {{
                    .id = QStringLiteral("future-knowledge"),
                    .symbol = QStringLiteral("AAPL"),
                    .type = tvchart::ResearchEventType::Earnings,
                    .scheduledDate = QDate(2025, 3, 15),
                    .title = QStringLiteral("Earnings"),
                    .source = QStringLiteral("Test"),
                    .asOfUtc =
                        QDateTime(
                            QDate(2025, 4, 1),
                            QTime(0, 0),
                            QTimeZone::UTC)
                            .toSecsSinceEpoch(),
                }},
            }},
            QDate(2025, 3, 1));
    QVERIFY2(
        unavailableFutureEvent.ok(),
        qPrintable(unavailableFutureEvent.error));
    QVERIFY(!unavailableFutureEvent.rows.front().matched);
}

void FundamentalAnalysisTests::alignsEventImpactToTradingDate() {
    const auto security =
        dailyBars(QDate(2025, 1, 1), 50, 100.0, 0.002);
    const auto benchmark =
        dailyBars(QDate(2025, 1, 1), 50, 400.0, 0.001);
    const QDate saturday(2025, 2, 1);
    const auto report = tvchart::calculateEventImpact(
        security,
        benchmark,
        saturday);
    QVERIFY2(report.ok(), qPrintable(report.error));
    QCOMPARE(report.alignedTradingDate, QDate(2025, 2, 3));
    QVERIFY(report.openingGapPercent.has_value());
    QVERIFY(report.eventDayReturnPercent.has_value());
    QVERIFY(report.eventVolumeRatio20.has_value());
    QVERIFY(!report.windows.empty());
    QVERIFY(report.windows.front().abnormalReturnPercent.has_value());

    const QDate weekday(2025, 1, 9);
    const auto sameSession = tvchart::calculateEventImpact(
        security,
        benchmark,
        weekday);
    const auto nextSession = tvchart::calculateEventImpact(
        security,
        benchmark,
        weekday,
        true);
    QVERIFY2(sameSession.ok(), qPrintable(sameSession.error));
    QVERIFY2(nextSession.ok(), qPrintable(nextSession.error));
    QCOMPARE(sameSession.alignedTradingDate, weekday);
    QCOMPARE(nextSession.alignedTradingDate, QDate(2025, 1, 10));
}

QTEST_GUILESS_MAIN(FundamentalAnalysisTests)

#include "fundamental_analysis_tests.moc"
