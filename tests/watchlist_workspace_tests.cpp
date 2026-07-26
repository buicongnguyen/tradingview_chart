#include "watchlists/watchlist_workspace.hpp"

#include <QTest>

class WatchlistWorkspaceTests final : public QObject {
    Q_OBJECT

private slots:
    void roundTripsSavedWatchlists();
    void importsAndExportsQuotedCsv();
    void rejectsInvalidAndDuplicateEntries();
    void sortsWithoutMutatingManualOrder();
};

void WatchlistWorkspaceTests::roundTripsSavedWatchlists() {
    auto collection = tvchart::defaultWatchlists();
    collection.lists.push_back({
        .id = QStringLiteral("income"),
        .name = QStringLiteral("Income"),
        .sort = tvchart::WatchlistSort::Symbol,
        .entries = {{
            .symbol = QStringLiteral("KO"),
            .note = QStringLiteral("Quarterly review"),
        }},
    });
    collection.activeListId = QStringLiteral("income");

    const auto loaded =
        tvchart::deserializeWatchlists(tvchart::serializeWatchlists(collection));
    QVERIFY2(loaded.ok(), qPrintable(loaded.error));
    QCOMPARE(loaded.collection, collection);
}

void WatchlistWorkspaceTests::importsAndExportsQuotedCsv() {
    const tvchart::NamedWatchlist watchlist{
        .id = QStringLiteral("test"),
        .name = QStringLiteral("Test"),
        .entries = {
            {
                .symbol = QStringLiteral("AAPL"),
                .note = QStringLiteral("Large, liquid"),
            },
            {
                .symbol = QStringLiteral("BRK-B"),
                .note = QStringLiteral("Says \"B\""),
            },
        },
    };

    const auto imported =
        tvchart::importWatchlistCsv(tvchart::exportWatchlistCsv(watchlist));
    QVERIFY2(imported.ok(), qPrintable(imported.error));
    QVERIFY(imported.rejectedLines.empty());
    QCOMPARE(imported.entries, watchlist.entries);
}

void WatchlistWorkspaceTests::rejectsInvalidAndDuplicateEntries() {
    const tvchart::NamedWatchlist duplicate{
        .id = QStringLiteral("duplicate"),
        .name = QStringLiteral("Duplicate"),
        .entries = {
            {.symbol = QStringLiteral("aapl")},
            {.symbol = QStringLiteral("AAPL")},
        },
    };
    QVERIFY(!tvchart::validateWatchlist(duplicate).isEmpty());

    const auto imported = tvchart::importWatchlistCsv(
        QByteArrayLiteral(
            "symbol,note\n"
            "AAPL,ok\n"
            "bad symbol,rejected\n"
            "AAPL,duplicate\n"));
    QVERIFY(imported.ok());
    QCOMPARE(imported.entries.size(), std::size_t{1});
    QCOMPARE(imported.rejectedLines, std::vector<std::size_t>({3, 4}));
}

void WatchlistWorkspaceTests::sortsWithoutMutatingManualOrder() {
    const tvchart::NamedWatchlist watchlist{
        .id = QStringLiteral("sorted"),
        .name = QStringLiteral("Sorted"),
        .sort = tvchart::WatchlistSort::Symbol,
        .entries = {
            {.symbol = QStringLiteral("TSLA")},
            {.symbol = QStringLiteral("AAPL")},
            {.symbol = QStringLiteral("MSFT")},
        },
    };
    const auto order = tvchart::watchlistDisplayOrder(watchlist);
    QCOMPARE(order, std::vector<std::size_t>({1, 2, 0}));
    QCOMPARE(watchlist.entries.front().symbol, QStringLiteral("TSLA"));
}

QTEST_APPLESS_MAIN(WatchlistWorkspaceTests)

#include "watchlist_workspace_tests.moc"
