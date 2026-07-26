#include "portfolio/portfolio_models.hpp"

#include "watchlists/watchlist_workspace.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QRegularExpression>
#include <QSet>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <ranges>

namespace tvchart {
namespace {

constexpr auto kMaximumWorkspaceBytes = qsizetype{4 * 1024 * 1024};
constexpr auto kMaximumQuantity = 1.0e15;
constexpr auto kMaximumPrice = 1.0e12;
constexpr auto kMaximumAmount = 1.0e15;
constexpr auto kMaximumFees = 1.0e9;

[[nodiscard]] bool finiteRange(
    const double value,
    const double minimum,
    const double maximum) {
    return std::isfinite(value) && value >= minimum && value <= maximum;
}

[[nodiscard]] bool validCurrency(const QString& currency) {
    static const QRegularExpression pattern(
        QStringLiteral("^[A-Z][A-Z0-9]{2,11}$"));
    return pattern.match(currency.trimmed().toUpper()).hasMatch();
}

[[nodiscard]] bool validSymbol(const QString& symbol) {
    const NamedWatchlist candidate{
        .id = QStringLiteral("portfolio-validation"),
        .name = QStringLiteral("Portfolio validation"),
        .entries = {{.symbol = normalizeWatchlistSymbol(symbol)}},
    };
    return validateWatchlist(candidate).isEmpty();
}

[[nodiscard]] std::optional<PortfolioTransactionType> parseType(
    const QString& id) {
    constexpr std::array values{
        PortfolioTransactionType::Deposit,
        PortfolioTransactionType::Withdrawal,
        PortfolioTransactionType::Buy,
        PortfolioTransactionType::Sell,
        PortfolioTransactionType::Dividend,
        PortfolioTransactionType::Fee,
        PortfolioTransactionType::Split,
    };
    for (const auto value : values) {
        if (portfolioTransactionTypeId(value) == id) {
            return value;
        }
    }
    return std::nullopt;
}

[[nodiscard]] QJsonObject transactionToJson(
    const PortfolioTransaction& transaction) {
    return {
        {QStringLiteral("id"), transaction.id},
        {QStringLiteral("type"), portfolioTransactionTypeId(transaction.type)},
        {QStringLiteral("symbol"), transaction.symbol},
        {
            QStringLiteral("timestampUtc"),
            QString::number(transaction.timestampUtc),
        },
        {QStringLiteral("quantity"), transaction.quantity},
        {QStringLiteral("price"), transaction.price},
        {QStringLiteral("amount"), transaction.amount},
        {QStringLiteral("fees"), transaction.fees},
        {QStringLiteral("currency"), transaction.currency},
        {QStringLiteral("note"), transaction.note},
    };
}

[[nodiscard]] std::optional<double> strictNumber(
    const QJsonObject& object,
    const QString& key) {
    const auto value = object.value(key);
    if (!value.isDouble()) {
        return std::nullopt;
    }
    const auto number = value.toDouble();
    return std::isfinite(number)
               ? std::optional<double>{number}
               : std::nullopt;
}

struct HoldingState {
    double quantity{};
    double costBasis{};
    double realized{};
    double income{};
};

} // namespace

QString portfolioTransactionTypeId(const PortfolioTransactionType type) {
    switch (type) {
    case PortfolioTransactionType::Deposit:
        return QStringLiteral("deposit");
    case PortfolioTransactionType::Withdrawal:
        return QStringLiteral("withdrawal");
    case PortfolioTransactionType::Buy:
        return QStringLiteral("buy");
    case PortfolioTransactionType::Sell:
        return QStringLiteral("sell");
    case PortfolioTransactionType::Dividend:
        return QStringLiteral("dividend");
    case PortfolioTransactionType::Fee:
        return QStringLiteral("fee");
    case PortfolioTransactionType::Split:
        return QStringLiteral("split");
    }
    return QStringLiteral("buy");
}

QString portfolioTransactionTypeLabel(const PortfolioTransactionType type) {
    switch (type) {
    case PortfolioTransactionType::Deposit:
        return QStringLiteral("Deposit");
    case PortfolioTransactionType::Withdrawal:
        return QStringLiteral("Withdrawal");
    case PortfolioTransactionType::Buy:
        return QStringLiteral("Buy");
    case PortfolioTransactionType::Sell:
        return QStringLiteral("Sell");
    case PortfolioTransactionType::Dividend:
        return QStringLiteral("Dividend");
    case PortfolioTransactionType::Fee:
        return QStringLiteral("Fee");
    case PortfolioTransactionType::Split:
        return QStringLiteral("Split");
    }
    return QStringLiteral("Buy");
}

QString validatePortfolioTransaction(
    const PortfolioTransaction& transaction) {
    if (transaction.id.trimmed().isEmpty() ||
        transaction.id.size() > 160 || transaction.timestampUtc <= 0 ||
        !validCurrency(transaction.currency) ||
        transaction.note.size() > 512 ||
        !finiteRange(transaction.quantity, 0.0, kMaximumQuantity) ||
        !finiteRange(transaction.price, 0.0, kMaximumPrice) ||
        !finiteRange(transaction.amount, 0.0, kMaximumAmount) ||
        !finiteRange(transaction.fees, 0.0, kMaximumFees)) {
        return QStringLiteral("Portfolio transaction metadata is invalid.");
    }
    const auto hasSymbol = validSymbol(transaction.symbol);
    switch (transaction.type) {
    case PortfolioTransactionType::Buy:
    case PortfolioTransactionType::Sell:
        if (!hasSymbol || transaction.quantity <= 0.0 ||
            transaction.price <= 0.0 || transaction.amount != 0.0) {
            return QStringLiteral("Buy or sell fields are invalid.");
        }
        break;
    case PortfolioTransactionType::Dividend:
        if (!hasSymbol || transaction.amount <= 0.0 ||
            transaction.quantity != 0.0 || transaction.price != 0.0 ||
            transaction.fees != 0.0) {
            return QStringLiteral("Dividend fields are invalid.");
        }
        break;
    case PortfolioTransactionType::Deposit:
    case PortfolioTransactionType::Withdrawal:
    case PortfolioTransactionType::Fee:
        if (!transaction.symbol.trimmed().isEmpty() ||
            transaction.amount <= 0.0 || transaction.quantity != 0.0 ||
            transaction.price != 0.0 || transaction.fees != 0.0) {
            return QStringLiteral("Cash transaction fields are invalid.");
        }
        break;
    case PortfolioTransactionType::Split:
        if (!hasSymbol || transaction.quantity <= 0.0 ||
            transaction.quantity > 10'000.0 || transaction.price != 0.0 ||
            transaction.amount != 0.0 || transaction.fees != 0.0) {
            return QStringLiteral("Split ratio fields are invalid.");
        }
        break;
    }
    return {};
}

QString validatePortfolioTarget(const PortfolioTarget& target) {
    if (!validSymbol(target.symbol) ||
        !std::isfinite(target.targetPercent) ||
        target.targetPercent < 0.0 ||
        target.targetPercent > 100.0) {
        return QStringLiteral("Portfolio target fields are invalid.");
    }
    return {};
}

QString validatePortfolio(const Portfolio& portfolio) {
    if (portfolio.id.trimmed().isEmpty() || portfolio.id.size() > 160 ||
        portfolio.name.trimmed().isEmpty() || portfolio.name.size() > 120 ||
        !validCurrency(portfolio.baseCurrency) ||
        portfolio.transactions.size() >
            PortfolioWorkspace::maximumTransactionsPerPortfolio ||
        portfolio.targets.size() >
            PortfolioWorkspace::maximumTargetsPerPortfolio) {
        return QStringLiteral("Portfolio identity or size is invalid.");
    }
    QSet<QString> identities;
    for (const auto& transaction : portfolio.transactions) {
        if (const auto error = validatePortfolioTransaction(transaction);
            !error.isEmpty()) {
            return error;
        }
        const auto identity = transaction.id.trimmed();
        if (identities.contains(identity)) {
            return QStringLiteral("Portfolio transaction identities must be unique.");
        }
        identities.insert(identity);
        if (transaction.currency.compare(
                portfolio.baseCurrency,
                Qt::CaseInsensitive) != 0) {
            return QStringLiteral(
                "This portfolio model requires one base currency.");
        }
    }
    QSet<QString> targetSymbols;
    auto targetTotal = 0.0L;
    for (const auto& target : portfolio.targets) {
        if (const auto error = validatePortfolioTarget(target);
            !error.isEmpty()) {
            return error;
        }
        const auto symbol =
            normalizeWatchlistSymbol(target.symbol);
        if (targetSymbols.contains(symbol)) {
            return QStringLiteral(
                "Portfolio target symbols must be unique.");
        }
        targetSymbols.insert(symbol);
        targetTotal += target.targetPercent;
    }
    if (targetTotal > 100.0L + 1.0e-9L) {
        return QStringLiteral(
            "Portfolio target allocation cannot exceed 100 percent.");
    }
    return {};
}

QString validatePortfolioWorkspace(const PortfolioWorkspace& workspace) {
    if (workspace.portfolios.empty() ||
        workspace.portfolios.size() >
            PortfolioWorkspace::maximumPortfolios) {
        return QStringLiteral("A portfolio workspace requires 1 to 16 portfolios.");
    }
    QSet<QString> identities;
    auto transactionCount = std::size_t{};
    for (const auto& portfolio : workspace.portfolios) {
        if (const auto error = validatePortfolio(portfolio); !error.isEmpty()) {
            return error;
        }
        const auto identity = portfolio.id.trimmed();
        if (identities.contains(identity)) {
            return QStringLiteral("Portfolio identities must be unique.");
        }
        identities.insert(identity);
        transactionCount += portfolio.transactions.size();
        if (transactionCount >
            PortfolioWorkspace::maximumTransactionsTotal) {
            return QStringLiteral(
                "The portfolio workspace has too many transactions.");
        }
    }
    return {};
}

QByteArray serializePortfolioWorkspace(
    const PortfolioWorkspace& workspace) {
    if (!validatePortfolioWorkspace(workspace).isEmpty()) {
        return {};
    }
    QJsonArray portfolios;
    for (const auto& portfolio : workspace.portfolios) {
        QJsonArray transactions;
        for (const auto& transaction : portfolio.transactions) {
            transactions.append(transactionToJson(transaction));
        }
        QJsonArray targets;
        for (const auto& target : portfolio.targets) {
            targets.append(QJsonObject{
                {QStringLiteral("symbol"), target.symbol},
                {QStringLiteral("targetPercent"), target.targetPercent},
            });
        }
        portfolios.append(QJsonObject{
            {QStringLiteral("id"), portfolio.id},
            {QStringLiteral("name"), portfolio.name},
            {QStringLiteral("baseCurrency"), portfolio.baseCurrency},
            {QStringLiteral("transactions"), transactions},
            {QStringLiteral("targets"), targets},
        });
    }
    const auto payload = QJsonDocument(QJsonObject{
        {
            QStringLiteral("schemaVersion"),
            PortfolioWorkspace::currentSchemaVersion,
        },
        {QStringLiteral("portfolios"), portfolios},
    }).toJson(QJsonDocument::Compact);
    return payload.size() <= kMaximumWorkspaceBytes ? payload : QByteArray{};
}

PortfolioWorkspaceLoadResult deserializePortfolioWorkspace(
    const QByteArray& json) {
    if (json.isEmpty()) {
        return {.workspace = defaultPortfolioWorkspace()};
    }
    if (json.size() > kMaximumWorkspaceBytes) {
        return {.error = QStringLiteral("Saved portfolio workspace is too large.")};
    }
    QJsonParseError parseError;
    const auto document = QJsonDocument::fromJson(json, &parseError);
    if (parseError.error != QJsonParseError::NoError ||
        !document.isObject()) {
        return {.error = QStringLiteral("Saved portfolio JSON is invalid.")};
    }
    const auto root = document.object();
    const auto schemaVersion =
        root.value(QStringLiteral("schemaVersion")).toInt(-1);
    if ((schemaVersion != 1 &&
         schemaVersion != PortfolioWorkspace::currentSchemaVersion) ||
        !root.value(QStringLiteral("portfolios")).isArray()) {
        return {.error = QStringLiteral("Saved portfolio schema is unsupported.")};
    }

    PortfolioWorkspaceLoadResult result;
    const auto portfolios =
        root.value(QStringLiteral("portfolios")).toArray();
    if (portfolios.size() >
        static_cast<qsizetype>(PortfolioWorkspace::maximumPortfolios)) {
        return {.error = QStringLiteral("Saved portfolio workspace is too large.")};
    }
    for (const auto& value : portfolios) {
        if (!value.isObject()) {
            return {.error = QStringLiteral("Saved portfolio entry is invalid.")};
        }
        const auto object = value.toObject();
        const auto transactionValues =
            object.value(QStringLiteral("transactions"));
        const auto targetValues =
            object.value(QStringLiteral("targets"));
        if (!transactionValues.isArray() ||
            transactionValues.toArray().size() >
                static_cast<qsizetype>(
                    PortfolioWorkspace::maximumTransactionsPerPortfolio)) {
            return {.error = QStringLiteral("Saved portfolio transactions are invalid.")};
        }
        if (schemaVersion >= 2 &&
            (!targetValues.isArray() ||
             targetValues.toArray().size() >
                 static_cast<qsizetype>(
                     PortfolioWorkspace::maximumTargetsPerPortfolio))) {
            return {.error = QStringLiteral("Saved portfolio targets are invalid.")};
        }
        Portfolio portfolio{
            .id = object.value(QStringLiteral("id")).toString(),
            .name = object.value(QStringLiteral("name")).toString(),
            .baseCurrency =
                object.value(QStringLiteral("baseCurrency")).toString(),
        };
        for (const auto& transactionValue : transactionValues.toArray()) {
            if (!transactionValue.isObject()) {
                return {.error = QStringLiteral("Saved portfolio transaction is invalid.")};
            }
            const auto transactionObject = transactionValue.toObject();
            const auto type = parseType(
                transactionObject.value(QStringLiteral("type")).toString());
            bool timestampOk = false;
            const auto timestamp = transactionObject
                                       .value(QStringLiteral("timestampUtc"))
                                       .toString()
                                       .toLongLong(&timestampOk);
            const auto quantity =
                strictNumber(transactionObject, QStringLiteral("quantity"));
            const auto price =
                strictNumber(transactionObject, QStringLiteral("price"));
            const auto amount =
                strictNumber(transactionObject, QStringLiteral("amount"));
            const auto fees =
                strictNumber(transactionObject, QStringLiteral("fees"));
            if (!type || !timestampOk || !quantity || !price || !amount ||
                !fees) {
                return {.error = QStringLiteral("Saved portfolio transaction fields are invalid.")};
            }
            portfolio.transactions.push_back({
                .id = transactionObject.value(QStringLiteral("id")).toString(),
                .type = *type,
                .symbol =
                    transactionObject.value(QStringLiteral("symbol")).toString(),
                .timestampUtc = timestamp,
                .quantity = *quantity,
                .price = *price,
                .amount = *amount,
                .fees = *fees,
                .currency =
                    transactionObject.value(QStringLiteral("currency")).toString(),
                .note =
                    transactionObject.value(QStringLiteral("note")).toString(),
            });
        }
        if (schemaVersion >= 2) {
            for (const auto& targetValue : targetValues.toArray()) {
                if (!targetValue.isObject()) {
                    return {.error = QStringLiteral("Saved portfolio target is invalid.")};
                }
                const auto targetObject = targetValue.toObject();
                const auto percent = strictNumber(
                    targetObject,
                    QStringLiteral("targetPercent"));
                if (!percent) {
                    return {.error = QStringLiteral("Saved portfolio target fields are invalid.")};
                }
                portfolio.targets.push_back({
                    .symbol = normalizeWatchlistSymbol(
                        targetObject
                            .value(QStringLiteral("symbol"))
                            .toString()),
                    .targetPercent = *percent,
                });
            }
        }
        result.workspace.portfolios.push_back(std::move(portfolio));
    }
    if (const auto error = validatePortfolioWorkspace(result.workspace);
        !error.isEmpty()) {
        return {.error = error};
    }
    return result;
}

PortfolioSnapshot calculatePortfolioSnapshot(
    const Portfolio& portfolio,
    const QHash<QString, PortfolioPrice>& latestPrices) {
    PortfolioSnapshot result{
        .portfolioId = portfolio.id,
        .baseCurrency = portfolio.baseCurrency.toUpper(),
    };
    if (const auto error = validatePortfolio(portfolio); !error.isEmpty()) {
        result.error = error;
        return result;
    }

    std::vector<const PortfolioTransaction*> transactions;
    transactions.reserve(portfolio.transactions.size());
    for (const auto& transaction : portfolio.transactions) {
        transactions.push_back(&transaction);
    }
    std::ranges::stable_sort(
        transactions,
        [](const auto* left, const auto* right) {
            return left->timestampUtc < right->timestampUtc;
        });

    QHash<QString, HoldingState> states;
    for (const auto* transaction : transactions) {
        const auto symbol = normalizeWatchlistSymbol(transaction->symbol);
        auto& state = states[symbol];
        switch (transaction->type) {
        case PortfolioTransactionType::Deposit:
            result.cash += transaction->amount;
            result.netContributions += transaction->amount;
            break;
        case PortfolioTransactionType::Withdrawal:
            result.cash -= transaction->amount;
            result.netContributions -= transaction->amount;
            break;
        case PortfolioTransactionType::Fee:
            result.cash -= transaction->amount;
            result.feesPaid += transaction->amount;
            break;
        case PortfolioTransactionType::Buy: {
            const auto cost =
                transaction->quantity * transaction->price +
                transaction->fees;
            if (!std::isfinite(cost)) {
                result.error = QStringLiteral("Portfolio buy cost overflowed.");
                return result;
            }
            result.cash -= cost;
            result.feesPaid += transaction->fees;
            state.quantity += transaction->quantity;
            state.costBasis += cost;
            break;
        }
        case PortfolioTransactionType::Sell: {
            const auto tolerance =
                std::max(1.0e-9, std::abs(state.quantity) * 1.0e-12);
            if (transaction->quantity > state.quantity + tolerance) {
                result.error =
                    QStringLiteral("%1 sale exceeds the long position.")
                        .arg(symbol);
                return result;
            }
            const auto averageCost =
                state.quantity > 0.0
                    ? state.costBasis / state.quantity
                    : 0.0;
            const auto removedCost =
                averageCost * transaction->quantity;
            const auto proceeds =
                transaction->quantity * transaction->price -
                transaction->fees;
            result.cash += proceeds;
            result.feesPaid += transaction->fees;
            state.realized += proceeds - removedCost;
            state.quantity =
                std::max(0.0, state.quantity - transaction->quantity);
            state.costBasis =
                std::max(0.0, state.costBasis - removedCost);
            if (state.quantity <= tolerance) {
                state.quantity = 0.0;
                state.costBasis = 0.0;
            }
            break;
        }
        case PortfolioTransactionType::Dividend:
            result.cash += transaction->amount;
            result.income += transaction->amount;
            state.income += transaction->amount;
            break;
        case PortfolioTransactionType::Split:
            if (state.quantity <= 0.0) {
                result.error =
                    QStringLiteral("%1 split has no existing position.")
                        .arg(symbol);
                return result;
            }
            state.quantity *= transaction->quantity;
            if (!std::isfinite(state.quantity) ||
                state.quantity > kMaximumQuantity) {
                result.error =
                    QStringLiteral("%1 split produced an invalid quantity.")
                        .arg(symbol);
                return result;
            }
            break;
        }
    }

    for (auto iterator = states.cbegin(); iterator != states.cend();
         ++iterator) {
        if (iterator.key().isEmpty()) {
            continue;
        }
        const auto& state = iterator.value();
        result.realizedProfitLoss += state.realized;
        if (state.quantity <= 0.0) {
            continue;
        }
        PortfolioHolding holding{
            .symbol = iterator.key(),
            .quantity = state.quantity,
            .costBasis = state.costBasis,
            .averageCost =
                state.quantity > 0.0
                    ? state.costBasis / state.quantity
                    : 0.0,
            .realizedProfitLoss = state.realized,
            .income = state.income,
        };
        const auto price = latestPrices.constFind(iterator.key());
        if (price != latestPrices.cend() &&
            finiteRange(price->price, 0.0, kMaximumPrice) &&
            price->price > 0.0 && price->asOfUtc > 0 &&
            (price->currency.isEmpty() ||
             price->currency.compare(
                 portfolio.baseCurrency,
                 Qt::CaseInsensitive) == 0)) {
            holding.latestPrice = price->price;
            holding.marketValue = state.quantity * price->price;
            holding.unrealizedProfitLoss =
                *holding.marketValue - state.costBasis;
            result.marketValue += *holding.marketValue;
            result.unrealizedProfitLoss += *holding.unrealizedProfitLoss;
            result.valuationTimestampUtc =
                result.valuationTimestampUtc == 0
                    ? price->asOfUtc
                    : std::min(
                          result.valuationTimestampUtc,
                          price->asOfUtc);
        } else {
            result.completeValuation = false;
            result.missingPrices.push_back(iterator.key());
        }
        result.holdings.push_back(std::move(holding));
    }
    std::ranges::sort(
        result.holdings,
        {},
        &PortfolioHolding::symbol);
    result.missingPrices.sort(Qt::CaseInsensitive);
    result.equity = result.cash + result.marketValue;
    result.totalGain = result.equity - result.netContributions;

    if (result.marketValue > 0.0) {
        for (auto& holding : result.holdings) {
            if (!holding.marketValue) {
                continue;
            }
            holding.allocationPercent =
                *holding.marketValue / result.marketValue * 100.0;
            result.largestPositionPercent =
                std::max(
                    result.largestPositionPercent,
                    holding.allocationPercent);
            const auto weight =
                holding.allocationPercent / 100.0;
            result.concentrationIndex += weight * weight;
        }
        if (result.concentrationIndex > 0.0) {
            result.effectiveHoldings = 1.0 / result.concentrationIndex;
        }
    }
    if (!std::isfinite(result.cash) || !std::isfinite(result.equity) ||
        !std::isfinite(result.totalGain)) {
        result.error =
            QStringLiteral("Portfolio calculation produced a non-finite value.");
    }
    return result;
}

PortfolioWorkspace defaultPortfolioWorkspace() {
    return {
        .portfolios = {{
            .id = QStringLiteral("default-portfolio"),
            .name = QStringLiteral("My portfolio"),
            .baseCurrency = QStringLiteral("USD"),
        }},
    };
}

} // namespace tvchart
