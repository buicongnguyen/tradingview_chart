#include "watchlists/watchlist_workspace.hpp"

#include "util/csv_security.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QRegularExpression>

#include <algorithm>
#include <optional>
#include <ranges>

namespace tvchart {
namespace {

constexpr auto kSchemaVersion = 1;
constexpr auto kMaximumLists = std::size_t{32};
constexpr auto kMaximumEntries = std::size_t{500};
constexpr auto kMaximumNoteLength = qsizetype{512};

[[nodiscard]] QString csvField(QString value) {
    value = protectSpreadsheetCsvText(std::move(value));
    value.replace(QStringLiteral("\""), QStringLiteral("\"\""));
    return QStringLiteral("\"%1\"").arg(value);
}

[[nodiscard]] std::vector<QString> parseCsvRow(
    const QString& row,
    bool& valid) {
    std::vector<QString> fields;
    QString field;
    bool quoted = false;
    bool closedQuote = false;
    valid = true;

    for (qsizetype index = 0; index < row.size(); ++index) {
        const auto character = row.at(index);
        if (quoted) {
            if (character == u'"') {
                if (index + 1 < row.size() && row.at(index + 1) == u'"') {
                    field += u'"';
                    ++index;
                } else {
                    quoted = false;
                    closedQuote = true;
                }
            } else {
                field += character;
            }
            continue;
        }

        if (closedQuote && character != u',') {
            valid = false;
            return {};
        }
        if (character == u',') {
            fields.push_back(field);
            field.clear();
            closedQuote = false;
        } else if (character == u'"' && field.isEmpty()) {
            quoted = true;
        } else {
            field += character;
        }
    }
    if (quoted) {
        valid = false;
        return {};
    }
    fields.push_back(field);
    return fields;
}

[[nodiscard]] QString sortId(const WatchlistSort sort) {
    return sort == WatchlistSort::Symbol
               ? QStringLiteral("symbol")
               : QStringLiteral("manual");
}

[[nodiscard]] WatchlistSort parseSort(const QString& value, bool& valid) {
    if (value == QStringLiteral("manual")) {
        return WatchlistSort::Manual;
    }
    if (value == QStringLiteral("symbol")) {
        return WatchlistSort::Symbol;
    }
    valid = false;
    return WatchlistSort::Manual;
}

} // namespace

QString normalizeWatchlistSymbol(QString symbol) {
    return symbol.trimmed().toUpper();
}

QString validateWatchlist(const NamedWatchlist& watchlist) {
    if (watchlist.id.trimmed().isEmpty() || watchlist.id.size() > 128) {
        return QStringLiteral("Watchlist identity is missing or too long.");
    }
    if (watchlist.name.trimmed().isEmpty() || watchlist.name.size() > 64) {
        return QStringLiteral("Watchlist name is missing or too long.");
    }
    if (watchlist.entries.size() > kMaximumEntries) {
        return QStringLiteral("A watchlist can contain at most 500 symbols.");
    }

    static const QRegularExpression symbolPattern(
        QStringLiteral("^[A-Z0-9.^][A-Z0-9.^=_/-]{0,31}$"));
    std::vector<QString> identities;
    identities.reserve(watchlist.entries.size());
    for (const auto& entry : watchlist.entries) {
        const auto symbol = normalizeWatchlistSymbol(entry.symbol);
        if (!symbolPattern.match(symbol).hasMatch()) {
            return QStringLiteral("Invalid watchlist symbol: %1").arg(entry.symbol);
        }
        if (entry.note.size() > kMaximumNoteLength ||
            entry.note.contains(u'\n') ||
            entry.note.contains(u'\r')) {
            return QStringLiteral("Notes must be one line and at most 512 characters.");
        }
        if (std::ranges::find(identities, symbol) != identities.end()) {
            return QStringLiteral("Duplicate watchlist symbol: %1").arg(symbol);
        }
        identities.push_back(symbol);
    }
    return {};
}

WatchlistCollection defaultWatchlists() {
    return {
        .lists = {{
            .id = QStringLiteral("default"),
            .name = QStringLiteral("Main"),
            .sort = WatchlistSort::Manual,
            .entries = {
                {.symbol = QStringLiteral("AAPL")},
                {.symbol = QStringLiteral("MSFT")},
                {.symbol = QStringLiteral("NVDA")},
                {.symbol = QStringLiteral("TSLA")},
                {.symbol = QStringLiteral("SPY")},
                {.symbol = QStringLiteral("BTCUSD")},
            },
        }},
        .activeListId = QStringLiteral("default"),
    };
}

QByteArray serializeWatchlists(const WatchlistCollection& collection) {
    QJsonArray lists;
    for (const auto& watchlist : collection.lists) {
        QJsonArray entries;
        for (const auto& entry : watchlist.entries) {
            entries.append(QJsonObject{
                {QStringLiteral("symbol"), normalizeWatchlistSymbol(entry.symbol)},
                {QStringLiteral("note"), entry.note},
            });
        }
        lists.append(QJsonObject{
            {QStringLiteral("id"), watchlist.id},
            {QStringLiteral("name"), watchlist.name},
            {QStringLiteral("sort"), sortId(watchlist.sort)},
            {QStringLiteral("entries"), entries},
        });
    }

    return QJsonDocument(QJsonObject{
                             {QStringLiteral("schemaVersion"), kSchemaVersion},
                             {QStringLiteral("activeListId"), collection.activeListId},
                             {QStringLiteral("lists"), lists},
                         })
        .toJson(QJsonDocument::Compact);
}

WatchlistLoadResult deserializeWatchlists(const QByteArray& json) {
    QJsonParseError parseError;
    const auto document = QJsonDocument::fromJson(json, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        return {.error = QStringLiteral("Saved watchlists contain invalid JSON.")};
    }

    const auto root = document.object();
    if (root.value(QStringLiteral("schemaVersion")).toInt() != kSchemaVersion) {
        return {.error = QStringLiteral("Saved watchlist schema is unsupported.")};
    }
    const auto listValues = root.value(QStringLiteral("lists")).toArray();
    if (listValues.isEmpty() ||
        static_cast<std::size_t>(listValues.size()) > kMaximumLists) {
        return {.error = QStringLiteral("Saved watchlist count is invalid.")};
    }

    WatchlistCollection collection{
        .activeListId = root.value(QStringLiteral("activeListId")).toString(),
    };
    collection.lists.reserve(static_cast<std::size_t>(listValues.size()));
    std::vector<QString> identities;
    for (const auto& listValue : listValues) {
        const auto object = listValue.toObject();
        bool validSort = true;
        NamedWatchlist watchlist{
            .id = object.value(QStringLiteral("id")).toString(),
            .name = object.value(QStringLiteral("name")).toString(),
            .sort = parseSort(
                object.value(QStringLiteral("sort")).toString(),
                validSort),
        };
        if (!validSort ||
            std::ranges::find(identities, watchlist.id) != identities.end()) {
            return {.error = QStringLiteral("Saved watchlist identity or sort is invalid.")};
        }
        identities.push_back(watchlist.id);

        const auto entries = object.value(QStringLiteral("entries")).toArray();
        watchlist.entries.reserve(static_cast<std::size_t>(entries.size()));
        for (const auto& entryValue : entries) {
            const auto entry = entryValue.toObject();
            watchlist.entries.push_back({
                .symbol = normalizeWatchlistSymbol(
                    entry.value(QStringLiteral("symbol")).toString()),
                .note = entry.value(QStringLiteral("note")).toString(),
            });
        }
        if (const auto error = validateWatchlist(watchlist); !error.isEmpty()) {
            return {.error = error};
        }
        collection.lists.push_back(std::move(watchlist));
    }

    if (std::ranges::find(
            collection.lists,
            collection.activeListId,
            &NamedWatchlist::id) == collection.lists.end()) {
        collection.activeListId = collection.lists.front().id;
    }
    return {.collection = std::move(collection)};
}

QByteArray exportWatchlistCsv(const NamedWatchlist& watchlist) {
    QString output = QStringLiteral("symbol,note\r\n");
    for (const auto& entry : watchlist.entries) {
        output += csvField(normalizeWatchlistSymbol(entry.symbol));
        output += u',';
        output += csvField(entry.note);
        output += QStringLiteral("\r\n");
    }
    return output.toUtf8();
}

WatchlistCsvResult importWatchlistCsv(const QByteArray& csv) {
    const auto text = QString::fromUtf8(csv);
    const auto rows = text.split(u'\n');
    if (rows.isEmpty()) {
        return {.error = QStringLiteral("The watchlist CSV is empty.")};
    }

    bool validHeader = true;
    auto header = parseCsvRow(rows.front().trimmed(), validHeader);
    if (!validHeader || header.empty()) {
        return {.error = QStringLiteral("The watchlist CSV header is invalid.")};
    }
    for (auto& field : header) {
        field = field.trimmed().toLower();
    }
    const auto symbolColumn =
        std::ranges::find(header, QStringLiteral("symbol"));
    if (symbolColumn == header.end()) {
        return {.error = QStringLiteral("The watchlist CSV requires a symbol column.")};
    }
    const auto symbolIndex =
        static_cast<std::size_t>(std::distance(header.begin(), symbolColumn));
    const auto noteColumn = std::ranges::find(header, QStringLiteral("note"));
    const auto noteIndex =
        noteColumn == header.end()
            ? std::optional<std::size_t>{}
            : std::optional<std::size_t>{
                  static_cast<std::size_t>(
                      std::distance(header.begin(), noteColumn))};

    WatchlistCsvResult result;
    std::vector<QString> symbols;
    for (qsizetype rowIndex = 1; rowIndex < rows.size(); ++rowIndex) {
        auto row = rows.at(rowIndex);
        if (row.endsWith(u'\r')) {
            row.chop(1);
        }
        if (row.trimmed().isEmpty()) {
            continue;
        }

        bool validRow = true;
        const auto fields = parseCsvRow(row, validRow);
        const auto line = static_cast<std::size_t>(rowIndex + 1);
        if (!validRow || symbolIndex >= fields.size() ||
            (noteIndex && *noteIndex >= fields.size())) {
            result.rejectedLines.push_back(line);
            continue;
        }

        WatchlistEntry entry{
            .symbol = normalizeWatchlistSymbol(
                restoreSpreadsheetCsvText(fields[symbolIndex])),
            .note =
                noteIndex
                    ? restoreSpreadsheetCsvText(fields[*noteIndex]).trimmed()
                    : QString{},
        };
        NamedWatchlist candidate{
            .id = QStringLiteral("csv"),
            .name = QStringLiteral("CSV"),
            .entries = {entry},
        };
        if (!validateWatchlist(candidate).isEmpty() ||
            std::ranges::find(symbols, entry.symbol) != symbols.end()) {
            result.rejectedLines.push_back(line);
            continue;
        }
        symbols.push_back(entry.symbol);
        result.entries.push_back(std::move(entry));
    }
    return result;
}

std::vector<std::size_t> watchlistDisplayOrder(
    const NamedWatchlist& watchlist) {
    std::vector<std::size_t> indexes(watchlist.entries.size());
    for (std::size_t index = 0; index < indexes.size(); ++index) {
        indexes[index] = index;
    }
    if (watchlist.sort == WatchlistSort::Symbol) {
        std::ranges::sort(indexes, [&watchlist](const auto left, const auto right) {
            return normalizeWatchlistSymbol(watchlist.entries[left].symbol) <
                   normalizeWatchlistSymbol(watchlist.entries[right].symbol);
        });
    }
    return indexes;
}

} // namespace tvchart
