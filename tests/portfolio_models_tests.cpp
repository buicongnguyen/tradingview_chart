#include "portfolio/portfolio_models.hpp"
#include "portfolio/portfolio_risk.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTest>

#include <cmath>
#include <ranges>

class PortfolioModelsTests final : public QObject {
    Q_OBJECT

private slots:
    void roundTripsPortfolioWorkspace();
    void migratesSchemaOnePortfolioAndValidatesTargets();
    void reconcilesCashHoldingsAndProfit();
    void rejectsOversellsAndInvalidPersistence();
    void reportsIncompleteValuation();
    void preservesRecordedOrderAtEqualTimestamps();
    void calculatesAlignedPortfolioRisk();
    void calculatesNonExecutingRebalance();
};

namespace {

[[nodiscard]] tvchart::PortfolioTransaction transaction(
    const QString& id,
    const tvchart::PortfolioTransactionType type,
    const std::int64_t timestamp,
    const QString& symbol = {},
    const double quantity = 0.0,
    const double price = 0.0,
    const double amount = 0.0,
    const double fees = 0.0) {
    return {
        .id = id,
        .type = type,
        .symbol = symbol,
        .timestampUtc = timestamp,
        .quantity = quantity,
        .price = price,
        .amount = amount,
        .fees = fees,
        .currency = QStringLiteral("USD"),
    };
}

[[nodiscard]] tvchart::Portfolio samplePortfolio() {
    return {
        .id = QStringLiteral("portfolio"),
        .name = QStringLiteral("Test portfolio"),
        .baseCurrency = QStringLiteral("USD"),
        .transactions = {
            transaction(
                QStringLiteral("deposit"),
                tvchart::PortfolioTransactionType::Deposit,
                1,
                {},
                0.0,
                0.0,
                10'000.0),
            transaction(
                QStringLiteral("buy"),
                tvchart::PortfolioTransactionType::Buy,
                2,
                QStringLiteral("AAPL"),
                10.0,
                100.0,
                0.0,
                5.0),
            transaction(
                QStringLiteral("dividend"),
                tvchart::PortfolioTransactionType::Dividend,
                3,
                QStringLiteral("AAPL"),
                0.0,
                0.0,
                20.0),
            transaction(
                QStringLiteral("sell"),
                tvchart::PortfolioTransactionType::Sell,
                4,
                QStringLiteral("AAPL"),
                4.0,
                120.0,
                0.0,
                5.0),
            transaction(
                QStringLiteral("split"),
                tvchart::PortfolioTransactionType::Split,
                5,
                QStringLiteral("AAPL"),
                2.0),
        },
    };
}

} // namespace

void PortfolioModelsTests::roundTripsPortfolioWorkspace() {
    auto portfolio = samplePortfolio();
    portfolio.targets = {{
        .symbol = QStringLiteral("AAPL"),
        .targetPercent = 60.0,
    }};
    const tvchart::PortfolioWorkspace workspace{
        .portfolios = {portfolio},
    };
    const auto serialized = tvchart::serializePortfolioWorkspace(workspace);
    QVERIFY(!serialized.isEmpty());
    const auto loaded = tvchart::deserializePortfolioWorkspace(serialized);
    QVERIFY2(loaded.ok(), qPrintable(loaded.error));
    QCOMPARE(loaded.workspace, workspace);
}

void PortfolioModelsTests::migratesSchemaOnePortfolioAndValidatesTargets() {
    auto document = QJsonDocument::fromJson(
        tvchart::serializePortfolioWorkspace({
            .portfolios = {samplePortfolio()},
        }));
    auto root = document.object();
    root.insert(QStringLiteral("schemaVersion"), 1);
    auto portfolios =
        root.value(QStringLiteral("portfolios")).toArray();
    auto portfolioObject = portfolios.at(0).toObject();
    portfolioObject.remove(QStringLiteral("targets"));
    portfolios.replace(0, portfolioObject);
    root.insert(QStringLiteral("portfolios"), portfolios);
    const auto migrated = tvchart::deserializePortfolioWorkspace(
        QJsonDocument(root).toJson(QJsonDocument::Compact));
    QVERIFY2(migrated.ok(), qPrintable(migrated.error));
    QVERIFY(migrated.workspace.portfolios.front().targets.empty());

    auto invalid = samplePortfolio();
    invalid.targets = {
        {QStringLiteral("AAPL"), 60.0},
        {QStringLiteral("MSFT"), 50.0},
    };
    QVERIFY(
        tvchart::validatePortfolio(invalid).contains(
            QStringLiteral("100 percent")));
    invalid.targets = {
        {QStringLiteral("AAPL"), 50.0},
        {QStringLiteral("aapl"), 40.0},
    };
    QVERIFY(
        tvchart::validatePortfolio(invalid).contains(
            QStringLiteral("unique")));
}

void PortfolioModelsTests::reconcilesCashHoldingsAndProfit() {
    const QHash<QString, tvchart::PortfolioPrice> prices{
        {
            QStringLiteral("AAPL"),
            {
                .price = 60.0,
                .asOfUtc = 6,
                .currency = QStringLiteral("USD"),
            },
        },
    };
    const auto snapshot =
        tvchart::calculatePortfolioSnapshot(samplePortfolio(), prices);
    QVERIFY2(snapshot.ok(), qPrintable(snapshot.error));
    QVERIFY(snapshot.completeValuation);
    QCOMPARE(snapshot.holdings.size(), std::size_t{1});
    const auto& holding = snapshot.holdings.front();
    QCOMPARE(holding.quantity, 12.0);
    QVERIFY(std::abs(holding.costBasis - 603.0) < 1.0e-9);
    QVERIFY(std::abs(holding.averageCost - 50.25) < 1.0e-9);
    QVERIFY(std::abs(*holding.marketValue - 720.0) < 1.0e-9);
    QVERIFY(std::abs(*holding.unrealizedProfitLoss - 117.0) < 1.0e-9);
    QVERIFY(std::abs(snapshot.cash - 9'490.0) < 1.0e-9);
    QVERIFY(std::abs(snapshot.equity - 10'210.0) < 1.0e-9);
    QVERIFY(std::abs(snapshot.realizedProfitLoss - 73.0) < 1.0e-9);
    QVERIFY(std::abs(snapshot.unrealizedProfitLoss - 117.0) < 1.0e-9);
    QCOMPARE(snapshot.income, 20.0);
    QCOMPARE(snapshot.feesPaid, 10.0);
    QVERIFY(std::abs(snapshot.totalGain - 210.0) < 1.0e-9);
    QCOMPARE(snapshot.largestPositionPercent, 100.0);
    QCOMPARE(snapshot.concentrationIndex, 1.0);
    QCOMPARE(snapshot.effectiveHoldings, std::optional<double>{1.0});
}

void PortfolioModelsTests::rejectsOversellsAndInvalidPersistence() {
    auto portfolio = samplePortfolio();
    portfolio.transactions.insert(
        portfolio.transactions.begin() + 2,
        transaction(
            QStringLiteral("oversell"),
            tvchart::PortfolioTransactionType::Sell,
            3,
            QStringLiteral("AAPL"),
            100.0,
            120.0));
    const auto snapshot = tvchart::calculatePortfolioSnapshot(portfolio, {});
    QVERIFY(!snapshot.ok());
    QVERIFY(snapshot.error.contains(QStringLiteral("exceeds")));

    auto document = QJsonDocument::fromJson(
        tvchart::serializePortfolioWorkspace({
            .portfolios = {samplePortfolio()},
        }));
    auto root = document.object();
    root.insert(QStringLiteral("schemaVersion"), 999);
    const auto loaded = tvchart::deserializePortfolioWorkspace(
        QJsonDocument(root).toJson(QJsonDocument::Compact));
    QVERIFY(!loaded.ok());
}

void PortfolioModelsTests::reportsIncompleteValuation() {
    const auto snapshot =
        tvchart::calculatePortfolioSnapshot(samplePortfolio(), {});
    QVERIFY2(snapshot.ok(), qPrintable(snapshot.error));
    QVERIFY(!snapshot.completeValuation);
    QCOMPARE(snapshot.missingPrices, QStringList{QStringLiteral("AAPL")});
    QVERIFY(!snapshot.holdings.front().marketValue.has_value());
}

void PortfolioModelsTests::preservesRecordedOrderAtEqualTimestamps() {
    auto portfolio = samplePortfolio();
    portfolio.transactions = {
        transaction(
            QStringLiteral("z-buy"),
            tvchart::PortfolioTransactionType::Buy,
            10,
            QStringLiteral("AAPL"),
            1.0,
            100.0),
        transaction(
            QStringLiteral("a-sell"),
            tvchart::PortfolioTransactionType::Sell,
            10,
            QStringLiteral("AAPL"),
            1.0,
            110.0),
    };
    const auto snapshot =
        tvchart::calculatePortfolioSnapshot(portfolio, {});
    QVERIFY2(snapshot.ok(), qPrintable(snapshot.error));
    QCOMPARE(snapshot.holdings.size(), std::size_t{0});
    QCOMPARE(snapshot.realizedProfitLoss, 10.0);
}

void PortfolioModelsTests::calculatesAlignedPortfolioRisk() {
    constexpr auto start = std::int64_t{1'700'000'000};
    tvchart::Bars aapl;
    tvchart::Bars msft;
    tvchart::Bars spy;
    auto aaplPrice = 100.0;
    auto msftPrice = 200.0;
    auto spyPrice = 400.0;
    for (std::int64_t day = 0; day < 100; ++day) {
        const auto aaplReturn =
            0.0008 + 0.012 * std::sin(static_cast<double>(day) * 0.37);
        const auto msftReturn =
            0.0005 + 0.009 * std::cos(static_cast<double>(day) * 0.29);
        const auto spyReturn =
            0.0004 + 0.007 * std::sin(static_cast<double>(day) * 0.31);
        aaplPrice *= 1.0 + aaplReturn;
        msftPrice *= 1.0 + msftReturn;
        spyPrice *= 1.0 + spyReturn;
        const auto timestamp = start + day * 86'400;
        aapl.push_back({
            timestamp,
            aaplPrice,
            aaplPrice * 1.01,
            aaplPrice * 0.99,
            aaplPrice,
            1'000,
        });
        msft.push_back({
            timestamp,
            msftPrice,
            msftPrice * 1.01,
            msftPrice * 0.99,
            msftPrice,
            1'000,
        });
        spy.push_back({
            timestamp,
            spyPrice,
            spyPrice * 1.01,
            spyPrice * 0.99,
            spyPrice,
            1'000,
        });
    }
    tvchart::Portfolio portfolio{
        .id = QStringLiteral("risk"),
        .name = QStringLiteral("Risk fixture"),
        .baseCurrency = QStringLiteral("USD"),
        .transactions = {
            transaction(
                QStringLiteral("deposit"),
                tvchart::PortfolioTransactionType::Deposit,
                start,
                {},
                0,
                0,
                50'000),
            transaction(
                QStringLiteral("buy-aapl"),
                tvchart::PortfolioTransactionType::Buy,
                start + 1,
                QStringLiteral("AAPL"),
                100,
                100),
            transaction(
                QStringLiteral("buy-msft"),
                tvchart::PortfolioTransactionType::Buy,
                start + 2,
                QStringLiteral("MSFT"),
                50,
                200),
        },
    };
    const auto valuationTime = spy.back().timestamp + 86'399;
    const QHash<QString, tvchart::PortfolioPrice> prices{
        {
            QStringLiteral("AAPL"),
            {aapl.back().close, valuationTime, QStringLiteral("USD")},
        },
        {
            QStringLiteral("MSFT"),
            {msft.back().close, valuationTime, QStringLiteral("USD")},
        },
    };
    const auto snapshot =
        tvchart::calculatePortfolioSnapshot(portfolio, prices);
    QVERIFY2(snapshot.ok(), qPrintable(snapshot.error));
    QVERIFY(snapshot.completeValuation);
    QCOMPARE(snapshot.valuationTimestampUtc, valuationTime);
    const auto risk = tvchart::calculatePortfolioRisk(
        portfolio,
        snapshot,
        {
            {QStringLiteral("AAPL"), aapl},
            {QStringLiteral("MSFT"), msft},
        },
        QStringLiteral("SPY"),
        spy);
    QVERIFY2(risk.ok(), qPrintable(risk.error));
    QVERIFY(risk.observations >= 90);
    QVERIFY(risk.annualizedReturnPercent.has_value());
    QVERIFY(risk.annualizedVolatilityPercent.has_value());
    QVERIFY(risk.sharpeRatio.has_value());
    QVERIFY(risk.beta.has_value());
    QVERIFY(risk.annualizedAlphaPercent.has_value());
    QVERIFY(risk.moneyWeightedReturnPercent.has_value());
    QVERIFY(risk.dailyCloseTimeWeightedReturnPercent.has_value());
    QVERIFY(risk.dailyCloseTimeWeightedReturnIsApproximate);
    QVERIFY(
        *risk.historicalConditionalValueAtRisk95Percent >=
        *risk.historicalValueAtRisk95Percent);
    QCOMPARE(risk.correlations.size(), std::size_t{3});
    auto contributionTotal = 0.0;
    for (const auto& contribution : risk.riskContributions) {
        contributionTotal += contribution.contributionPercent;
    }
    QVERIFY(std::abs(contributionTotal - 100.0) < 1.0e-8);

    auto stalePortfolio = portfolio;
    stalePortfolio.transactions.push_back(
        transaction(
            QStringLiteral("late-withdrawal"),
            tvchart::PortfolioTransactionType::Withdrawal,
            valuationTime + 1,
            {},
            0,
            0,
            100));
    const auto staleSnapshot =
        tvchart::calculatePortfolioSnapshot(stalePortfolio, prices);
    QVERIFY2(staleSnapshot.ok(), qPrintable(staleSnapshot.error));
    QCOMPARE(staleSnapshot.valuationTimestampUtc, valuationTime);
    const auto staleRisk = tvchart::calculatePortfolioRisk(
        stalePortfolio,
        staleSnapshot,
        {
            {QStringLiteral("AAPL"), aapl},
            {QStringLiteral("MSFT"), msft},
        },
        QStringLiteral("SPY"),
        spy);
    QVERIFY2(staleRisk.ok(), qPrintable(staleRisk.error));
    QVERIFY(!staleRisk.moneyWeightedReturnPercent.has_value());

    const auto incomplete = tvchart::calculatePortfolioRisk(
        portfolio,
        snapshot,
        {{QStringLiteral("AAPL"), aapl}},
        QStringLiteral("SPY"),
        spy);
    QVERIFY(!incomplete.ok());
    QVERIFY(!incomplete.missingHistory.isEmpty());
}

void PortfolioModelsTests::calculatesNonExecutingRebalance() {
    auto portfolio = samplePortfolio();
    portfolio.targets = {
        {QStringLiteral("AAPL"), 25.0},
        {QStringLiteral("MSFT"), 25.0},
    };
    const QHash<QString, tvchart::PortfolioPrice> prices{{
        QStringLiteral("AAPL"),
        {
            .price = 60.0,
            .asOfUtc = 6,
            .currency = QStringLiteral("USD"),
        },
    }};
    const auto snapshot =
        tvchart::calculatePortfolioSnapshot(portfolio, prices);
    const auto report =
        tvchart::calculateRebalance(portfolio, snapshot);
    QVERIFY2(report.ok(), qPrintable(report.error));
    QCOMPARE(report.targetCashPercent, 50.0);
    QCOMPARE(report.suggestions.size(), std::size_t{2});
    const auto aapl = std::ranges::find(
        report.suggestions,
        QStringLiteral("AAPL"),
        &tvchart::RebalanceSuggestion::symbol);
    QVERIFY(aapl != report.suggestions.end());
    QVERIFY(aapl->approximateShares.has_value());
    const auto msft = std::ranges::find(
        report.suggestions,
        QStringLiteral("MSFT"),
        &tvchart::RebalanceSuggestion::symbol);
    QVERIFY(msft != report.suggestions.end());
    QVERIFY(!msft->approximateShares.has_value());
    QCOMPARE(report.missingPrices, QStringList{QStringLiteral("MSFT")});
    auto allPrices = prices;
    allPrices.insert(
        QStringLiteral("MSFT"),
        {
            .price = 200.0,
            .asOfUtc = 6,
            .currency = QStringLiteral("USD"),
        });
    const auto priced =
        tvchart::calculateRebalance(
            portfolio,
            snapshot,
            allPrices);
    QVERIFY(priced.missingPrices.isEmpty());
    const auto pricedMsft = std::ranges::find(
        priced.suggestions,
        QStringLiteral("MSFT"),
        &tvchart::RebalanceSuggestion::symbol);
    QVERIFY(pricedMsft->approximateShares.has_value());
    QCOMPARE(portfolio.targets.size(), std::size_t{2});
}

QTEST_APPLESS_MAIN(PortfolioModelsTests)

#include "portfolio_models_tests.moc"
