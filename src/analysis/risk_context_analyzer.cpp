#include "analysis/risk_context_analyzer.hpp"

#include "analysis/market_structure_analyzer.hpp"
#include "analysis/technical_indicators.hpp"
#include "fundamentals/fundamental_analysis.hpp"
#include "watchlists/watchlist_workspace.hpp"

#include <QDateTime>
#include <QLocale>
#include <QTimeZone>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <limits>
#include <map>
#include <numeric>
#include <ranges>
#include <utility>

namespace tvchart {
namespace {

constexpr auto kMinimumBars = std::size_t{60};
constexpr auto kMaximumBars = std::size_t{2'500};
constexpr auto kMaximumHistoricalDates = std::size_t{750};
constexpr auto kForwardFive = std::size_t{5};
constexpr auto kForwardTwenty = std::size_t{20};

struct ScoreResult {
    RiskLevel level{RiskLevel::Unavailable};
    int score{};
    int adversePoints{};
    int availableWeight{};
    std::uint32_t availableMask{};
    std::vector<RiskEvidence> evidence;
    std::vector<QString> missingInputs;
};

[[nodiscard]] std::optional<MarketStructureReport>
marketStructureAt(
    const QString& symbol,
    const Bars& bars,
    const std::size_t end) {
    if (end < 59 || end >= bars.size()) {
        return std::nullopt;
    }
    const auto first =
        end + 1 > 600 ? end + 1 - 600 : std::size_t{};
    Bars prefix(
        bars.begin() + static_cast<std::ptrdiff_t>(first),
        bars.begin() + static_cast<std::ptrdiff_t>(end + 1));
    auto report = analyzeMarketStructure({
        .symbol = symbol,
        .bars = std::move(prefix),
        .timeframe = Timeframe::OneDay,
        .settings = {
            .pivotStrength = 3,
            .zoneAtrMultiplier = 0.75,
            .minimumZoneTouches = 2,
            .maximumLookbackBars = 600,
            .maximumPivots = 64,
            .maximumZones = 12,
            .maximumPatterns = 24,
        },
        .includeHistoricalValidation = false,
    });
    if (!report.ok()) {
        return std::nullopt;
    }
    return report;
}

struct CategoryAccumulator {
    RiskCategory category;
    int cap;
    int used{};
    QDate asOfDate;
    std::vector<RiskEvidence>& evidence;

    void adverse(
        const int requestedPoints,
        QString title,
        QString observed,
        QString explanation,
        QString source) {
        const auto points = std::clamp(requestedPoints, 0, cap - used);
        if (points <= 0) {
            return;
        }
        used += points;
        evidence.push_back({
            .kind = RiskEvidenceKind::Adverse,
            .category = category,
            .points = points,
            .title = std::move(title),
            .observed = std::move(observed),
            .explanation = std::move(explanation),
            .source = std::move(source),
            .asOfDate = asOfDate,
        });
    }

    void constructive(
        QString title,
        QString observed,
        QString explanation,
        QString source) {
        evidence.push_back({
            .kind = RiskEvidenceKind::Constructive,
            .category = category,
            .points = 0,
            .title = std::move(title),
            .observed = std::move(observed),
            .explanation = std::move(explanation),
            .source = std::move(source),
            .asOfDate = asOfDate,
        });
    }
};

[[nodiscard]] QDate utcDate(const std::int64_t timestamp) {
    return QDateTime::fromSecsSinceEpoch(timestamp, QTimeZone::UTC).date();
}

[[nodiscard]] QString number(
    const double value,
    const int decimals = 2,
    const QString& suffix = {}) {
    return QStringLiteral("%1%2")
        .arg(QLocale::c().toString(value, 'f', decimals), suffix);
}

[[nodiscard]] std::optional<double> movingAverage(
    const Bars& bars,
    const std::size_t end,
    const std::size_t period) {
    if (period == 0 || end >= bars.size() || end + 1 < period) {
        return std::nullopt;
    }
    auto sum = 0.0L;
    for (auto index = end + 1 - period; index <= end; ++index) {
        sum += static_cast<long double>(bars[index].close);
    }
    return static_cast<double>(sum / static_cast<long double>(period));
}

[[nodiscard]] std::optional<double> returnPercent(
    const Bars& bars,
    const std::size_t end,
    const std::size_t lookback) {
    if (end >= bars.size() || end < lookback ||
        bars[end - lookback].close <= 0.0) {
        return std::nullopt;
    }
    return ((bars[end].close / bars[end - lookback].close) - 1.0) * 100.0;
}

[[nodiscard]] std::optional<double> rsi(
    const Bars& bars,
    const std::size_t end) {
    if (end < 14 || end >= bars.size()) {
        return std::nullopt;
    }
    Bars prefix(bars.begin(), bars.begin() + static_cast<std::ptrdiff_t>(end + 1));
    const auto calculation = calculateIndicator(
        prefix,
        defaultIndicatorSpec(IndicatorKind::RelativeStrengthIndex));
    if (calculation.primary.empty()) {
        return std::nullopt;
    }
    return calculation.primary.back().value;
}

[[nodiscard]] double trueRangePercent(
    const Bars& bars,
    const std::size_t index) {
    const auto& current = bars[index];
    auto range = current.high - current.low;
    if (index > 0) {
        range = std::max(
            {range,
             std::abs(current.high - bars[index - 1].close),
             std::abs(current.low - bars[index - 1].close)});
    }
    return current.close > 0.0 ? (range / current.close) * 100.0 : 0.0;
}

[[nodiscard]] std::optional<double> averageTrueRangePercent(
    const Bars& bars,
    const std::size_t end,
    const std::size_t period = 14) {
    if (end >= bars.size() || end + 1 < period) {
        return std::nullopt;
    }
    auto total = 0.0;
    for (auto index = end + 1 - period; index <= end; ++index) {
        total += trueRangePercent(bars, index);
    }
    return total / static_cast<double>(period);
}

[[nodiscard]] std::optional<double> atrPercentile(
    const Bars& bars,
    const std::size_t end) {
    const auto current = averageTrueRangePercent(bars, end);
    if (!current || end < 60) {
        return std::nullopt;
    }
    const auto first = end > 252 ? end - 252 : std::size_t{13};
    auto observations = std::size_t{};
    auto notGreater = std::size_t{};
    for (auto index = std::max<std::size_t>(13, first); index <= end; ++index) {
        const auto value = averageTrueRangePercent(bars, index);
        if (!value) {
            continue;
        }
        ++observations;
        if (*value <= *current) {
            ++notGreater;
        }
    }
    if (observations < 30) {
        return std::nullopt;
    }
    return static_cast<double>(notGreater) * 100.0 /
           static_cast<double>(observations);
}

[[nodiscard]] double averageGapPercent(
    const Bars& bars,
    const std::size_t end,
    const std::size_t period = 20) {
    const auto first = end + 1 - period;
    auto total = 0.0;
    auto count = std::size_t{};
    for (auto index = std::max<std::size_t>(1, first); index <= end; ++index) {
        total += std::abs(
            (bars[index].open / bars[index - 1].close) - 1.0) * 100.0;
        ++count;
    }
    return count == 0 ? 0.0 : total / static_cast<double>(count);
}

[[nodiscard]] double averageDollarVolume(
    const Bars& bars,
    const std::size_t end,
    const std::size_t period = 20) {
    const auto count = std::min(period, end + 1);
    auto total = 0.0L;
    for (auto index = end + 1 - count; index <= end; ++index) {
        total += static_cast<long double>(bars[index].close) *
                 static_cast<long double>(bars[index].volume);
    }
    return static_cast<double>(total / static_cast<long double>(count));
}

[[nodiscard]] std::optional<double> median(std::vector<double> values) {
    if (values.empty()) {
        return std::nullopt;
    }
    std::ranges::sort(values);
    const auto middle = values.size() / 2;
    if (values.size() % 2 == 1) {
        return values[middle];
    }
    return std::midpoint(values[middle - 1], values[middle]);
}

[[nodiscard]] std::size_t benchmarkIndexAt(
    const Bars& bars,
    const std::int64_t timestamp) {
    const auto upper = std::upper_bound(
        bars.begin(),
        bars.end(),
        timestamp,
        [](const std::int64_t value, const Bar& bar) {
            return value < bar.timestamp;
        });
    if (upper == bars.begin()) {
        return bars.size();
    }
    return static_cast<std::size_t>(
        std::distance(bars.begin(), std::prev(upper)));
}

[[nodiscard]] bool relevantEvent(
    const ResearchEvent& event,
    const QString& symbol) {
    if (event.symbol.compare(symbol, Qt::CaseInsensitive) == 0) {
        return true;
    }
    return event.symbol.trimmed().isEmpty() &&
           (event.type == ResearchEventType::EconomicRelease ||
            event.type == ResearchEventType::CentralBank ||
            event.type == ResearchEventType::OptionsExpiration ||
            event.type == ResearchEventType::MarketHoliday);
}

[[nodiscard]] bool eventKnownBy(
    const ResearchEvent& event,
    const QDate& date) {
    if (event.asOfUtc <= 0) {
        return false;
    }
    const auto cutoff =
        QDateTime(date.addDays(1), QTime(0, 0), QTimeZone::UTC).toSecsSinceEpoch();
    return event.asOfUtc < cutoff;
}

[[nodiscard]] std::optional<FundamentalSnapshot> snapshotAt(
    const std::optional<FundamentalCompany>& company,
    const QString& symbol,
    const QDate& date) {
    if (!company || company->facts.empty() ||
        normalizeWatchlistSymbol(company->symbol) !=
            normalizeWatchlistSymbol(symbol) ||
        !validateFundamentalCompany(*company).isEmpty()) {
        return std::nullopt;
    }
    auto snapshot = buildFundamentalSnapshot(*company, date);
    if (!snapshot.ok()) {
        return std::nullopt;
    }
    return snapshot;
}

[[nodiscard]] RiskLevel levelForScore(const int score) {
    if (score < 25) {
        return RiskLevel::Lower;
    }
    if (score < 45) {
        return RiskLevel::Moderate;
    }
    if (score < 65) {
        return RiskLevel::Elevated;
    }
    return RiskLevel::High;
}

void finishCategory(
    ScoreResult& result,
    const CategoryAccumulator& category,
    const std::uint32_t bit,
    const int availableWeight) {
    result.adversePoints += category.used;
    result.availableWeight += availableWeight;
    result.availableMask |= bit;
}

[[nodiscard]] ScoreResult scoreAt(
    const RiskContextInput& input,
    const Bars& security,
    const std::size_t securityEnd,
    const Bars& benchmark,
    const bool currentEvaluation) {
    ScoreResult result;
    const auto date = utcDate(security[securityEnd].timestamp);
    constexpr auto marketBit = std::uint32_t{1} << 0;
    constexpr auto trendBit = std::uint32_t{1} << 1;
    constexpr auto candleBit = std::uint32_t{1} << 2;
    constexpr auto volatilityBit = std::uint32_t{1} << 3;
    constexpr auto locationBit = std::uint32_t{1} << 4;
    constexpr auto eventBit = std::uint32_t{1} << 5;
    constexpr auto fundamentalBit = std::uint32_t{1} << 6;

    const auto benchmarkEnd =
        benchmarkIndexAt(benchmark, security[securityEnd].timestamp);
    const auto structure =
        marketStructureAt(input.symbol, security, securityEnd);
    if (benchmarkEnd != benchmark.size() && benchmarkEnd >= 199) {
        CategoryAccumulator category{
            RiskCategory::MarketRegime, 20, 0, date, result.evidence};
        const auto sma200 = *movingAverage(benchmark, benchmarkEnd, 200);
        const auto sma50 = *movingAverage(benchmark, benchmarkEnd, 50);
        const auto priorSma50 =
            *movingAverage(benchmark, benchmarkEnd - 20, 50);
        const auto return20 = *returnPercent(benchmark, benchmarkEnd, 20);
        if (benchmark[benchmarkEnd].close < sma200) {
            category.adverse(
                10,
                QStringLiteral("Benchmark below 200-day average"),
                number(
                    ((benchmark[benchmarkEnd].close / sma200) - 1.0) * 100.0,
                    2,
                    QStringLiteral("%")),
                QStringLiteral(
                    "The broad-market proxy is below its long trend reference."),
                input.benchmarkSymbol);
        } else {
            category.constructive(
                QStringLiteral("Benchmark above 200-day average"),
                number(
                    ((benchmark[benchmarkEnd].close / sma200) - 1.0) * 100.0,
                    2,
                    QStringLiteral("%")),
                QStringLiteral(
                    "The broad-market proxy remains above its long trend reference."),
                input.benchmarkSymbol);
        }
        if (sma50 < priorSma50) {
            category.adverse(
                6,
                QStringLiteral("Benchmark medium trend is falling"),
                number(((sma50 / priorSma50) - 1.0) * 100.0, 2, QStringLiteral("%")),
                QStringLiteral("The 50-day average declined over 20 sessions."),
                input.benchmarkSymbol);
        }
        if (return20 <= -5.0) {
            category.adverse(
                4,
                QStringLiteral("Benchmark has recent downside"),
                number(return20, 2, QStringLiteral("%")),
                QStringLiteral("The benchmark lost at least 5% over 20 sessions."),
                input.benchmarkSymbol);
        }
        finishCategory(result, category, marketBit, category.cap);
    } else {
        result.missingInputs.push_back(
            QStringLiteral("Benchmark regime needs 200 daily bars through %1.")
                .arg(date.toString(Qt::ISODate)));
    }

    if (securityEnd >= 199) {
        CategoryAccumulator category{
            RiskCategory::TrendMomentum, 20, 0, date, result.evidence};
        const auto close = security[securityEnd].close;
        const auto sma200 = *movingAverage(security, securityEnd, 200);
        const auto sma50 = *movingAverage(security, securityEnd, 50);
        const auto return20 = *returnPercent(security, securityEnd, 20);
        const auto currentRsi = *rsi(security, securityEnd);
        if (close < sma200) {
            category.adverse(
                8,
                QStringLiteral("Price below 200-day average"),
                number(((close / sma200) - 1.0) * 100.0, 2, QStringLiteral("%")),
                QStringLiteral("Price is below its long-term trend reference."),
                input.symbol);
        } else {
            category.constructive(
                QStringLiteral("Price above 200-day average"),
                number(((close / sma200) - 1.0) * 100.0, 2, QStringLiteral("%")),
                QStringLiteral("Price remains above its long-term trend reference."),
                input.symbol);
        }
        if (sma50 < sma200) {
            category.adverse(
                5,
                QStringLiteral("50-day average below 200-day average"),
                number(((sma50 / sma200) - 1.0) * 100.0, 2, QStringLiteral("%")),
                QStringLiteral("The medium trend is below the long trend."),
                input.symbol);
        } else {
            category.constructive(
                QStringLiteral("50-day average above 200-day average"),
                number(((sma50 / sma200) - 1.0) * 100.0, 2, QStringLiteral("%")),
                QStringLiteral("Medium and long trend alignment is constructive."),
                input.symbol);
        }
        if (return20 <= -8.0) {
            category.adverse(
                4,
                QStringLiteral("Strong 20-day downside momentum"),
                number(return20, 2, QStringLiteral("%")),
                QStringLiteral("Price lost at least 8% over 20 sessions."),
                input.symbol);
        }
        if (currentRsi <= 30.0) {
            category.adverse(
                4,
                QStringLiteral("RSI shows downside momentum"),
                number(currentRsi),
                QStringLiteral(
                    "Oversold readings can remain oversold; this is falling-knife risk, not a reversal signal."),
                QStringLiteral("RSI(14)"));
        } else if (currentRsi >= 75.0) {
            category.adverse(
                3,
                QStringLiteral("RSI is highly extended"),
                number(currentRsi),
                QStringLiteral("A highly extended entry can be vulnerable to a pullback."),
                QStringLiteral("RSI(14)"));
        } else if (currentRsi >= 40.0 && currentRsi <= 65.0) {
            category.constructive(
                QStringLiteral("RSI is not at an extreme"),
                number(currentRsi),
                QStringLiteral("Momentum is outside the analyzer's extreme-risk zones."),
                QStringLiteral("RSI(14)"));
        }
        if (structure) {
            if (structure->bias == StructureBias::Bearish) {
                category.adverse(
                    3,
                    QStringLiteral("Confirmed swing structure is bearish"),
                    structureBiasLabel(structure->bias),
                    QStringLiteral(
                        "The latest two confirmed swing highs and lows are both declining."),
                    QStringLiteral("Market Structure"));
            }
            if (structure->confluence.state ==
                    QStringLiteral("Aligned") &&
                structure->bias == StructureBias::Bearish) {
                category.adverse(
                    2,
                    QStringLiteral("Higher-timeframe structure confirms downside"),
                    structure->confluence.state,
                    structure->confluence.explanation,
                    QStringLiteral("Market Structure"));
            } else if (
                structure->confluence.state ==
                    QStringLiteral("Aligned") &&
                structure->bias == StructureBias::Bullish) {
                category.constructive(
                    QStringLiteral("Higher-timeframe structure confirms upside"),
                    structure->confluence.state,
                    structure->confluence.explanation,
                    QStringLiteral("Market Structure"));
            }
        }
        finishCategory(result, category, trendBit, category.cap);
    } else {
        result.missingInputs.push_back(
            QStringLiteral("Trend context needs 200 daily security bars."));
    }

    if (securityEnd >= 20) {
        CategoryAccumulator category{
            RiskCategory::PriceAction, 15, 0, date, result.evidence};
        const auto& current = security[securityEnd];
        const auto& previous = security[securityEnd - 1];
        const auto range = current.high - current.low;
        const auto body = std::abs(current.close - current.open);
        const auto upperWick =
            current.high - std::max(current.open, current.close);
        if (range > 0.0 && upperWick >= body * 2.0 &&
            upperWick / range >= 0.4) {
            category.adverse(
                5,
                QStringLiteral("Long upper-wick rejection"),
                number((upperWick / range) * 100.0, 1, QStringLiteral("% of range")),
                QStringLiteral("The latest candle rejected a material part of its intraday high."),
                QStringLiteral("Latest completed daily candle"));
        }
        if (range > 0.0 && current.close < current.open &&
            body / range >= 0.5) {
            category.adverse(
                3,
                QStringLiteral("Wide bearish candle"),
                number((body / range) * 100.0, 1, QStringLiteral("% of range")),
                QStringLiteral("The latest close finished below its open with a large real body."),
                QStringLiteral("Latest completed daily candle"));
        }
        if (current.close < current.open && previous.close > previous.open &&
            current.open >= previous.close &&
            current.close <= previous.open) {
            category.adverse(
                5,
                QStringLiteral("Bearish engulfing body"),
                QStringLiteral("present"),
                QStringLiteral("The latest bearish real body enveloped the prior bullish body."),
                QStringLiteral("Two completed daily candles"));
        }
        auto priorHigh = security[securityEnd - 20].high;
        for (auto index = securityEnd - 19; index < securityEnd; ++index) {
            priorHigh = std::max(priorHigh, security[index].high);
        }
        if (current.high > priorHigh && current.close < priorHigh) {
            category.adverse(
                5,
                QStringLiteral("Failed 20-day breakout"),
                number(((current.close / priorHigh) - 1.0) * 100.0, 2, QStringLiteral("%")),
                QStringLiteral("Price traded above prior resistance but closed back below it."),
                QStringLiteral("20-day high"));
        }
        if (structure) {
            const auto bearish = std::ranges::find_if(
                structure->patterns.rbegin(),
                structure->patterns.rend(),
                [](const ChartPattern& pattern) {
                    return pattern.direction ==
                               PatternDirection::Bearish &&
                           (pattern.status ==
                                PatternStatus::Confirmed ||
                            pattern.status ==
                                PatternStatus::Emerging);
                });
            if (bearish != structure->patterns.rend()) {
                category.adverse(
                    bearish->status == PatternStatus::Confirmed ? 5 : 3,
                    QStringLiteral("%1 is %2")
                        .arg(
                            patternKindLabel(bearish->kind),
                            patternStatusLabel(bearish->status)
                                .toLower()),
                    patternDirectionLabel(bearish->direction),
                    bearish->explanation,
                    QStringLiteral("Market Structure"));
            }
            const auto bullish = std::ranges::find_if(
                structure->patterns.rbegin(),
                structure->patterns.rend(),
                [](const ChartPattern& pattern) {
                    return pattern.direction ==
                               PatternDirection::Bullish &&
                           (pattern.status ==
                                PatternStatus::Confirmed ||
                            pattern.status ==
                                PatternStatus::Emerging);
                });
            if (bullish != structure->patterns.rend()) {
                category.constructive(
                    QStringLiteral("%1 is %2")
                        .arg(
                            patternKindLabel(bullish->kind),
                            patternStatusLabel(bullish->status)
                                .toLower()),
                    patternDirectionLabel(bullish->direction),
                    bullish->explanation,
                    QStringLiteral("Market Structure"));
            }
        }
        if (category.used == 0) {
            category.constructive(
                QStringLiteral("No selected bearish candle pattern"),
                QStringLiteral("none"),
                QStringLiteral("The latest candle did not trigger the analyzer's rejection patterns."),
                QStringLiteral("Latest completed daily candles"));
        }
        finishCategory(result, category, candleBit, category.cap);
    }

    if (securityEnd >= 59) {
        CategoryAccumulator category{
            RiskCategory::VolatilityLiquidity, 15, 0, date, result.evidence};
        const auto atr = *averageTrueRangePercent(security, securityEnd);
        const auto percentile = atrPercentile(security, securityEnd);
        if (percentile && *percentile >= 80.0) {
            category.adverse(
                8,
                QStringLiteral("ATR is in a high historical percentile"),
                number(*percentile, 0, QStringLiteral("th percentile")),
                QStringLiteral("Recent daily ranges are unusually large relative to available history."),
                QStringLiteral("ATR(14) / close"));
        } else if (percentile && *percentile >= 60.0) {
            category.adverse(
                4,
                QStringLiteral("ATR is above its historical median"),
                number(*percentile, 0, QStringLiteral("th percentile")),
                QStringLiteral("Recent daily ranges are elevated relative to available history."),
                QStringLiteral("ATR(14) / close"));
        } else if (percentile) {
            category.constructive(
                QStringLiteral("ATR is not historically elevated"),
                QStringLiteral("%1; %2")
                    .arg(
                        number(atr, 2, QStringLiteral("%")),
                        number(*percentile, 0, QStringLiteral("th percentile"))),
                QStringLiteral("Recent ranges are below the analyzer's elevated-volatility zone."),
                QStringLiteral("ATR(14) / close"));
        }
        const auto gaps = averageGapPercent(security, securityEnd);
        if (gaps >= 2.0) {
            category.adverse(
                4,
                QStringLiteral("Large average opening gaps"),
                number(gaps, 2, QStringLiteral("%")),
                QStringLiteral("Twenty-day opening gaps increase entry and stop-execution uncertainty."),
                input.symbol);
        }
        const auto dollarVolume = averageDollarVolume(security, securityEnd);
        if (dollarVolume < 5'000'000.0) {
            category.adverse(
                5,
                QStringLiteral("Low estimated dollar volume"),
                number(dollarVolume / 1'000'000.0, 2, QStringLiteral("M/day")),
                QStringLiteral("Close times reported volume suggests materially limited liquidity."),
                QStringLiteral("20-day close × volume"));
        } else if (dollarVolume < 20'000'000.0) {
            category.adverse(
                3,
                QStringLiteral("Limited estimated dollar volume"),
                number(dollarVolume / 1'000'000.0, 2, QStringLiteral("M/day")),
                QStringLiteral("Close times reported volume suggests moderate liquidity constraints."),
                QStringLiteral("20-day close × volume"));
        } else {
            category.constructive(
                QStringLiteral("Estimated dollar volume above threshold"),
                number(dollarVolume / 1'000'000.0, 2, QStringLiteral("M/day")),
                QStringLiteral("Reported volume does not trigger the analyzer's liquidity threshold."),
                QStringLiteral("20-day close × volume"));
        }
        finishCategory(result, category, volatilityBit, category.cap);
    }

    if (securityEnd >= 60) {
        CategoryAccumulator category{
            RiskCategory::PriceLocation, 10, 0, date, result.evidence};
        const auto close = security[securityEnd].close;
        const auto sma20 = *movingAverage(security, securityEnd, 20);
        const auto currentRsi = *rsi(security, securityEnd);
        auto priorHigh = security[securityEnd - 60].high;
        for (auto index = securityEnd - 59; index < securityEnd; ++index) {
            priorHigh = std::max(priorHigh, security[index].high);
        }
        const auto extension = ((close / sma20) - 1.0) * 100.0;
        if (extension >= 8.0) {
            category.adverse(
                6,
                QStringLiteral("Price extended above 20-day average"),
                number(extension, 2, QStringLiteral("%")),
                QStringLiteral("A stretched entry has less room before normal mean reversion."),
                QStringLiteral("SMA(20)"));
        }
        const auto distanceToHigh = ((close / priorHigh) - 1.0) * 100.0;
        if (distanceToHigh >= -3.0 && currentRsi >= 70.0) {
            category.adverse(
                4,
                QStringLiteral("Extended near 60-day resistance"),
                number(distanceToHigh, 2, QStringLiteral("% from prior high")),
                QStringLiteral("Price is near prior resistance while momentum is overbought."),
                QStringLiteral("60-day high and RSI(14)"));
        }
        if (structure) {
            const auto resistance = std::ranges::find_if(
                structure->zones,
                [](const StructureZone& zone) {
                    return !zone.broken &&
                           zone.type ==
                               StructureZoneType::Resistance &&
                           zone.distanceFromClosePercent >= 0.0;
                });
            if (resistance != structure->zones.end() &&
                resistance->distanceFromClosePercent <= 3.0) {
                category.adverse(
                    4,
                    QStringLiteral("Confirmed resistance zone is nearby"),
                    number(
                        resistance->distanceFromClosePercent,
                        2,
                        QStringLiteral("% above")),
                    resistance->explanation,
                    QStringLiteral("Market Structure"));
            }
            const auto support = std::ranges::find_if(
                structure->zones,
                [](const StructureZone& zone) {
                    return !zone.broken &&
                           zone.type ==
                               StructureZoneType::Support &&
                           zone.distanceFromClosePercent <= 0.0;
                });
            if (support != structure->zones.end() &&
                support->distanceFromClosePercent >= -3.0) {
                category.constructive(
                    QStringLiteral("Confirmed support zone is nearby"),
                    number(
                        -support->distanceFromClosePercent,
                        2,
                        QStringLiteral("% below")),
                    support->explanation,
                    QStringLiteral("Market Structure"));
            }
        }
        if (category.used == 0) {
            category.constructive(
                QStringLiteral("No selected location extension"),
                number(extension, 2, QStringLiteral("% vs SMA(20)")),
                QStringLiteral("Price location does not trigger the analyzer's extension thresholds."),
                QStringLiteral("SMA(20) / 60-day high"));
        }
        finishCategory(result, category, locationBit, category.cap);
    }

    auto eventCoverage =
        currentEvaluation && input.eventCalendarCoverageKnown;
    for (const auto& event : input.events) {
        if (relevantEvent(event, input.symbol) &&
            eventKnownBy(event, date) &&
            event.scheduledDate >= date.addDays(-30) &&
            event.scheduledDate <= date.addDays(90)) {
            eventCoverage = true;
            break;
        }
    }
    if (eventCoverage) {
        CategoryAccumulator category{
            RiskCategory::EventRisk, 10, 0, date, result.evidence};
        const ResearchEvent* nearestEarnings = nullptr;
        const ResearchEvent* nearestMarketEvent = nullptr;
        for (const auto& event : input.events) {
            if (!relevantEvent(event, input.symbol) ||
                !eventKnownBy(event, date)) {
                continue;
            }
            const auto days = date.daysTo(event.scheduledDate);
            if (days < 0 || days > 14) {
                continue;
            }
            if (event.type == ResearchEventType::Earnings) {
                if (!nearestEarnings ||
                    event.scheduledDate < nearestEarnings->scheduledDate) {
                    nearestEarnings = &event;
                }
            } else if (
                (event.type == ResearchEventType::EconomicRelease ||
                 event.type == ResearchEventType::CentralBank ||
                 event.type == ResearchEventType::OptionsExpiration) &&
                days <= 3) {
                if (!nearestMarketEvent ||
                    event.scheduledDate <
                        nearestMarketEvent->scheduledDate) {
                    nearestMarketEvent = &event;
                }
            }
        }
        if (nearestEarnings) {
            const auto days = date.daysTo(nearestEarnings->scheduledDate);
            category.adverse(
                days <= 5 ? 10 : 6,
                QStringLiteral("Known earnings event is near"),
                QStringLiteral("%1 in %2 calendar day(s)")
                    .arg(nearestEarnings->scheduledDate.toString(Qt::ISODate))
                    .arg(days),
                QStringLiteral(
                    "Earnings can create gap risk beyond normal chart ranges."),
                nearestEarnings->source);
        }
        if (nearestMarketEvent) {
            const auto days =
                date.daysTo(nearestMarketEvent->scheduledDate);
            category.adverse(
                3,
                QStringLiteral("Known market event is near"),
                QStringLiteral("%1 in %2 calendar day(s)")
                    .arg(
                        nearestMarketEvent->scheduledDate.toString(
                            Qt::ISODate))
                    .arg(days),
                QStringLiteral(
                    "The recorded market event may increase broad volatility."),
                nearestMarketEvent->source);
        }
        if (category.used == 0) {
            category.constructive(
                QStringLiteral("No recorded near-term event trigger"),
                QStringLiteral("next 14 calendar days"),
                QStringLiteral("The available calendar contains no event that triggers this model."),
                QStringLiteral("Local research calendar"));
        }
        finishCategory(result, category, eventBit, category.cap);
    } else {
        result.missingInputs.push_back(
            QStringLiteral("Event-calendar coverage is unknown."));
    }

    const auto snapshot =
        snapshotAt(input.fundamentals, input.symbol, date);
    if (snapshot) {
        CategoryAccumulator category{
            RiskCategory::Fundamentals, 10, 0, date, result.evidence};
        auto available = 0;
        const auto& metrics = snapshot->derived;
        if (metrics.revenueGrowthYoYPercent) {
            available += 3;
            if (*metrics.revenueGrowthYoYPercent < -10.0) {
                category.adverse(
                    3,
                    QStringLiteral("Revenue is contracting materially"),
                    number(*metrics.revenueGrowthYoYPercent, 2, QStringLiteral("% YoY")),
                    QStringLiteral("Point-in-time reported revenue growth is below -10%."),
                    QStringLiteral("SEC filing"));
            } else if (*metrics.revenueGrowthYoYPercent < 0.0) {
                category.adverse(
                    2,
                    QStringLiteral("Revenue is contracting"),
                    number(*metrics.revenueGrowthYoYPercent, 2, QStringLiteral("% YoY")),
                    QStringLiteral("Point-in-time reported revenue growth is negative."),
                    QStringLiteral("SEC filing"));
            } else {
                category.constructive(
                    QStringLiteral("Revenue growth is non-negative"),
                    number(*metrics.revenueGrowthYoYPercent, 2, QStringLiteral("% YoY")),
                    QStringLiteral("The latest point-in-time revenue comparison is constructive."),
                    QStringLiteral("SEC filing"));
            }
        }
        if (metrics.operatingMarginPercent) {
            available += 2;
            if (*metrics.operatingMarginPercent < 0.0) {
                category.adverse(
                    2,
                    QStringLiteral("Operating margin is negative"),
                    number(*metrics.operatingMarginPercent, 2, QStringLiteral("%")),
                    QStringLiteral("The point-in-time operating result is loss-making."),
                    QStringLiteral("SEC filing"));
            }
        }
        if (metrics.freeCashFlow) {
            available += 2;
            if (*metrics.freeCashFlow < 0.0) {
                category.adverse(
                    2,
                    QStringLiteral("Free cash flow is negative"),
                    number(*metrics.freeCashFlow / 1'000'000.0, 2, QStringLiteral("M")),
                    QStringLiteral("Reported operating cash flow minus capital expenditure is negative."),
                    QStringLiteral("SEC filing"));
            } else {
                category.constructive(
                    QStringLiteral("Free cash flow is positive"),
                    number(*metrics.freeCashFlow / 1'000'000.0, 2, QStringLiteral("M")),
                    QStringLiteral("Point-in-time derived free cash flow is positive."),
                    QStringLiteral("SEC filing"));
            }
        }
        if (metrics.debtToEquity) {
            available += 2;
            if (*metrics.debtToEquity > 2.0) {
                category.adverse(
                    2,
                    QStringLiteral("Debt-to-equity is high"),
                    number(*metrics.debtToEquity, 2, QStringLiteral("×")),
                    QStringLiteral("Reported debt exceeds two times reported equity."),
                    QStringLiteral("SEC filing"));
            }
        }
        if (metrics.epsGrowthYoYPercent) {
            available += 1;
            if (*metrics.epsGrowthYoYPercent < 0.0) {
                category.adverse(
                    1,
                    QStringLiteral("EPS growth is negative"),
                    number(*metrics.epsGrowthYoYPercent, 2, QStringLiteral("% YoY")),
                    QStringLiteral("Point-in-time diluted EPS growth is negative."),
                    QStringLiteral("SEC filing"));
            }
        }
        if (available > 0) {
            finishCategory(result, category, fundamentalBit, available);
        } else {
            result.missingInputs.push_back(
                QStringLiteral("SEC facts exist, but selected derived metrics are unavailable."));
        }
    } else {
        result.missingInputs.push_back(
            QStringLiteral("Point-in-time SEC fundamentals are unavailable."));
    }

    if (result.availableWeight > 0) {
        result.score = std::clamp(
            static_cast<int>(std::lround(
                static_cast<double>(result.adversePoints) * 100.0 /
                static_cast<double>(result.availableWeight))),
            0,
            100);
        result.level = levelForScore(result.score);
    }
    return result;
}

void calculateHistoricalValidation(
    RiskContextReport& report,
    const RiskContextInput& input,
    const Bars& security,
    const Bars& benchmark,
    const ScoreResult& current) {
    if (!input.includeHistoricalValidation ||
        security.size() <= kForwardTwenty + kMinimumBars) {
        report.historical.note =
            QStringLiteral("Insufficient history for comparable 20-day outcomes.");
        return;
    }

    std::vector<double> forward5;
    std::vector<double> forward20;
    std::vector<double> drawdowns;
    std::vector<double> relative20;
    const auto lastEligible = security.size() - 1 - kForwardTwenty;
    const auto firstEligible = lastEligible + 1 > kMaximumHistoricalDates
                                   ? lastEligible + 1 - kMaximumHistoricalDates
                                   : kMinimumBars - 1;

    for (auto index = std::max(kMinimumBars - 1, firstEligible);
         index <= lastEligible;
         ++index) {
        const auto historical =
            scoreAt(input, security, index, benchmark, false);
        if (historical.level != current.level ||
            historical.availableMask != current.availableMask ||
            historical.availableWeight != current.availableWeight ||
            std::abs(historical.score - current.score) > 10) {
            continue;
        }
        const auto base = security[index].close;
        forward5.push_back(
            ((security[index + kForwardFive].close / base) - 1.0) * 100.0);
        forward20.push_back(
            ((security[index + kForwardTwenty].close / base) - 1.0) * 100.0);
        auto minimumReturn = 0.0;
        for (auto future = index + 1;
             future <= index + kForwardTwenty;
             ++future) {
            minimumReturn = std::min(
                minimumReturn,
                ((security[future].close / base) - 1.0) * 100.0);
        }
        drawdowns.push_back(minimumReturn);

        const auto benchmarkStart =
            benchmarkIndexAt(benchmark, security[index].timestamp);
        const auto benchmarkEnd = benchmarkIndexAt(
            benchmark,
            security[index + kForwardTwenty].timestamp);
        if (benchmarkStart != benchmark.size() &&
            benchmarkEnd != benchmark.size() &&
            benchmark[benchmarkStart].timestamp == security[index].timestamp &&
            benchmark[benchmarkEnd].timestamp ==
                security[index + kForwardTwenty].timestamp) {
            const auto benchmarkReturn =
                ((benchmark[benchmarkEnd].close /
                  benchmark[benchmarkStart].close) -
                 1.0) *
                100.0;
            relative20.push_back(forward20.back() - benchmarkReturn);
        }
    }

    report.historical.comparableSetups = forward20.size();
    report.historical.medianForwardReturn5Percent = median(forward5);
    report.historical.medianForwardReturn20Percent = median(forward20);
    report.historical.medianMaximumDrawdown20Percent = median(drawdowns);
    report.historical.medianBenchmarkRelativeReturn20Percent =
        median(relative20);
    if (!drawdowns.empty()) {
        report.historical.worstMaximumDrawdown20Percent =
            *std::ranges::min_element(drawdowns);
    }
    if (!forward20.empty()) {
        const auto negative = std::ranges::count_if(
            forward20,
            [](const double value) { return value < 0.0; });
        report.historical.negativeForwardReturn20Percent =
            static_cast<double>(negative) * 100.0 /
            static_cast<double>(forward20.size());
    }
    report.historical.sampleAdequate =
        report.historical.comparableSetups >= 30;
    report.historical.note =
        report.historical.sampleAdequate
            ? QStringLiteral(
                  "Descriptive outcomes for matching historical setups; not a forecast.")
            : QStringLiteral(
                  "Fewer than 30 comparable setups: descriptive only, not a probability estimate.");
}

} // namespace

QString riskLevelLabel(const RiskLevel level) {
    switch (level) {
    case RiskLevel::Unavailable:
        return QStringLiteral("Unavailable");
    case RiskLevel::Lower:
        return QStringLiteral("Lower observed risk");
    case RiskLevel::Moderate:
        return QStringLiteral("Moderate observed risk");
    case RiskLevel::Elevated:
        return QStringLiteral("Elevated observed risk");
    case RiskLevel::High:
        return QStringLiteral("High observed risk");
    }
    return QStringLiteral("Unavailable");
}

QString riskCategoryLabel(const RiskCategory category) {
    switch (category) {
    case RiskCategory::MarketRegime:
        return QStringLiteral("Market regime");
    case RiskCategory::TrendMomentum:
        return QStringLiteral("Trend / momentum");
    case RiskCategory::PriceAction:
        return QStringLiteral("Price action");
    case RiskCategory::VolatilityLiquidity:
        return QStringLiteral("Volatility / liquidity");
    case RiskCategory::PriceLocation:
        return QStringLiteral("Price location");
    case RiskCategory::EventRisk:
        return QStringLiteral("Known events");
    case RiskCategory::Fundamentals:
        return QStringLiteral("Fundamentals");
    }
    return QStringLiteral("Unknown");
}

RiskContextReport analyzeRiskContext(const RiskContextInput& input) {
    RiskContextReport report{
        .symbol = normalizeWatchlistSymbol(input.symbol),
        .benchmarkSymbol = normalizeWatchlistSymbol(input.benchmarkSymbol),
    };
    if (report.benchmarkSymbol.isEmpty()) {
        report.benchmarkSymbol = QStringLiteral("SPY");
    }
    if (report.symbol.isEmpty()) {
        report.error = QStringLiteral("A valid security symbol is required.");
        return report;
    }

    Bars security = input.securityBars;
    if (input.analysisThroughDate.isValid()) {
        const auto cutoff = QDateTime(
                                input.analysisThroughDate.addDays(1),
                                QTime(0, 0),
                                QTimeZone::UTC)
                                .toSecsSinceEpoch();
        std::erase_if(
            security,
            [cutoff](const Bar& bar) { return bar.timestamp >= cutoff; });
    }
    if (const auto error = validateBars(security)) {
        report.error =
            QStringLiteral("Security daily history is invalid: %1")
                .arg(QString::fromStdString(*error));
        return report;
    }
    if (security.size() < kMinimumBars) {
        report.error = input.analysisThroughDate.isValid()
                           ? QStringLiteral(
                                 "At least 60 completed daily security bars are required through the selected date.")
                           : QStringLiteral(
                                 "At least 60 completed daily security bars are required.");
        return report;
    }
    if (security.size() > kMaximumBars) {
        security.erase(
            security.begin(),
            security.end() - static_cast<std::ptrdiff_t>(kMaximumBars));
        report.warnings.push_back(
            QStringLiteral("Analysis was bounded to the latest 2,500 daily bars."));
    }
    Bars benchmark = input.benchmarkBars;
    if (input.analysisThroughDate.isValid()) {
        const auto cutoff = QDateTime(
                                input.analysisThroughDate.addDays(1),
                                QTime(0, 0),
                                QTimeZone::UTC)
                                .toSecsSinceEpoch();
        std::erase_if(
            benchmark,
            [cutoff](const Bar& bar) { return bar.timestamp >= cutoff; });
    }
    if (!benchmark.empty()) {
        if (const auto error = validateBars(benchmark)) {
            report.warnings.push_back(
                QStringLiteral("Benchmark history is invalid and was ignored: %1")
                    .arg(QString::fromStdString(*error)));
            benchmark.clear();
        }
    }
    if (!benchmark.empty()) {
        if (benchmark.size() > kMaximumBars) {
            benchmark.erase(
                benchmark.begin(),
                benchmark.end() - static_cast<std::ptrdiff_t>(kMaximumBars));
        }
    }

    const auto current = scoreAt(
        input,
        security,
        security.size() - 1,
        benchmark,
        true);
    report.level = current.level;
    report.score = current.score;
    report.adversePoints = current.adversePoints;
    report.availableWeight = current.availableWeight;
    report.coveragePercent =
        static_cast<double>(current.availableWeight);
    report.evidence = current.evidence;
    report.missingInputs = current.missingInputs;
    report.securityAsOfDate = utcDate(security.back().timestamp);
    if (!benchmark.empty()) {
        const auto benchmarkEnd =
            benchmarkIndexAt(benchmark, security.back().timestamp);
        if (benchmarkEnd != benchmark.size()) {
            report.benchmarkAsOfDate = utcDate(benchmark[benchmarkEnd].timestamp);
        }
    }

    if (input.securityQuality.grade == DataQualityGrade::Poor) {
        report.warnings.push_back(
            QStringLiteral("Security data quality is Poor; interpret the score cautiously."));
    } else if (input.securityQuality.grade == DataQualityGrade::Caution) {
        report.warnings.push_back(
            QStringLiteral("Security data quality has Caution findings."));
    }
    if (!benchmark.empty()) {
        if (input.benchmarkQuality.grade == DataQualityGrade::Poor) {
            report.warnings.push_back(
                QStringLiteral("Benchmark data quality is Poor."));
        } else if (
            input.benchmarkQuality.grade == DataQualityGrade::Caution) {
            report.warnings.push_back(
                QStringLiteral("Benchmark data quality has Caution findings."));
        }
    }
    const auto stale =
        input.observationDate.isValid() &&
        report.securityAsOfDate.daysTo(input.observationDate) > 7;
    if (stale) {
        report.warnings.push_back(
            QStringLiteral("Security history is stale: latest bar is %1.")
                .arg(report.securityAsOfDate.toString(Qt::ISODate)));
    }

    calculateHistoricalValidation(
        report,
        input,
        security,
        benchmark,
        current);

    if (input.securityQuality.grade == DataQualityGrade::Poor ||
        (!benchmark.empty() &&
         input.benchmarkQuality.grade == DataQualityGrade::Poor) ||
        stale ||
        report.coveragePercent < 60.0 ||
        !report.historical.sampleAdequate) {
        report.confidence = QStringLiteral("Low");
    } else if (
        report.coveragePercent >= 80.0 &&
        report.historical.comparableSetups >= 50 &&
        input.securityQuality.grade == DataQualityGrade::Good &&
        (benchmark.empty() ||
         input.benchmarkQuality.grade == DataQualityGrade::Good)) {
        report.confidence = QStringLiteral("High");
    } else {
        report.confidence = QStringLiteral("Medium");
    }
    return report;
}

} // namespace tvchart
