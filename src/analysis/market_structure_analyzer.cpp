#include "analysis/market_structure_analyzer.hpp"

#include "watchlists/watchlist_workspace.hpp"

#include <QDate>
#include <QDateTime>
#include <QTimeZone>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <map>
#include <numeric>
#include <ranges>
#include <set>
#include <tuple>
#include <utility>

namespace tvchart {
namespace {

constexpr auto kMinimumBars = std::size_t{30};
constexpr auto kForwardFive = std::size_t{5};
constexpr auto kForwardTwenty = std::size_t{20};

struct Boundary {
    ConfirmedPivot first;
    ConfirmedPivot second;
    double slope{};

    [[nodiscard]] double at(const std::size_t index) const {
        return first.price +
               slope *
                   (static_cast<double>(index) -
                    static_cast<double>(first.anchorIndex));
    }
};

struct ZoneCluster {
    StructureZoneType type{StructureZoneType::Support};
    std::vector<ConfirmedPivot> pivots;
    double center{};
};

[[nodiscard]] std::vector<double> atrSeries(const Bars& bars) {
    std::vector<double> result(bars.size());
    auto smoothed = 0.0;
    for (auto index = std::size_t{}; index < bars.size(); ++index) {
        auto range = bars[index].high - bars[index].low;
        if (index > 0) {
            range = std::max(
                {range,
                 std::abs(bars[index].high - bars[index - 1].close),
                 std::abs(bars[index].low - bars[index - 1].close)});
        }
        if (index == 0) {
            smoothed = range;
        } else if (index < 14) {
            smoothed =
                (smoothed * static_cast<double>(index) + range) /
                static_cast<double>(index + 1);
        } else {
            smoothed =
                ((smoothed * 13.0) + range) / 14.0;
        }
        result[index] = std::max(
            smoothed,
            bars[index].close * 0.0001);
    }
    return result;
}

[[nodiscard]] std::vector<ConfirmedPivot> detectPivots(
    const Bars& bars,
    const std::vector<double>& atr,
    const MarketStructureSettings& settings) {
    std::vector<ConfirmedPivot> result;
    const auto strength =
        static_cast<std::size_t>(settings.pivotStrength);
    if (bars.size() <= strength * 2) {
        return result;
    }
    for (auto index = strength;
         index + strength < bars.size();
         ++index) {
        auto high = true;
        auto low = true;
        for (auto offset = std::size_t{1};
             offset <= strength;
             ++offset) {
            high = high &&
                   bars[index].high > bars[index - offset].high &&
                   bars[index].high > bars[index + offset].high;
            low = low &&
                  bars[index].low < bars[index - offset].low &&
                  bars[index].low < bars[index + offset].low;
        }
        if (high) {
            result.push_back({
                .type = PivotType::High,
                .anchorIndex = index,
                .confirmationIndex = index + strength,
                .anchorTimestamp = bars[index].timestamp,
                .confirmationTimestamp =
                    bars[index + strength].timestamp,
                .price = bars[index].high,
                .atr = atr[index + strength],
            });
        }
        if (low) {
            result.push_back({
                .type = PivotType::Low,
                .anchorIndex = index,
                .confirmationIndex = index + strength,
                .anchorTimestamp = bars[index].timestamp,
                .confirmationTimestamp =
                    bars[index + strength].timestamp,
                .price = bars[index].low,
                .atr = atr[index + strength],
            });
        }
    }
    std::ranges::sort(
        result,
        {},
        &ConfirmedPivot::confirmationIndex);
    if (result.size() > settings.maximumPivots) {
        result.erase(
            result.begin(),
            result.end() -
                static_cast<std::ptrdiff_t>(settings.maximumPivots));
    }
    return result;
}

[[nodiscard]] std::vector<ConfirmedPivot> pivotsOf(
    const std::vector<ConfirmedPivot>& pivots,
    const PivotType type) {
    std::vector<ConfirmedPivot> result;
    for (const auto& pivot : pivots) {
        if (pivot.type == type) {
            result.push_back(pivot);
        }
    }
    std::ranges::sort(result, {}, &ConfirmedPivot::anchorIndex);
    return result;
}

[[nodiscard]] std::optional<ConfirmedPivot> lowestBetween(
    const std::vector<ConfirmedPivot>& lows,
    const std::size_t first,
    const std::size_t second) {
    std::optional<ConfirmedPivot> result;
    for (const auto& pivot : lows) {
        if (pivot.anchorIndex <= first ||
            pivot.anchorIndex >= second) {
            continue;
        }
        if (!result || pivot.price < result->price) {
            result = pivot;
        }
    }
    return result;
}

[[nodiscard]] std::optional<ConfirmedPivot> highestBetween(
    const std::vector<ConfirmedPivot>& highs,
    const std::size_t first,
    const std::size_t second) {
    std::optional<ConfirmedPivot> result;
    for (const auto& pivot : highs) {
        if (pivot.anchorIndex <= first ||
            pivot.anchorIndex >= second) {
            continue;
        }
        if (!result || pivot.price > result->price) {
            result = pivot;
        }
    }
    return result;
}

[[nodiscard]] Boundary boundary(
    const ConfirmedPivot& first,
    const ConfirmedPivot& second) {
    const auto distance =
        std::max<std::size_t>(
            1,
            second.anchorIndex - first.anchorIndex);
    return {
        .first = first,
        .second = second,
        .slope =
            (second.price - first.price) /
            static_cast<double>(distance),
    };
}

[[nodiscard]] StructureLine lineFromBoundary(
    const Boundary& value,
    const Bars& bars,
    const std::size_t end,
    const StructureLineKind kind,
    QString title,
    QString color,
    const bool dashed) {
    return {
        .kind = kind,
        .startTimestamp = value.first.anchorTimestamp,
        .endTimestamp = bars[end].timestamp,
        .startPrice = value.first.price,
        .endPrice = value.at(end),
        .title = std::move(title),
        .color = std::move(color),
        .dashed = dashed,
    };
}

[[nodiscard]] QString patternId(
    const PatternKind kind,
    const std::vector<ConfirmedPivot>& anchors) {
    auto id = QStringLiteral("%1")
                  .arg(static_cast<int>(kind));
    for (const auto& anchor : anchors) {
        id += QStringLiteral("-%1").arg(anchor.anchorTimestamp);
    }
    return id;
}

void advanceDirectionalPattern(
    ChartPattern& pattern,
    const Bars& bars,
    const std::vector<double>& atr,
    const std::size_t firstEvaluation,
    const std::optional<Boundary>& breakoutBoundary,
    const std::optional<Boundary>& oppositeBoundary) {
    auto status = PatternStatus::Emerging;
    auto direction = pattern.direction;
    auto detectionIndex = pattern.formationIndex;
    auto statusIndex = pattern.formationIndex;
    for (auto index = firstEvaluation; index < bars.size(); ++index) {
        const auto close = bars[index].close;
        if (status == PatternStatus::Emerging) {
            if (direction == PatternDirection::Neutral &&
                breakoutBoundary && oppositeBoundary) {
                const auto upper =
                    std::max(
                        breakoutBoundary->at(index),
                        oppositeBoundary->at(index));
                const auto lower =
                    std::min(
                        breakoutBoundary->at(index),
                        oppositeBoundary->at(index));
                if (close > upper) {
                    direction = PatternDirection::Bullish;
                    status = PatternStatus::Confirmed;
                    detectionIndex = index;
                    statusIndex = index;
                    const auto height =
                        std::max(
                            atr[index] * 2.0,
                            upper - lower);
                    pattern.targetPrice = close + height;
                    pattern.invalidationPrice = lower;
                } else if (close < lower) {
                    direction = PatternDirection::Bearish;
                    status = PatternStatus::Confirmed;
                    detectionIndex = index;
                    statusIndex = index;
                    const auto height =
                        std::max(
                            atr[index] * 2.0,
                            upper - lower);
                    pattern.targetPrice =
                        std::max(0.0001, close - height);
                    pattern.invalidationPrice = upper;
                }
            } else if (
                direction == PatternDirection::Bullish &&
                breakoutBoundary &&
                close > breakoutBoundary->at(index)) {
                status = PatternStatus::Confirmed;
                statusIndex = index;
            } else if (
                direction == PatternDirection::Bearish &&
                breakoutBoundary &&
                close < breakoutBoundary->at(index)) {
                status = PatternStatus::Confirmed;
                statusIndex = index;
            } else if (
                pattern.invalidationPrice &&
                ((direction == PatternDirection::Bullish &&
                  close < *pattern.invalidationPrice) ||
                 (direction == PatternDirection::Bearish &&
                  close > *pattern.invalidationPrice))) {
                status = PatternStatus::Invalidated;
                statusIndex = index;
                break;
            }
        } else if (status == PatternStatus::Confirmed) {
            if (pattern.targetPrice &&
                ((direction == PatternDirection::Bullish &&
                  bars[index].high >= *pattern.targetPrice) ||
                 (direction == PatternDirection::Bearish &&
                  bars[index].low <= *pattern.targetPrice))) {
                status = PatternStatus::TargetReached;
                statusIndex = index;
                break;
            }
            if (pattern.invalidationPrice &&
                ((direction == PatternDirection::Bullish &&
                  close < *pattern.invalidationPrice) ||
                 (direction == PatternDirection::Bearish &&
                  close > *pattern.invalidationPrice))) {
                status = PatternStatus::Invalidated;
                statusIndex = index;
                break;
            }
        }
    }
    pattern.direction = direction;
    pattern.status = status;
    pattern.detectionIndex = detectionIndex;
    pattern.detectionTimestamp =
        bars[detectionIndex].timestamp;
    pattern.statusTimestamp = bars[statusIndex].timestamp;
}

void detectDoublePatterns(
    std::vector<ChartPattern>& patterns,
    const Bars& bars,
    const std::vector<double>& atr,
    const std::vector<ConfirmedPivot>& highs,
    const std::vector<ConfirmedPivot>& lows) {
    for (auto index = std::size_t{1}; index < highs.size(); ++index) {
        const auto& first = highs[index - 1];
        const auto& second = highs[index];
        const auto trough =
            lowestBetween(
                lows,
                first.anchorIndex,
                second.anchorIndex);
        if (!trough) {
            continue;
        }
        const auto tolerance =
            std::max(
                second.atr * 1.5,
                std::midpoint(first.price, second.price) * 0.02);
        const auto depth =
            std::min(first.price, second.price) - trough->price;
        if (std::abs(first.price - second.price) > tolerance ||
            depth < second.atr * 2.0) {
            continue;
        }
        ChartPattern pattern{
            .kind = PatternKind::DoubleTop,
            .direction = PatternDirection::Bearish,
            .formationIndex = std::max(
                {first.confirmationIndex,
                 second.confirmationIndex,
                 trough->confirmationIndex}),
            .referencePrice = trough->price,
            .targetPrice =
                std::max(0.0001, trough->price - depth),
            .invalidationPrice =
                std::max(first.price, second.price) +
                second.atr * 0.5,
            .anchors = {first, *trough, second},
            .explanation = QStringLiteral(
                "Two comparable confirmed highs with an intervening swing low."),
        };
        pattern.startTimestamp = first.anchorTimestamp;
        pattern.endTimestamp = second.anchorTimestamp;
        pattern.id = patternId(pattern.kind, pattern.anchors);
        const Boundary neckline{*trough, *trough, 0.0};
        pattern.boundaries.push_back({
            .kind = StructureLineKind::PatternBoundary,
            .startTimestamp = trough->anchorTimestamp,
            .endTimestamp = bars.back().timestamp,
            .startPrice = trough->price,
            .endPrice = trough->price,
            .title = QStringLiteral("Double-top neckline"),
            .color = QStringLiteral("#ef5350"),
            .dashed = true,
        });
        advanceDirectionalPattern(
            pattern,
            bars,
            atr,
            pattern.formationIndex,
            neckline,
            std::nullopt);
        patterns.push_back(std::move(pattern));
    }

    for (auto index = std::size_t{1}; index < lows.size(); ++index) {
        const auto& first = lows[index - 1];
        const auto& second = lows[index];
        const auto peak =
            highestBetween(
                highs,
                first.anchorIndex,
                second.anchorIndex);
        if (!peak) {
            continue;
        }
        const auto tolerance =
            std::max(
                second.atr * 1.5,
                std::midpoint(first.price, second.price) * 0.02);
        const auto height =
            peak->price - std::max(first.price, second.price);
        if (std::abs(first.price - second.price) > tolerance ||
            height < second.atr * 2.0) {
            continue;
        }
        ChartPattern pattern{
            .kind = PatternKind::DoubleBottom,
            .direction = PatternDirection::Bullish,
            .formationIndex = std::max(
                {first.confirmationIndex,
                 second.confirmationIndex,
                 peak->confirmationIndex}),
            .referencePrice = peak->price,
            .targetPrice = peak->price + height,
            .invalidationPrice =
                std::max(
                    0.0001,
                    std::min(first.price, second.price) -
                        second.atr * 0.5),
            .anchors = {first, *peak, second},
            .explanation = QStringLiteral(
                "Two comparable confirmed lows with an intervening swing high."),
        };
        pattern.startTimestamp = first.anchorTimestamp;
        pattern.endTimestamp = second.anchorTimestamp;
        pattern.id = patternId(pattern.kind, pattern.anchors);
        const Boundary neckline{*peak, *peak, 0.0};
        pattern.boundaries.push_back({
            .kind = StructureLineKind::PatternBoundary,
            .startTimestamp = peak->anchorTimestamp,
            .endTimestamp = bars.back().timestamp,
            .startPrice = peak->price,
            .endPrice = peak->price,
            .title = QStringLiteral("Double-bottom neckline"),
            .color = QStringLiteral("#26a69a"),
            .dashed = true,
        });
        advanceDirectionalPattern(
            pattern,
            bars,
            atr,
            pattern.formationIndex,
            neckline,
            std::nullopt);
        patterns.push_back(std::move(pattern));
    }
}

void detectRangePatterns(
    std::vector<ChartPattern>& patterns,
    const Bars& bars,
    const std::vector<double>& atr,
    const std::vector<ConfirmedPivot>& highs,
    const std::vector<ConfirmedPivot>& lows) {
    for (auto highIndex = std::size_t{1};
         highIndex < highs.size();
         ++highIndex) {
        const auto highLine =
            boundary(highs[highIndex - 1], highs[highIndex]);
        for (auto lowIndex = std::size_t{1};
             lowIndex < lows.size();
             ++lowIndex) {
            const auto lowLine =
                boundary(lows[lowIndex - 1], lows[lowIndex]);
            const auto firstAnchor =
                std::min(
                    highLine.first.anchorIndex,
                    lowLine.first.anchorIndex);
            const auto lastAnchor =
                std::max(
                    highLine.second.anchorIndex,
                    lowLine.second.anchorIndex);
            if (lastAnchor <= firstAnchor ||
                lastAnchor - firstAnchor > 120 ||
                std::max(
                    highLine.first.anchorIndex,
                    lowLine.first.anchorIndex) >=
                    std::min(
                        highLine.second.anchorIndex,
                        lowLine.second.anchorIndex)) {
                continue;
            }
            const auto upper = highLine.at(lastAnchor);
            const auto lower = lowLine.at(lastAnchor);
            const auto width = upper - lower;
            const auto localAtr = atr[lastAnchor];
            if (width < localAtr * 1.5) {
                continue;
            }
            const auto span =
                static_cast<double>(lastAnchor - firstAnchor);
            const auto highMove =
                std::abs(highLine.slope * span);
            const auto lowMove =
                std::abs(lowLine.slope * span);
            const auto flatTolerance =
                std::max(localAtr * 1.25, upper * 0.01);

            std::optional<PatternKind> kind;
            PatternDirection direction{PatternDirection::Neutral};
            if (highMove <= flatTolerance &&
                lowMove <= flatTolerance) {
                kind = PatternKind::Rectangle;
            } else if (
                highLine.slope <= localAtr * 0.01 &&
                lowLine.slope >= -localAtr * 0.01 &&
                highLine.slope < lowLine.slope) {
                kind = PatternKind::Triangle;
            } else if (
                highLine.slope > 0.0 &&
                lowLine.slope > highLine.slope) {
                kind = PatternKind::RisingWedge;
                direction = PatternDirection::Bearish;
            } else if (
                highLine.slope < lowLine.slope &&
                lowLine.slope < 0.0) {
                kind = PatternKind::FallingWedge;
                direction = PatternDirection::Bullish;
            }
            if (!kind) {
                continue;
            }
            const auto startWidth =
                highLine.at(firstAnchor) -
                lowLine.at(firstAnchor);
            if (*kind != PatternKind::Rectangle &&
                (startWidth <= width || startWidth <= 0.0)) {
                continue;
            }
            std::vector<ConfirmedPivot> anchors{
                highLine.first,
                lowLine.first,
                highLine.second,
                lowLine.second,
            };
            std::ranges::sort(
                anchors,
                {},
                &ConfirmedPivot::anchorIndex);
            ChartPattern pattern{
                .kind = *kind,
                .direction = direction,
                .formationIndex = std::max(
                    highLine.second.confirmationIndex,
                    lowLine.second.confirmationIndex),
                .referencePrice = std::midpoint(upper, lower),
                .anchors = anchors,
                .explanation =
                    *kind == PatternKind::Rectangle
                        ? QStringLiteral(
                              "Comparable confirmed high and low boundaries form a range.")
                        : QStringLiteral(
                              "Confirmed high and low boundaries converge."),
            };
            pattern.startTimestamp = bars[firstAnchor].timestamp;
            pattern.endTimestamp = bars[lastAnchor].timestamp;
            pattern.id = patternId(pattern.kind, pattern.anchors);
            pattern.boundaries = {
                lineFromBoundary(
                    highLine,
                    bars,
                    bars.size() - 1,
                    StructureLineKind::PatternBoundary,
                    QStringLiteral("Pattern upper"),
                    QStringLiteral("#ff9800"),
                    true),
                lineFromBoundary(
                    lowLine,
                    bars,
                    bars.size() - 1,
                    StructureLineKind::PatternBoundary,
                    QStringLiteral("Pattern lower"),
                    QStringLiteral("#ff9800"),
                    true),
            };
            if (direction == PatternDirection::Bullish) {
                pattern.invalidationPrice =
                    std::max(0.0001, lower - localAtr);
                pattern.targetPrice = upper + startWidth;
                advanceDirectionalPattern(
                    pattern,
                    bars,
                    atr,
                    pattern.formationIndex,
                    highLine,
                    std::nullopt);
            } else if (direction == PatternDirection::Bearish) {
                pattern.invalidationPrice = upper + localAtr;
                pattern.targetPrice =
                    std::max(0.0001, lower - startWidth);
                advanceDirectionalPattern(
                    pattern,
                    bars,
                    atr,
                    pattern.formationIndex,
                    lowLine,
                    std::nullopt);
            } else {
                advanceDirectionalPattern(
                    pattern,
                    bars,
                    atr,
                    pattern.formationIndex,
                    highLine,
                    lowLine);
            }
            patterns.push_back(std::move(pattern));
        }
    }
}

void detectHeadAndShoulders(
    std::vector<ChartPattern>& patterns,
    const Bars& bars,
    const std::vector<double>& atr,
    const std::vector<ConfirmedPivot>& highs,
    const std::vector<ConfirmedPivot>& lows) {
    for (auto index = std::size_t{2}; index < highs.size(); ++index) {
        const auto& left = highs[index - 2];
        const auto& head = highs[index - 1];
        const auto& right = highs[index];
        const auto leftNeck =
            lowestBetween(
                lows,
                left.anchorIndex,
                head.anchorIndex);
        const auto rightNeck =
            lowestBetween(
                lows,
                head.anchorIndex,
                right.anchorIndex);
        if (!leftNeck || !rightNeck) {
            continue;
        }
        const auto tolerance =
            std::max(right.atr * 1.5, head.price * 0.02);
        if (std::abs(left.price - right.price) > tolerance ||
            head.price - std::max(left.price, right.price) <
                right.atr) {
            continue;
        }
        const auto neckline = boundary(*leftNeck, *rightNeck);
        const auto neckAtRight = neckline.at(right.anchorIndex);
        const auto height = head.price - neckAtRight;
        ChartPattern pattern{
            .kind = PatternKind::HeadAndShoulders,
            .direction = PatternDirection::Bearish,
            .formationIndex = std::max(
                {left.confirmationIndex,
                 head.confirmationIndex,
                 right.confirmationIndex,
                 leftNeck->confirmationIndex,
                 rightNeck->confirmationIndex}),
            .referencePrice = neckAtRight,
            .targetPrice =
                std::max(0.0001, neckAtRight - height),
            .invalidationPrice = head.price + right.atr * 0.5,
            .anchors = {
                left,
                *leftNeck,
                head,
                *rightNeck,
                right,
            },
            .explanation = QStringLiteral(
                "A higher confirmed head is separated by two comparable shoulders."),
        };
        pattern.startTimestamp = left.anchorTimestamp;
        pattern.endTimestamp = right.anchorTimestamp;
        pattern.id = patternId(pattern.kind, pattern.anchors);
        pattern.boundaries.push_back(
            lineFromBoundary(
                neckline,
                bars,
                bars.size() - 1,
                StructureLineKind::PatternBoundary,
                QStringLiteral("Head-and-shoulders neckline"),
                QStringLiteral("#ef5350"),
                true));
        advanceDirectionalPattern(
            pattern,
            bars,
            atr,
            pattern.formationIndex,
            neckline,
            std::nullopt);
        patterns.push_back(std::move(pattern));
    }

    for (auto index = std::size_t{2}; index < lows.size(); ++index) {
        const auto& left = lows[index - 2];
        const auto& head = lows[index - 1];
        const auto& right = lows[index];
        const auto leftNeck =
            highestBetween(
                highs,
                left.anchorIndex,
                head.anchorIndex);
        const auto rightNeck =
            highestBetween(
                highs,
                head.anchorIndex,
                right.anchorIndex);
        if (!leftNeck || !rightNeck) {
            continue;
        }
        const auto tolerance =
            std::max(right.atr * 1.5, std::abs(head.price) * 0.02);
        if (std::abs(left.price - right.price) > tolerance ||
            std::min(left.price, right.price) - head.price <
                right.atr) {
            continue;
        }
        const auto neckline = boundary(*leftNeck, *rightNeck);
        const auto neckAtRight = neckline.at(right.anchorIndex);
        const auto height = neckAtRight - head.price;
        ChartPattern pattern{
            .kind = PatternKind::InverseHeadAndShoulders,
            .direction = PatternDirection::Bullish,
            .formationIndex = std::max(
                {left.confirmationIndex,
                 head.confirmationIndex,
                 right.confirmationIndex,
                 leftNeck->confirmationIndex,
                 rightNeck->confirmationIndex}),
            .referencePrice = neckAtRight,
            .targetPrice = neckAtRight + height,
            .invalidationPrice =
                std::max(0.0001, head.price - right.atr * 0.5),
            .anchors = {
                left,
                *leftNeck,
                head,
                *rightNeck,
                right,
            },
            .explanation = QStringLiteral(
                "A lower confirmed head is separated by two comparable shoulders."),
        };
        pattern.startTimestamp = left.anchorTimestamp;
        pattern.endTimestamp = right.anchorTimestamp;
        pattern.id = patternId(pattern.kind, pattern.anchors);
        pattern.boundaries.push_back(
            lineFromBoundary(
                neckline,
                bars,
                bars.size() - 1,
                StructureLineKind::PatternBoundary,
                QStringLiteral("Inverse head-and-shoulders neckline"),
                QStringLiteral("#26a69a"),
                true));
        advanceDirectionalPattern(
            pattern,
            bars,
            atr,
            pattern.formationIndex,
            neckline,
            std::nullopt);
        patterns.push_back(std::move(pattern));
    }
}

[[nodiscard]] std::vector<ChartPattern> detectPatterns(
    const Bars& bars,
    const std::vector<double>& atr,
    const std::vector<ConfirmedPivot>& pivots) {
    const auto highs = pivotsOf(pivots, PivotType::High);
    const auto lows = pivotsOf(pivots, PivotType::Low);
    std::vector<ChartPattern> patterns;
    detectDoublePatterns(patterns, bars, atr, highs, lows);
    detectRangePatterns(patterns, bars, atr, highs, lows);
    detectHeadAndShoulders(patterns, bars, atr, highs, lows);

    std::ranges::sort(
        patterns,
        [](const ChartPattern& left, const ChartPattern& right) {
            return std::tie(
                       left.detectionIndex,
                       left.id) <
                   std::tie(
                       right.detectionIndex,
                       right.id);
        });
    std::set<QString> identities;
    std::erase_if(
        patterns,
        [&identities](const ChartPattern& pattern) {
            return !identities.insert(pattern.id).second;
        });
    constexpr auto maximumValidationPatterns =
        std::size_t{256};
    if (patterns.size() > maximumValidationPatterns) {
        patterns.erase(
            patterns.begin(),
            patterns.end() -
                static_cast<std::ptrdiff_t>(
                    maximumValidationPatterns));
    }
    return patterns;
}

[[nodiscard]] std::vector<StructureZone> buildZones(
    const Bars& bars,
    const std::vector<double>& atr,
    const std::vector<ConfirmedPivot>& pivots,
    const MarketStructureSettings& settings) {
    std::vector<ZoneCluster> clusters;
    for (const auto& pivot : pivots) {
        const auto type =
            pivot.type == PivotType::Low
                ? StructureZoneType::Support
                : StructureZoneType::Resistance;
        const auto threshold =
            std::max(
                pivot.atr * settings.zoneAtrMultiplier,
                pivot.price * 0.0015);
        auto nearest = clusters.end();
        auto nearestDistance =
            std::numeric_limits<double>::max();
        for (auto candidate = clusters.begin();
             candidate != clusters.end();
             ++candidate) {
            const auto distance =
                std::abs(candidate->center - pivot.price);
            if (candidate->type == type &&
                distance <= threshold &&
                distance < nearestDistance) {
                nearest = candidate;
                nearestDistance = distance;
            }
        }
        if (nearest == clusters.end()) {
            clusters.push_back({
                .type = type,
                .pivots = {pivot},
                .center = pivot.price,
            });
        } else {
            nearest->pivots.push_back(pivot);
            nearest->center =
                std::accumulate(
                    nearest->pivots.begin(),
                    nearest->pivots.end(),
                    0.0,
                    [](const double total, const ConfirmedPivot& item) {
                        return total + item.price;
                    }) /
                static_cast<double>(nearest->pivots.size());
        }
    }

    std::vector<StructureZone> zones;
    const auto latestIndex = bars.size() - 1;
    for (const auto& cluster : clusters) {
        if (cluster.pivots.size() <
            static_cast<std::size_t>(
                settings.minimumZoneTouches)) {
            continue;
        }
        const auto minimum = std::ranges::min_element(
            cluster.pivots,
            {},
            &ConfirmedPivot::price)->price;
        const auto maximum = std::ranges::max_element(
            cluster.pivots,
            {},
            &ConfirmedPivot::price)->price;
        const auto averageAtr =
            std::accumulate(
                cluster.pivots.begin(),
                cluster.pivots.end(),
                0.0,
                [](const double total, const ConfirmedPivot& item) {
                    return total + item.atr;
                }) /
            static_cast<double>(cluster.pivots.size());
        StructureZone zone{
            .type = cluster.type,
            .low = std::max(0.0001, minimum - averageAtr * 0.25),
            .high = maximum + averageAtr * 0.25,
            .center = cluster.center,
            .touches = static_cast<int>(cluster.pivots.size()),
            .firstAnchorTimestamp =
                cluster.pivots.front().anchorTimestamp,
            .lastAnchorTimestamp =
                cluster.pivots.back().anchorTimestamp,
            .detectionTimestamp =
                cluster.pivots.back().confirmationTimestamp,
        };
        const auto recencyBars =
            latestIndex -
            cluster.pivots.back().confirmationIndex;
        zone.strength = std::clamp(
            static_cast<double>(zone.touches * 18) +
                std::max(
                    0.0,
                    35.0 -
                        static_cast<double>(recencyBars) * 0.25),
            0.0,
            100.0);
        zone.distanceFromClosePercent =
            ((zone.center / bars.back().close) - 1.0) * 100.0;
        const auto firstEvaluation =
            cluster.pivots.back().confirmationIndex;
        for (auto index = firstEvaluation;
             index < bars.size();
             ++index) {
            const auto buffer = atr[index] * 0.5;
            if ((zone.type == StructureZoneType::Support &&
                 bars[index].close < zone.low - buffer) ||
                (zone.type == StructureZoneType::Resistance &&
                 bars[index].close > zone.high + buffer)) {
                zone.broken = true;
                zone.brokenTimestamp = bars[index].timestamp;
                break;
            }
        }
        zone.explanation =
            QStringLiteral(
                "%1 confirmed pivot touches; %2% from the latest close%3.")
                .arg(zone.touches)
                .arg(zone.distanceFromClosePercent, 0, 'f', 2)
                .arg(
                    zone.broken
                        ? QStringLiteral("; later broken on a completed close")
                        : QString{});
        zones.push_back(std::move(zone));
    }
    std::ranges::sort(
        zones,
        [](const StructureZone& left, const StructureZone& right) {
            return std::tuple{
                       left.broken,
                       std::abs(left.distanceFromClosePercent),
                       -left.strength} <
                   std::tuple{
                       right.broken,
                       std::abs(right.distanceFromClosePercent),
                       -right.strength};
        });
    if (zones.size() > settings.maximumZones) {
        zones.resize(settings.maximumZones);
    }
    return zones;
}

[[nodiscard]] StructureBias biasFromPivots(
    const std::vector<ConfirmedPivot>& pivots) {
    const auto highs = pivotsOf(pivots, PivotType::High);
    const auto lows = pivotsOf(pivots, PivotType::Low);
    if (highs.size() < 2 || lows.size() < 2) {
        return StructureBias::Unavailable;
    }
    const auto higherHigh =
        highs.back().price > highs[highs.size() - 2].price;
    const auto higherLow =
        lows.back().price > lows[lows.size() - 2].price;
    if (higherHigh && higherLow) {
        return StructureBias::Bullish;
    }
    if (!higherHigh && !higherLow) {
        return StructureBias::Bearish;
    }
    return StructureBias::Neutral;
}

[[nodiscard]] std::vector<StructureLine> buildTrendLines(
    const Bars& bars,
    const std::vector<ConfirmedPivot>& pivots) {
    std::vector<StructureLine> result;
    const auto highs = pivotsOf(pivots, PivotType::High);
    const auto lows = pivotsOf(pivots, PivotType::Low);
    if (highs.size() >= 2) {
        const auto value =
            boundary(
                highs[highs.size() - 2],
                highs.back());
        result.push_back(
            lineFromBoundary(
                value,
                bars,
                bars.size() - 1,
                StructureLineKind::Trendline,
                value.slope < 0.0
                    ? QStringLiteral("Falling resistance trendline")
                    : QStringLiteral("Rising high trendline"),
                QStringLiteral("#ef5350"),
                false));
    }
    if (lows.size() >= 2) {
        const auto value =
            boundary(
                lows[lows.size() - 2],
                lows.back());
        result.push_back(
            lineFromBoundary(
                value,
                bars,
                bars.size() - 1,
                StructureLineKind::Trendline,
                value.slope > 0.0
                    ? QStringLiteral("Rising support trendline")
                    : QStringLiteral("Falling low trendline"),
                QStringLiteral("#26a69a"),
                false));
    }
    if (highs.size() >= 2 && lows.size() >= 2) {
        const auto high =
            boundary(
                highs[highs.size() - 2],
                highs.back());
        const auto low =
            boundary(
                lows[lows.size() - 2],
                lows.back());
        const auto denominator =
            std::max(
                {std::abs(high.slope),
                 std::abs(low.slope),
                 0.000001});
        if (high.slope * low.slope > 0.0 &&
            std::abs(high.slope - low.slope) /
                    denominator <=
                0.5) {
            if (result.size() >= 2) {
                result[result.size() - 2].kind =
                    StructureLineKind::ChannelBoundary;
                result[result.size() - 2].dashed = true;
                result.back().kind =
                    StructureLineKind::ChannelBoundary;
                result.back().dashed = true;
            }
        }
    }
    return result;
}

[[nodiscard]] std::optional<double> median(
    std::vector<double> values) {
    if (values.empty()) {
        return std::nullopt;
    }
    std::ranges::sort(values);
    const auto middle = values.size() / 2;
    if (values.size() % 2 == 1) {
        return values[middle];
    }
    return std::midpoint(
        values[middle - 1],
        values[middle]);
}

[[nodiscard]] std::vector<PatternOutcomeSummary>
historicalOutcomes(
    const Bars& bars,
    const std::vector<ChartPattern>& patterns) {
    struct Accumulator {
        std::vector<double> returns5;
        std::vector<double> returns20;
        std::vector<double> adverse;
        std::size_t targetHits{};
        std::size_t invalidations{};
    };
    std::map<
        std::pair<PatternKind, PatternDirection>,
        Accumulator>
        groups;
    for (const auto& pattern : patterns) {
        if (pattern.direction == PatternDirection::Neutral ||
            pattern.detectionIndex + kForwardTwenty >= bars.size()) {
            continue;
        }
        const auto base = bars[pattern.detectionIndex].close;
        const auto sign =
            pattern.direction == PatternDirection::Bullish
                ? 1.0
                : -1.0;
        auto& group =
            groups[{pattern.kind, pattern.direction}];
        group.returns5.push_back(
            sign *
            ((bars[pattern.detectionIndex + kForwardFive].close /
              base) -
             1.0) *
            100.0);
        group.returns20.push_back(
            sign *
            ((bars[pattern.detectionIndex + kForwardTwenty].close /
              base) -
             1.0) *
            100.0);
        auto adverse = 0.0;
        auto targetHit = false;
        auto invalidated = false;
        for (auto index = pattern.detectionIndex + 1;
             index <= pattern.detectionIndex + kForwardTwenty;
             ++index) {
            if (pattern.direction == PatternDirection::Bullish) {
                adverse = std::min(
                    adverse,
                    ((bars[index].low / base) - 1.0) * 100.0);
                if (!targetHit && !invalidated &&
                    pattern.targetPrice &&
                    bars[index].high >= *pattern.targetPrice) {
                    targetHit = true;
                } else if (
                    !targetHit && !invalidated &&
                    pattern.invalidationPrice &&
                    bars[index].close <=
                        *pattern.invalidationPrice) {
                    invalidated = true;
                }
            } else {
                adverse = std::min(
                    adverse,
                    -((bars[index].high / base) - 1.0) * 100.0);
                if (!targetHit && !invalidated &&
                    pattern.targetPrice &&
                    bars[index].low <= *pattern.targetPrice) {
                    targetHit = true;
                } else if (
                    !targetHit && !invalidated &&
                    pattern.invalidationPrice &&
                    bars[index].close >=
                        *pattern.invalidationPrice) {
                    invalidated = true;
                }
            }
        }
        group.adverse.push_back(adverse);
        group.targetHits += targetHit ? 1 : 0;
        group.invalidations += invalidated ? 1 : 0;
    }

    std::vector<PatternOutcomeSummary> result;
    for (auto& [identity, values] : groups) {
        PatternOutcomeSummary summary{
            .kind = identity.first,
            .direction = identity.second,
            .samples = values.returns20.size(),
            .medianSignedReturn5Percent =
                median(std::move(values.returns5)),
            .medianSignedReturn20Percent =
                median(values.returns20),
            .medianMaximumAdverseExcursion20Percent =
                median(std::move(values.adverse)),
            .targetHits = values.targetHits,
            .invalidations = values.invalidations,
        };
        if (!values.returns20.empty()) {
            const auto positive =
                std::ranges::count_if(
                    values.returns20,
                    [](const double value) {
                        return value > 0.0;
                    });
            summary.positiveSignedReturn20Percent =
                static_cast<double>(positive) * 100.0 /
                static_cast<double>(values.returns20.size());
        }
        summary.sampleAdequate = summary.samples >= 30;
        summary.note =
            summary.sampleAdequate
                ? QStringLiteral(
                      "Descriptive detected-pattern outcomes; not a forecast.")
                : QStringLiteral(
                      "Fewer than 30 samples; descriptive only, not a probability.");
        result.push_back(std::move(summary));
    }
    return result;
}

[[nodiscard]] QString higherLabel(const Timeframe timeframe) {
    return timeframe == Timeframe::OneDay
               ? QStringLiteral("Completed ISO weeks")
               : QStringLiteral("Completed UTC days");
}

} // namespace

QString pivotTypeLabel(const PivotType type) {
    return type == PivotType::High
               ? QStringLiteral("Pivot high")
               : QStringLiteral("Pivot low");
}

QString structureZoneTypeLabel(const StructureZoneType type) {
    return type == StructureZoneType::Support
               ? QStringLiteral("Support")
               : QStringLiteral("Resistance");
}

QString structureBiasLabel(const StructureBias bias) {
    switch (bias) {
    case StructureBias::Unavailable:
        return QStringLiteral("Unavailable");
    case StructureBias::Bullish:
        return QStringLiteral("Bullish structure");
    case StructureBias::Neutral:
        return QStringLiteral("Mixed structure");
    case StructureBias::Bearish:
        return QStringLiteral("Bearish structure");
    }
    return QStringLiteral("Unavailable");
}

QString patternKindLabel(const PatternKind kind) {
    switch (kind) {
    case PatternKind::DoubleTop:
        return QStringLiteral("Double top");
    case PatternKind::DoubleBottom:
        return QStringLiteral("Double bottom");
    case PatternKind::Triangle:
        return QStringLiteral("Triangle");
    case PatternKind::Rectangle:
        return QStringLiteral("Rectangle");
    case PatternKind::RisingWedge:
        return QStringLiteral("Rising wedge");
    case PatternKind::FallingWedge:
        return QStringLiteral("Falling wedge");
    case PatternKind::HeadAndShoulders:
        return QStringLiteral("Head and shoulders");
    case PatternKind::InverseHeadAndShoulders:
        return QStringLiteral("Inverse head and shoulders");
    }
    return QStringLiteral("Pattern");
}

QString patternDirectionLabel(const PatternDirection direction) {
    switch (direction) {
    case PatternDirection::Neutral:
        return QStringLiteral("Neutral");
    case PatternDirection::Bullish:
        return QStringLiteral("Bullish");
    case PatternDirection::Bearish:
        return QStringLiteral("Bearish");
    }
    return QStringLiteral("Neutral");
}

QString patternStatusLabel(const PatternStatus status) {
    switch (status) {
    case PatternStatus::Emerging:
        return QStringLiteral("Emerging");
    case PatternStatus::Confirmed:
        return QStringLiteral("Confirmed");
    case PatternStatus::Invalidated:
        return QStringLiteral("Invalidated");
    case PatternStatus::TargetReached:
        return QStringLiteral("Target reached");
    }
    return QStringLiteral("Emerging");
}

QString validateMarketStructureSettings(
    const MarketStructureSettings& settings) {
    if (settings.pivotStrength < 2 ||
        settings.pivotStrength > 10) {
        return QStringLiteral(
            "Pivot strength must be between 2 and 10.");
    }
    if (!std::isfinite(settings.zoneAtrMultiplier) ||
        settings.zoneAtrMultiplier < 0.25 ||
        settings.zoneAtrMultiplier > 3.0) {
        return QStringLiteral(
            "Zone ATR sensitivity must be between 0.25 and 3.0.");
    }
    if (settings.minimumZoneTouches < 2 ||
        settings.minimumZoneTouches > 6 ||
        settings.maximumLookbackBars < 100 ||
        settings.maximumLookbackBars > 1'500 ||
        settings.maximumPivots == 0 ||
        settings.maximumPivots > 64 ||
        settings.maximumZones == 0 ||
        settings.maximumZones > 12 ||
        settings.maximumPatterns == 0 ||
        settings.maximumPatterns > 24) {
        return QStringLiteral(
            "Market-structure bounds are invalid.");
    }
    return {};
}

Bars aggregateCompletedHigherTimeframe(
    const Bars& bars,
    const Timeframe timeframe) {
    Bars result;
    if (bars.empty() || validateBars(bars)) {
        return result;
    }
    struct BucketKey {
        int first{};
        int second{};
        [[nodiscard]] bool operator==(
            const BucketKey&) const = default;
    };
    std::optional<BucketKey> currentKey;
    Bar current;
    const auto keyFor = [timeframe](const std::int64_t timestamp) {
        const auto date =
            QDateTime::fromSecsSinceEpoch(
                timestamp,
                QTimeZone::UTC)
                .date();
        if (timeframe == Timeframe::OneDay) {
            int weekYear{};
            const auto week = date.weekNumber(&weekYear);
            return BucketKey{weekYear, week};
        }
        return BucketKey{date.year(), date.dayOfYear()};
    };
    for (const auto& bar : bars) {
        const auto key = keyFor(bar.timestamp);
        if (!currentKey || key != *currentKey) {
            if (currentKey) {
                result.push_back(current);
            }
            currentKey = key;
            current = bar;
        } else {
            current.high = std::max(current.high, bar.high);
            current.low = std::min(current.low, bar.low);
            current.close = bar.close;
            current.volume += bar.volume;
            current.timestamp = bar.timestamp;
        }
    }
    // The latest bucket has no later bucket proving that it completed.
    return result;
}

MarketStructureReport analyzeMarketStructure(
    const MarketStructureInput& input) {
    MarketStructureReport report{
        .symbol = normalizeWatchlistSymbol(input.symbol),
        .timeframe = input.timeframe,
    };
    if (report.symbol.isEmpty()) {
        report.error =
            QStringLiteral("A valid symbol is required.");
        return report;
    }
    if (const auto error =
            validateMarketStructureSettings(input.settings);
        !error.isEmpty()) {
        report.error = error;
        return report;
    }
    Bars bars = input.bars;
    if (input.analysisThroughTimestamp) {
        std::erase_if(
            bars,
            [&input](const Bar& bar) {
                return bar.timestamp >
                       *input.analysisThroughTimestamp;
            });
    }
    if (const auto error = validateBars(bars)) {
        report.error =
            QStringLiteral("Market-structure bars are invalid: %1")
                .arg(QString::fromStdString(*error));
        return report;
    }
    if (bars.size() < kMinimumBars) {
        report.error = QStringLiteral(
            "At least 30 completed bars are required.");
        return report;
    }
    if (bars.size() > input.settings.maximumLookbackBars) {
        bars.erase(
            bars.begin(),
            bars.end() -
                static_cast<std::ptrdiff_t>(
                    input.settings.maximumLookbackBars));
        report.warnings.push_back(
            QStringLiteral(
                "Analysis was bounded to the latest %1 bars.")
                .arg(input.settings.maximumLookbackBars));
    }
    report.barsAnalyzed = bars.size();
    report.asOfTimestamp = bars.back().timestamp;
    report.latestClose = bars.back().close;
    const auto atr = atrSeries(bars);
    report.pivots =
        detectPivots(bars, atr, input.settings);
    report.zones =
        buildZones(
            bars,
            atr,
            report.pivots,
            input.settings);
    report.lines =
        buildTrendLines(bars, report.pivots);
    auto allPatterns =
        detectPatterns(
            bars,
            atr,
            report.pivots);
    report.patterns = allPatterns;
    if (report.patterns.size() >
        input.settings.maximumPatterns) {
        report.patterns.erase(
            report.patterns.begin(),
            report.patterns.end() -
                static_cast<std::ptrdiff_t>(
                    input.settings.maximumPatterns));
    }
    report.bias = biasFromPivots(report.pivots);
    if (input.includeHistoricalValidation) {
        report.historicalOutcomes =
            historicalOutcomes(bars, allPatterns);
    }

    const auto higher =
        aggregateCompletedHigherTimeframe(
            bars,
            input.timeframe);
    report.confluence.higherTimeframe =
        higherLabel(input.timeframe);
    report.confluence.sourceBias = report.bias;
    report.confluence.completedHigherBars =
        higher.size();
    if (higher.size() >= kMinimumBars) {
        const auto higherAtr = atrSeries(higher);
        auto higherSettings = input.settings;
        higherSettings.maximumLookbackBars =
            std::min(
                higherSettings.maximumLookbackBars,
                higher.size());
        const auto higherPivots =
            detectPivots(
                higher,
                higherAtr,
                higherSettings);
        report.confluence.higherBias =
            biasFromPivots(higherPivots);
    }
    if (report.bias == StructureBias::Unavailable ||
        report.confluence.higherBias ==
            StructureBias::Unavailable) {
        report.confluence.state =
            QStringLiteral("Unavailable");
        report.confluence.explanation =
            QStringLiteral(
                "Insufficient confirmed pivots on one or both timeframes.");
    } else if (
        report.bias == report.confluence.higherBias &&
        report.bias != StructureBias::Neutral) {
        report.confluence.state = QStringLiteral("Aligned");
        report.confluence.explanation =
            QStringLiteral(
                "Source and completed higher timeframe have the same structure bias.");
    } else if (
        (report.bias == StructureBias::Bullish &&
         report.confluence.higherBias ==
             StructureBias::Bearish) ||
        (report.bias == StructureBias::Bearish &&
         report.confluence.higherBias ==
             StructureBias::Bullish)) {
        report.confluence.state =
            QStringLiteral("Conflicting");
        report.confluence.explanation =
            QStringLiteral(
                "Source and completed higher timeframe structure biases oppose.");
    } else {
        report.confluence.state = QStringLiteral("Mixed");
        report.confluence.explanation =
            QStringLiteral(
                "At least one timeframe has mixed swing structure.");
    }
    return report;
}

} // namespace tvchart
