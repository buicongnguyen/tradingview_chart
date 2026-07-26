#include "portfolio/portfolio_risk.hpp"

#include "watchlists/watchlist_workspace.hpp"

#include <QHash>
#include <QSet>

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>
#include <ranges>
#include <set>

namespace tvchart {
namespace {

using DayValues = std::map<std::int64_t, double>;

constexpr auto kSecondsPerDay = std::int64_t{86'400};
constexpr auto kAnnualTradingDays = 252.0;
constexpr auto kMinimumRiskObservations = std::size_t{20};

[[nodiscard]] DayValues dailyCloses(const Bars& bars) {
    DayValues result;
    if (validateBars(bars)) {
        return result;
    }
    for (const auto& bar : bars) {
        result[bar.timestamp / kSecondsPerDay] = bar.close;
    }
    return result;
}

[[nodiscard]] DayValues dailyReturns(const Bars& bars) {
    const auto closes = dailyCloses(bars);
    DayValues result;
    if (closes.size() < 2) {
        return result;
    }
    auto previous = closes.begin();
    for (auto iterator = std::next(previous);
         iterator != closes.end();
         ++iterator) {
        if (previous->second > 0.0) {
            result.emplace(
                iterator->first,
                iterator->second / previous->second - 1.0);
        }
        previous = iterator;
    }
    return result;
}

[[nodiscard]] std::vector<std::int64_t> commonDays(
    const std::vector<const DayValues*>& maps) {
    std::vector<std::int64_t> result;
    if (maps.empty() || maps.front()->empty()) {
        return result;
    }
    for (const auto& [day, value] : *maps.front()) {
        static_cast<void>(value);
        if (std::ranges::all_of(
                maps | std::views::drop(1),
                [&](const DayValues* values) {
                    return values->contains(day);
                })) {
            result.push_back(day);
        }
    }
    return result;
}

[[nodiscard]] std::optional<double> sampleVariance(
    const std::vector<double>& values) {
    if (values.size() < 2) {
        return std::nullopt;
    }
    const auto mean =
        std::accumulate(values.begin(), values.end(), 0.0) /
        static_cast<double>(values.size());
    auto sum = 0.0;
    for (const auto value : values) {
        const auto deviation = value - mean;
        sum += deviation * deviation;
    }
    return sum / static_cast<double>(values.size() - 1);
}

[[nodiscard]] std::optional<double> covariance(
    const std::vector<double>& left,
    const std::vector<double>& right) {
    if (left.size() != right.size() || left.size() < 2) {
        return std::nullopt;
    }
    const auto leftMean =
        std::accumulate(left.begin(), left.end(), 0.0) /
        static_cast<double>(left.size());
    const auto rightMean =
        std::accumulate(right.begin(), right.end(), 0.0) /
        static_cast<double>(right.size());
    auto sum = 0.0;
    for (std::size_t index = 0; index < left.size(); ++index) {
        sum += (left[index] - leftMean) *
               (right[index] - rightMean);
    }
    return sum / static_cast<double>(left.size() - 1);
}

[[nodiscard]] double percentile(
    std::vector<double> values,
    const double probability) {
    if (values.empty()) {
        return 0.0;
    }
    std::ranges::sort(values);
    const auto position =
        std::clamp(probability, 0.0, 1.0) *
        static_cast<double>(values.size() - 1);
    const auto lower = static_cast<std::size_t>(std::floor(position));
    const auto upper = static_cast<std::size_t>(std::ceil(position));
    const auto fraction = position - static_cast<double>(lower);
    return values[lower] +
           (values[upper] - values[lower]) * fraction;
}

struct DatedCashFlow {
    std::int64_t timestamp{};
    double amount{};
};

[[nodiscard]] std::optional<double> xirr(
    std::vector<DatedCashFlow> flows) {
    if (flows.size() < 2) {
        return std::nullopt;
    }
    std::ranges::sort(flows, {}, &DatedCashFlow::timestamp);
    const auto hasPositive = std::ranges::any_of(
        flows,
        [](const DatedCashFlow& flow) { return flow.amount > 0.0; });
    const auto hasNegative = std::ranges::any_of(
        flows,
        [](const DatedCashFlow& flow) { return flow.amount < 0.0; });
    if (!hasPositive || !hasNegative) {
        return std::nullopt;
    }
    const auto origin = flows.front().timestamp;
    const auto presentValue =
        [&](const double rate) {
        auto total = 0.0L;
        for (const auto& flow : flows) {
            const auto years =
                static_cast<double>(flow.timestamp - origin) /
                (365.2425 * static_cast<double>(kSecondsPerDay));
            total += static_cast<long double>(flow.amount) /
                     std::pow(1.0 + rate, years);
        }
        return static_cast<double>(total);
    };
    auto low = -0.9999;
    auto high = 10.0;
    auto lowValue = presentValue(low);
    auto highValue = presentValue(high);
    while (std::signbit(lowValue) == std::signbit(highValue) &&
           high < 1.0e6) {
        high *= 10.0;
        highValue = presentValue(high);
    }
    if (!std::isfinite(lowValue) || !std::isfinite(highValue) ||
        std::signbit(lowValue) == std::signbit(highValue)) {
        return std::nullopt;
    }
    for (auto iteration = 0; iteration < 200; ++iteration) {
        const auto middle = low + (high - low) / 2.0;
        const auto middleValue = presentValue(middle);
        if (!std::isfinite(middleValue)) {
            return std::nullopt;
        }
        if (std::abs(middleValue) < 1.0e-9) {
            return middle;
        }
        if (std::signbit(middleValue) == std::signbit(lowValue)) {
            low = middle;
            lowValue = middleValue;
        } else {
            high = middle;
            highValue = middleValue;
        }
    }
    return low + (high - low) / 2.0;
}

[[nodiscard]] std::optional<double> dailyCloseTwr(
    const Portfolio& portfolio,
    const PortfolioHistory& histories,
    const std::vector<std::int64_t>& days) {
    if (days.size() < 2) {
        return std::nullopt;
    }
    std::map<QString, DayValues> closes;
    for (const auto& transaction : portfolio.transactions) {
        const auto symbol =
            normalizeWatchlistSymbol(transaction.symbol);
        if (symbol.isEmpty() || closes.contains(symbol)) {
            continue;
        }
        const auto found = histories.find(symbol);
        if (found == histories.end()) {
            return std::nullopt;
        }
        closes.emplace(symbol, dailyCloses(found->second));
    }

    std::optional<double> previousEquity;
    auto compounded = 1.0;
    auto returnCount = std::size_t{};
    auto previousEnd = std::int64_t{};
    for (const auto day : days) {
        const auto end = (day + 1) * kSecondsPerDay - 1;
        Portfolio partial{
            .id = portfolio.id,
            .name = portfolio.name,
            .baseCurrency = portfolio.baseCurrency,
            .targets = portfolio.targets,
        };
        auto externalFlow = 0.0;
        for (const auto& transaction : portfolio.transactions) {
            if (transaction.timestampUtc <= end) {
                partial.transactions.push_back(transaction);
            }
            if (transaction.timestampUtc > previousEnd &&
                transaction.timestampUtc <= end) {
                if (transaction.type ==
                    PortfolioTransactionType::Deposit) {
                    externalFlow += transaction.amount;
                } else if (
                    transaction.type ==
                    PortfolioTransactionType::Withdrawal) {
                    externalFlow -= transaction.amount;
                }
            }
        }
        QHash<QString, PortfolioPrice> prices;
        for (const auto& [symbol, values] : closes) {
            const auto found = values.find(day);
            if (found != values.end()) {
                prices.insert(
                    symbol,
                    {
                        .price = found->second,
                        .asOfUtc = end,
                        .currency = portfolio.baseCurrency,
                    });
            }
        }
        const auto snapshot =
            calculatePortfolioSnapshot(partial, prices);
        if (!snapshot.ok() || !snapshot.completeValuation) {
            return std::nullopt;
        }
        if (previousEquity && *previousEquity > 0.0) {
            const auto periodReturn =
                (snapshot.equity - externalFlow) /
                    *previousEquity -
                1.0;
            if (!std::isfinite(periodReturn) ||
                periodReturn <= -1.0) {
                return std::nullopt;
            }
            compounded *= 1.0 + periodReturn;
            ++returnCount;
        }
        previousEquity = snapshot.equity;
        previousEnd = end;
    }
    return returnCount > 0
               ? std::optional<double>{(compounded - 1.0) * 100.0}
               : std::nullopt;
}

} // namespace

PortfolioRiskReport calculatePortfolioRisk(
    const Portfolio& portfolio,
    const PortfolioSnapshot& snapshot,
    const PortfolioHistory& histories,
    QString benchmarkSymbol,
    const Bars& benchmarkBars) {
    PortfolioRiskReport report{
        .benchmarkSymbol =
            normalizeWatchlistSymbol(std::move(benchmarkSymbol)),
    };
    if (const auto error = validatePortfolio(portfolio);
        !error.isEmpty()) {
        report.error = error;
        return report;
    }
    if (!snapshot.ok() || !snapshot.completeValuation ||
        snapshot.equity <= 0.0) {
        report.error =
            QStringLiteral(
                "Portfolio risk requires a complete positive current valuation.");
        return report;
    }

    std::vector<QString> symbols;
    std::vector<double> weights;
    std::map<QString, DayValues> returnMaps;
    for (const auto& holding : snapshot.holdings) {
        if (holding.quantity <= 0.0 || !holding.marketValue) {
            continue;
        }
        const auto symbol =
            normalizeWatchlistSymbol(holding.symbol);
        const auto found = histories.find(symbol);
        if (found == histories.end()) {
            report.missingHistory.push_back(symbol);
            continue;
        }
        auto returns = dailyReturns(found->second);
        if (returns.size() < kMinimumRiskObservations) {
            report.missingHistory.push_back(
                QStringLiteral("%1 (<%2 daily returns)")
                    .arg(symbol)
                    .arg(kMinimumRiskObservations));
            continue;
        }
        symbols.push_back(symbol);
        weights.push_back(*holding.marketValue / snapshot.equity);
        returnMaps.emplace(symbol, std::move(returns));
    }
    if (!report.missingHistory.isEmpty()) {
        report.error =
            QStringLiteral(
                "Compatible daily history is incomplete for one or more holdings.");
        return report;
    }
    if (symbols.empty()) {
        report.error =
            QStringLiteral("Portfolio risk requires at least one priced holding.");
        return report;
    }

    std::vector<const DayValues*> maps;
    for (const auto& symbol : symbols) {
        maps.push_back(&returnMaps.at(symbol));
    }
    const auto days = commonDays(maps);
    if (days.size() < kMinimumRiskObservations) {
        report.error =
            QStringLiteral(
                "Portfolio holdings have fewer than 20 common daily returns.");
        return report;
    }
    report.observations = days.size();
    report.firstTimestamp = days.front() * kSecondsPerDay;
    report.lastTimestamp = days.back() * kSecondsPerDay;

    std::vector<double> portfolioReturns;
    portfolioReturns.reserve(days.size());
    std::vector<std::vector<double>> matrix(
        symbols.size(),
        std::vector<double>{});
    for (auto& values : matrix) {
        values.reserve(days.size());
    }
    for (const auto day : days) {
        auto combined = 0.0;
        for (std::size_t index = 0; index < symbols.size(); ++index) {
            const auto value = returnMaps.at(symbols[index]).at(day);
            matrix[index].push_back(value);
            combined += weights[index] * value;
        }
        portfolioReturns.push_back(combined);
    }

    auto compounded = 1.0;
    auto peak = 1.0;
    auto maximumDrawdown = 0.0;
    for (const auto value : portfolioReturns) {
        compounded *= 1.0 + value;
        peak = std::max(peak, compounded);
        maximumDrawdown = std::max(
            maximumDrawdown,
            (peak - compounded) / peak);
    }
    if (compounded > 0.0) {
        report.annualizedReturnPercent =
            (std::pow(
                 compounded,
                 kAnnualTradingDays /
                     static_cast<double>(portfolioReturns.size())) -
             1.0) *
            100.0;
    }
    const auto variance = sampleVariance(portfolioReturns);
    if (variance && *variance > 1.0e-20) {
        const auto dailyMean =
            std::accumulate(
                portfolioReturns.begin(),
                portfolioReturns.end(),
                0.0) /
            static_cast<double>(portfolioReturns.size());
        const auto dailyDeviation = std::sqrt(*variance);
        report.annualizedVolatilityPercent =
            dailyDeviation * std::sqrt(kAnnualTradingDays) * 100.0;
        report.sharpeRatio =
            dailyMean / dailyDeviation *
            std::sqrt(kAnnualTradingDays);
    }
    report.maximumDrawdownPercent = maximumDrawdown * 100.0;
    const auto tailCutoff = percentile(portfolioReturns, 0.05);
    report.historicalValueAtRisk95Percent =
        std::max(0.0, -tailCutoff * 100.0);
    auto tailTotal = 0.0L;
    auto tailCount = std::size_t{};
    for (const auto value : portfolioReturns) {
        if (value <= tailCutoff) {
            tailTotal += value;
            ++tailCount;
        }
    }
    if (tailCount > 0) {
        report.historicalConditionalValueAtRisk95Percent =
            std::max(
                0.0,
                -static_cast<double>(
                    tailTotal /
                    static_cast<long double>(tailCount)) *
                    100.0);
    }

    for (std::size_t left = 0; left < symbols.size(); ++left) {
        for (std::size_t right = left; right < symbols.size(); ++right) {
            const auto pairDays = commonDays({
                &returnMaps.at(symbols[left]),
                &returnMaps.at(symbols[right]),
            });
            std::vector<double> leftValues;
            std::vector<double> rightValues;
            for (const auto day : pairDays) {
                leftValues.push_back(
                    returnMaps.at(symbols[left]).at(day));
                rightValues.push_back(
                    returnMaps.at(symbols[right]).at(day));
            }
            std::optional<double> correlation;
            const auto leftVariance = sampleVariance(leftValues);
            const auto rightVariance = sampleVariance(rightValues);
            const auto pairCovariance =
                covariance(leftValues, rightValues);
            if (leftVariance && rightVariance &&
                pairCovariance && *leftVariance > 1.0e-20 &&
                *rightVariance > 1.0e-20) {
                correlation =
                    *pairCovariance /
                    std::sqrt(*leftVariance * *rightVariance);
            }
            report.correlations.push_back({
                .leftSymbol = symbols[left],
                .rightSymbol = symbols[right],
                .observations = pairDays.size(),
                .correlation = correlation,
            });
        }
    }

    std::vector<std::vector<double>> covarianceMatrix(
        symbols.size(),
        std::vector<double>(symbols.size()));
    for (std::size_t row = 0; row < symbols.size(); ++row) {
        for (std::size_t column = 0; column < symbols.size(); ++column) {
            covarianceMatrix[row][column] =
                covariance(matrix[row], matrix[column]).value_or(0.0);
        }
    }
    std::vector<double> covarianceTimesWeight(symbols.size());
    auto portfolioVariance = 0.0;
    for (std::size_t row = 0; row < symbols.size(); ++row) {
        for (std::size_t column = 0; column < symbols.size(); ++column) {
            covarianceTimesWeight[row] +=
                covarianceMatrix[row][column] * weights[column];
        }
        portfolioVariance +=
            weights[row] * covarianceTimesWeight[row];
    }
    if (portfolioVariance > 1.0e-20) {
        for (std::size_t index = 0; index < symbols.size(); ++index) {
            report.riskContributions.push_back({
                .symbol = symbols[index],
                .weightPercent = weights[index] * 100.0,
                .contributionPercent =
                    weights[index] *
                    covarianceTimesWeight[index] /
                    portfolioVariance * 100.0,
            });
        }
    }

    const auto benchmarkReturns = dailyReturns(benchmarkBars);
    std::vector<double> alignedPortfolio;
    std::vector<double> alignedBenchmark;
    for (std::size_t index = 0; index < days.size(); ++index) {
        const auto found = benchmarkReturns.find(days[index]);
        if (found != benchmarkReturns.end()) {
            alignedPortfolio.push_back(portfolioReturns[index]);
            alignedBenchmark.push_back(found->second);
        }
    }
    report.benchmarkObservations = alignedPortfolio.size();
    const auto benchmarkVariance =
        sampleVariance(alignedBenchmark);
    const auto benchmarkCovariance =
        covariance(alignedPortfolio, alignedBenchmark);
    if (alignedPortfolio.size() >= kMinimumRiskObservations &&
        benchmarkVariance && benchmarkCovariance &&
        *benchmarkVariance > 1.0e-20) {
        report.beta = *benchmarkCovariance / *benchmarkVariance;
        const auto portfolioMean =
            std::accumulate(
                alignedPortfolio.begin(),
                alignedPortfolio.end(),
                0.0) /
            static_cast<double>(alignedPortfolio.size());
        const auto benchmarkMean =
            std::accumulate(
                alignedBenchmark.begin(),
                alignedBenchmark.end(),
                0.0) /
            static_cast<double>(alignedBenchmark.size());
        report.annualizedAlphaPercent =
            (portfolioMean - *report.beta * benchmarkMean) *
            kAnnualTradingDays * 100.0;
    }

    std::vector<DatedCashFlow> flows;
    auto latestExternalFlowTimestamp = std::int64_t{};
    for (const auto& transaction : portfolio.transactions) {
        if (transaction.type == PortfolioTransactionType::Deposit) {
            flows.push_back({
                .timestamp = transaction.timestampUtc,
                .amount = -transaction.amount,
            });
            latestExternalFlowTimestamp =
                std::max(
                    latestExternalFlowTimestamp,
                    transaction.timestampUtc);
        } else if (
            transaction.type == PortfolioTransactionType::Withdrawal) {
            flows.push_back({
                .timestamp = transaction.timestampUtc,
                .amount = transaction.amount,
            });
            latestExternalFlowTimestamp =
                std::max(
                    latestExternalFlowTimestamp,
                    transaction.timestampUtc);
        }
    }
    const auto valuationTimestamp =
        snapshot.valuationTimestampUtc > 0
            ? snapshot.valuationTimestampUtc
            : report.lastTimestamp + kSecondsPerDay - 1;
    if (!flows.empty() &&
        valuationTimestamp > latestExternalFlowTimestamp) {
        flows.push_back({
            .timestamp = valuationTimestamp,
            .amount = snapshot.equity,
        });
        if (const auto rate = xirr(std::move(flows))) {
            report.moneyWeightedReturnPercent = *rate * 100.0;
        }
    }
    report.dailyCloseTimeWeightedReturnPercent =
        dailyCloseTwr(portfolio, histories, days);
    report.dailyCloseTimeWeightedReturnIsApproximate =
        report.dailyCloseTimeWeightedReturnPercent.has_value();
    return report;
}

RebalanceReport calculateRebalance(
    const Portfolio& portfolio,
    const PortfolioSnapshot& snapshot,
    const QHash<QString, PortfolioPrice>& latestPrices) {
    RebalanceReport report;
    if (const auto error = validatePortfolio(portfolio);
        !error.isEmpty()) {
        report.error = error;
        return report;
    }
    if (!snapshot.ok() || !snapshot.completeValuation ||
        snapshot.equity <= 0.0) {
        report.error =
            QStringLiteral(
                "Rebalancing requires a complete positive current valuation.");
        return report;
    }
    std::map<QString, double> targets;
    auto targetTotal = 0.0;
    for (const auto& target : portfolio.targets) {
        const auto symbol =
            normalizeWatchlistSymbol(target.symbol);
        targets[symbol] = target.targetPercent;
        targetTotal += target.targetPercent;
    }
    report.targetCashPercent = std::max(0.0, 100.0 - targetTotal);
    std::map<QString, const PortfolioHolding*> holdings;
    for (const auto& holding : snapshot.holdings) {
        holdings.emplace(
            normalizeWatchlistSymbol(holding.symbol),
            &holding);
    }
    std::set<QString> symbols;
    for (const auto& [symbol, percent] : targets) {
        static_cast<void>(percent);
        symbols.insert(symbol);
    }
    for (const auto& [symbol, holding] : holdings) {
        static_cast<void>(holding);
        symbols.insert(symbol);
    }
    for (const auto& symbol : symbols) {
        const auto holding = holdings.contains(symbol)
                                 ? holdings.at(symbol)
                                 : nullptr;
        const auto currentValue =
            holding && holding->marketValue
                ? *holding->marketValue
                : 0.0;
        const auto targetPercent =
            targets.contains(symbol) ? targets.at(symbol) : 0.0;
        const auto targetValue =
            snapshot.equity * targetPercent / 100.0;
        const auto difference = targetValue - currentValue;
        std::optional<double> shares;
        auto price =
            holding && holding->latestPrice
                ? holding->latestPrice
                : std::optional<double>{};
        if (!price) {
            const auto latest = latestPrices.constFind(symbol);
            if (latest != latestPrices.cend() &&
                std::isfinite(latest->price) &&
                latest->price > 0.0 &&
                latest->asOfUtc > 0 &&
                (latest->currency.isEmpty() ||
                 latest->currency.compare(
                     portfolio.baseCurrency,
                     Qt::CaseInsensitive) == 0)) {
                price = latest->price;
            }
        }
        if (price) {
            shares = difference / *price;
        } else if (std::abs(difference) > 1.0e-9) {
            report.missingPrices.push_back(symbol);
        }
        report.suggestions.push_back({
            .symbol = symbol,
            .currentPercent =
                snapshot.equity > 0.0
                    ? currentValue / snapshot.equity * 100.0
                    : 0.0,
            .targetPercent = targetPercent,
            .currentValue = currentValue,
            .targetValue = targetValue,
            .differenceValue = difference,
            .approximateShares = shares,
        });
    }
    return report;
}

} // namespace tvchart
