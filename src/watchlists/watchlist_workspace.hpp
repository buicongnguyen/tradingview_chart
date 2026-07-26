#pragma once

#include <QByteArray>
#include <QString>

#include <cstddef>
#include <vector>

namespace tvchart {

enum class WatchlistSort {
    Manual,
    Symbol,
};

struct WatchlistEntry {
    QString symbol;
    QString note;

    [[nodiscard]] bool operator==(const WatchlistEntry&) const = default;
};

struct NamedWatchlist {
    QString id;
    QString name;
    WatchlistSort sort{WatchlistSort::Manual};
    std::vector<WatchlistEntry> entries;

    [[nodiscard]] bool operator==(const NamedWatchlist&) const = default;
};

struct WatchlistCollection {
    std::vector<NamedWatchlist> lists;
    QString activeListId;

    [[nodiscard]] bool operator==(const WatchlistCollection&) const = default;
};

struct WatchlistLoadResult {
    WatchlistCollection collection;
    QString error;

    [[nodiscard]] bool ok() const noexcept { return error.isEmpty(); }
};

struct WatchlistCsvResult {
    std::vector<WatchlistEntry> entries;
    std::vector<std::size_t> rejectedLines;
    QString error;

    [[nodiscard]] bool ok() const noexcept { return error.isEmpty(); }
};

[[nodiscard]] QString normalizeWatchlistSymbol(QString symbol);
[[nodiscard]] QString validateWatchlist(const NamedWatchlist& watchlist);
[[nodiscard]] WatchlistCollection defaultWatchlists();
[[nodiscard]] QByteArray serializeWatchlists(
    const WatchlistCollection& collection);
[[nodiscard]] WatchlistLoadResult deserializeWatchlists(
    const QByteArray& json);
[[nodiscard]] QByteArray exportWatchlistCsv(const NamedWatchlist& watchlist);
[[nodiscard]] WatchlistCsvResult importWatchlistCsv(const QByteArray& csv);
[[nodiscard]] std::vector<std::size_t> watchlistDisplayOrder(
    const NamedWatchlist& watchlist);

} // namespace tvchart
