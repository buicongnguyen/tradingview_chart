#include "strategy/theory_validation.hpp"

#include <algorithm>
#include <cmath>
#include <iterator>
#include <numeric>
#include <ranges>
#include <set>
#include <utility>

namespace tvchart {
namespace {

struct ObservedOutcome {
    std::int64_t signalTimestamp{};
    double shortReturnPercent{};
    double longReturnPercent{};
};

[[nodiscard]] double average(const std::vector<double>& values) {
    if (values.empty()) {
        return 0.0;
    }
    return std::accumulate(values.begin(), values.end(), 0.0) /
           static_cast<double>(values.size());
}

[[nodiscard]] double median(std::vector<double> values) {
    if (values.empty()) {
        return 0.0;
    }
    std::ranges::sort(values);
    const auto middle = values.size() / 2;
    if (values.size() % 2 == 0) {
        return (values[middle - 1] + values[middle]) / 2.0;
    }
    return values[middle];
}

[[nodiscard]] std::pair<double, double> wilsonInterval(
    const std::size_t successes,
    const std::size_t samples) {
    if (samples == 0) {
        return {};
    }
    constexpr auto z = 1.959963984540054;
    const auto count = static_cast<double>(samples);
    const auto proportion =
        static_cast<double>(successes) / count;
    const auto zSquared = z * z;
    const auto denominator = 1.0 + zSquared / count;
    const auto center =
        (proportion + zSquared / (2.0 * count)) / denominator;
    const auto margin =
        z *
        std::sqrt(
            (proportion * (1.0 - proportion) / count) +
            zSquared / (4.0 * count * count)) /
        denominator;
    return {
        std::max(0.0, center - margin) * 100.0,
        std::min(1.0, center + margin) * 100.0,
    };
}

[[nodiscard]] double modeledReturnPercent(
    const Bar& entryBar,
    const Bar& exitBar,
    const double roundTripCostBasisPoints) {
    const auto halfCost =
        roundTripCostBasisPoints / 20'000.0;
    const auto entry = entryBar.open * (1.0 + halfCost);
    const auto exit = exitBar.close * (1.0 - halfCost);
    return (exit / entry - 1.0) * 100.0;
}

[[nodiscard]] double baselineAverage(
    const Bars& bars,
    const std::size_t horizon,
    const double roundTripCostBasisPoints) {
    std::vector<double> returns;
    if (bars.size() <= horizon) {
        return 0.0;
    }
    returns.reserve(bars.size() - horizon);
    for (std::size_t index = 0;
         index + horizon < bars.size();
         ++index) {
        returns.push_back(modeledReturnPercent(
            bars[index + 1],
            bars[index + horizon],
            roundTripCostBasisPoints));
    }
    return average(returns);
}

[[nodiscard]] TheoryHorizonMetrics summarizeHorizon(
    const std::vector<ObservedOutcome>& outcomes,
    const bool shortHorizon,
    const std::size_t horizonBars,
    const double holdoutPercent,
    const double baseline) {
    TheoryHorizonMetrics metrics{
        .horizonBars = horizonBars,
        .samples = outcomes.size(),
        .baselineAverageReturnPercent = baseline,
    };
    if (outcomes.empty()) {
        return metrics;
    }

    std::vector<double> returns;
    returns.reserve(outcomes.size());
    for (const auto& outcome : outcomes) {
        returns.push_back(
            shortHorizon
                ? outcome.shortReturnPercent
                : outcome.longReturnPercent);
    }
    metrics.positiveOutcomes =
        static_cast<std::size_t>(std::ranges::count_if(
            returns,
            [](const double value) { return value > 0.0; }));
    metrics.hitRatePercent =
        static_cast<double>(metrics.positiveOutcomes) /
        static_cast<double>(metrics.samples) * 100.0;
    metrics.averageReturnPercent = average(returns);
    metrics.medianReturnPercent = median(returns);
    metrics.averageExcessReturnPercent =
        metrics.averageReturnPercent - baseline;

    const auto requestedHoldout = static_cast<std::size_t>(
        std::ceil(
            static_cast<double>(returns.size()) *
            holdoutPercent / 100.0));
    metrics.holdoutSamples =
        std::min(returns.size(), std::max(std::size_t{1}, requestedHoldout));
    metrics.trainingSamples =
        returns.size() - metrics.holdoutSamples;
    const auto split = metrics.trainingSamples;

    if (metrics.trainingSamples > 0) {
        const auto trainingEnd =
            std::next(
                returns.begin(),
                static_cast<std::ptrdiff_t>(split));
        const auto trainingWins =
            static_cast<std::size_t>(std::count_if(
                returns.begin(),
                trainingEnd,
                [](const double value) { return value > 0.0; }));
        metrics.trainingHitRatePercent =
            static_cast<double>(trainingWins) /
            static_cast<double>(metrics.trainingSamples) * 100.0;
        metrics.trainingAverageReturnPercent =
            std::accumulate(returns.begin(), trainingEnd, 0.0) /
            static_cast<double>(metrics.trainingSamples);
    }

    const auto holdoutBegin =
        std::next(
            returns.begin(),
            static_cast<std::ptrdiff_t>(split));
    const auto holdoutWins =
        static_cast<std::size_t>(std::count_if(
            holdoutBegin,
            returns.end(),
            [](const double value) { return value > 0.0; }));
    metrics.holdoutHitRatePercent =
        static_cast<double>(holdoutWins) /
        static_cast<double>(metrics.holdoutSamples) * 100.0;
    metrics.holdoutAverageReturnPercent =
        std::accumulate(holdoutBegin, returns.end(), 0.0) /
        static_cast<double>(metrics.holdoutSamples);
    const auto [lower, upper] =
        wilsonInterval(holdoutWins, metrics.holdoutSamples);
    metrics.holdoutConfidenceLowerPercent = lower;
    metrics.holdoutConfidenceUpperPercent = upper;
    return metrics;
}

[[nodiscard]] TheoryReliability reliabilityFor(
    const TheoryHorizonMetrics& metrics,
    const std::size_t minimumSamples) {
    if (metrics.samples < minimumSamples ||
        metrics.holdoutSamples < 8) {
        return TheoryReliability::Insufficient;
    }
    if (metrics.samples >= 150 &&
        metrics.holdoutSamples >= 30) {
        return TheoryReliability::High;
    }
    if (metrics.samples >= 60 &&
        metrics.holdoutSamples >= 15) {
        return TheoryReliability::Moderate;
    }
    return TheoryReliability::Low;
}

[[nodiscard]] TheoryEvidence evidenceFor(
    const TheoryHorizonMetrics& metrics,
    const TheoryReliability reliability) {
    if (reliability == TheoryReliability::Insufficient ||
        metrics.trainingSamples == 0 ||
        metrics.holdoutSamples == 0) {
        return TheoryEvidence::Unavailable;
    }
    if (metrics.trainingAverageReturnPercent > 0.0 &&
        metrics.holdoutAverageReturnPercent > 0.0 &&
        metrics.holdoutHitRatePercent > 50.0 &&
        metrics.averageExcessReturnPercent > 0.0) {
        return TheoryEvidence::Positive;
    }
    if (metrics.trainingAverageReturnPercent < 0.0 &&
        metrics.holdoutAverageReturnPercent < 0.0 &&
        metrics.holdoutHitRatePercent < 50.0 &&
        metrics.averageExcessReturnPercent < 0.0) {
        return TheoryEvidence::Negative;
    }
    return TheoryEvidence::Mixed;
}

[[nodiscard]] int reliabilityRank(
    const TheoryReliability reliability) noexcept {
    switch (reliability) {
    case TheoryReliability::High:
        return 3;
    case TheoryReliability::Moderate:
        return 2;
    case TheoryReliability::Low:
        return 1;
    case TheoryReliability::Insufficient:
        return 0;
    }
    return 0;
}

[[nodiscard]] Bars barsThrough(
    const Bars& bars,
    const std::int64_t timestamp) {
    const auto end = std::upper_bound(
        bars.begin(),
        bars.end(),
        timestamp,
        [](const std::int64_t value, const Bar& bar) {
            return value < bar.timestamp;
        });
    return {bars.begin(), end};
}

} // namespace

QString theoryReliabilityLabel(const TheoryReliability reliability) {
    switch (reliability) {
    case TheoryReliability::Insufficient:
        return QStringLiteral("Insufficient");
    case TheoryReliability::Low:
        return QStringLiteral("Low");
    case TheoryReliability::Moderate:
        return QStringLiteral("Moderate");
    case TheoryReliability::High:
        return QStringLiteral("High");
    }
    return QStringLiteral("Insufficient");
}

QString theoryEvidenceLabel(const TheoryEvidence evidence) {
    switch (evidence) {
    case TheoryEvidence::Unavailable:
        return QStringLiteral("Unavailable");
    case TheoryEvidence::Negative:
        return QStringLiteral("Negative");
    case TheoryEvidence::Mixed:
        return QStringLiteral("Mixed");
    case TheoryEvidence::Positive:
        return QStringLiteral("Positive");
    }
    return QStringLiteral("Unavailable");
}

TheoryValidationReport validateTheories(
    const TheoryValidationInput& input) {
    TheoryValidationReport report{
        .symbol = input.symbol.trimmed().toUpper(),
        .requestedAsOfTimestamp = input.analysisThroughTimestamp,
    };
    if (const auto error = validateBars(input.bars)) {
        report.error =
            QStringLiteral("Theory bars are invalid: %1")
                .arg(QString::fromStdString(*error));
        return report;
    }
    if (report.symbol.isEmpty()) {
        report.error = QStringLiteral("A theory symbol is required.");
        return report;
    }
    if (input.theories.empty() || input.theories.size() > 64) {
        report.error =
            QStringLiteral("Theory validation requires between 1 and 64 theories.");
        return report;
    }
    if (input.analysisThroughTimestamp <= 0 ||
        input.shortHorizonBars < 1 ||
        input.longHorizonBars <= input.shortHorizonBars ||
        input.longHorizonBars > 252 ||
        !std::isfinite(input.holdoutPercent) ||
        input.holdoutPercent < 10.0 ||
        input.holdoutPercent > 50.0 ||
        input.minimumSamples < 10 ||
        input.minimumSamples > 1'000 ||
        !std::isfinite(input.roundTripCostBasisPoints) ||
        input.roundTripCostBasisPoints < 0.0 ||
        input.roundTripCostBasisPoints > 1'000.0) {
        report.error =
            QStringLiteral("Theory horizons, holdout, sample, or cost assumptions are invalid.");
        return report;
    }

    const auto selectedBars =
        barsThrough(input.bars, input.analysisThroughTimestamp);
    if (selectedBars.size() <= input.longHorizonBars + 1) {
        report.error =
            QStringLiteral("The selected moment has too little completed history.");
        return report;
    }
    report.asOfTimestamp = selectedBars.back().timestamp;
    report.barsAnalyzed = selectedBars.size();

    const auto shortBaseline = baselineAverage(
        selectedBars,
        input.shortHorizonBars,
        input.roundTripCostBasisPoints);
    const auto longBaseline = baselineAverage(
        selectedBars,
        input.longHorizonBars,
        input.roundTripCostBasisPoints);
    std::set<QString> identities;
    report.results.reserve(input.theories.size());

    for (const auto& theory : input.theories) {
        TheoryValidationResult result{
            .theoryId = theory.id.trimmed(),
            .theoryName = theory.name.trimmed(),
        };
        if (result.theoryName.isEmpty()) {
            result.theoryName = result.theoryId;
        }
        if (result.theoryId.isEmpty() ||
            !identities.insert(result.theoryId).second) {
            result.error =
                QStringLiteral("Every theory requires a unique non-empty ID.");
            report.results.push_back(std::move(result));
            continue;
        }
        if (const auto error = validateStrategy(theory);
            !error.isEmpty()) {
            result.error = error;
            report.results.push_back(std::move(result));
            continue;
        }

        StrategyEvaluator evaluator(
            selectedBars,
            input.primaryTimeframe,
            input.additionalSeries);
        const auto current =
            evaluator.evaluate(
                theory.entry,
                selectedBars.size() - 1);
        result.evaluationAvailable = current.available;
        result.matchesAtAsOf =
            current.available && current.matched;
        result.currentDetail = current.detail;

        std::vector<ObservedOutcome> outcomes;
        auto previousMatched = false;
        auto blockedThrough = std::size_t{};
        auto hasBlockedWindow = false;
        for (std::size_t index = 0;
             index + input.longHorizonBars < selectedBars.size();
             ++index) {
            const auto evaluation =
                evaluator.evaluate(theory.entry, index);
            const auto matched =
                evaluation.available && evaluation.matched;
            const auto episodeStarted =
                matched && !previousMatched;
            previousMatched = matched;
            if (!episodeStarted ||
                (hasBlockedWindow && index <= blockedThrough)) {
                continue;
            }
            outcomes.push_back({
                .signalTimestamp = selectedBars[index].timestamp,
                .shortReturnPercent = modeledReturnPercent(
                    selectedBars[index + 1],
                    selectedBars[index + input.shortHorizonBars],
                    input.roundTripCostBasisPoints),
                .longReturnPercent = modeledReturnPercent(
                    selectedBars[index + 1],
                    selectedBars[index + input.longHorizonBars],
                    input.roundTripCostBasisPoints),
            });
            blockedThrough = index + input.longHorizonBars;
            hasBlockedWindow = true;
        }

        result.shortHorizon = summarizeHorizon(
            outcomes,
            true,
            input.shortHorizonBars,
            input.holdoutPercent,
            shortBaseline);
        result.longHorizon = summarizeHorizon(
            outcomes,
            false,
            input.longHorizonBars,
            input.holdoutPercent,
            longBaseline);
        result.reliability =
            reliabilityFor(
                result.longHorizon,
                input.minimumSamples);
        result.evidence =
            evidenceFor(
                result.longHorizon,
                result.reliability);
        result.explanation =
            QStringLiteral(
                "%1 at the selected completed bar. %2 non-overlapping "
                "episodes; %3 holdout episodes. Reliability measures sample "
                "coverage, not profitability.")
                .arg(
                    result.matchesAtAsOf
                        ? QStringLiteral("Entry rule matches")
                        : result.evaluationAvailable
                              ? QStringLiteral("Entry rule does not match")
                              : QStringLiteral("Entry rule is unavailable"))
                .arg(result.longHorizon.samples)
                .arg(result.longHorizon.holdoutSamples);
        report.results.push_back(std::move(result));
    }

    std::vector<std::size_t> candidates;
    for (std::size_t index = 0;
         index < report.results.size();
         ++index) {
        const auto& result = report.results[index];
        if (result.ok() &&
            result.evidence == TheoryEvidence::Positive &&
            result.reliability != TheoryReliability::Insufficient) {
            candidates.push_back(index);
        }
    }
    std::ranges::sort(
        candidates,
        [&report](const std::size_t left, const std::size_t right) {
            const auto& lhs = report.results[left];
            const auto& rhs = report.results[right];
            const auto lhsReliability =
                reliabilityRank(lhs.reliability);
            const auto rhsReliability =
                reliabilityRank(rhs.reliability);
            if (lhsReliability != rhsReliability) {
                return lhsReliability > rhsReliability;
            }
            if (lhs.longHorizon.holdoutConfidenceLowerPercent !=
                rhs.longHorizon.holdoutConfidenceLowerPercent) {
                return lhs.longHorizon.holdoutConfidenceLowerPercent >
                       rhs.longHorizon.holdoutConfidenceLowerPercent;
            }
            if (lhs.longHorizon.holdoutAverageReturnPercent !=
                rhs.longHorizon.holdoutAverageReturnPercent) {
                return lhs.longHorizon.holdoutAverageReturnPercent >
                       rhs.longHorizon.holdoutAverageReturnPercent;
            }
            return lhs.theoryName.compare(
                       rhs.theoryName,
                       Qt::CaseInsensitive) < 0;
        });
    for (std::size_t rank = 0; rank < candidates.size(); ++rank) {
        report.results[candidates[rank]].rank = rank + 1;
    }
    if (!candidates.empty()) {
        const auto& recommended =
            report.results[candidates.front()];
        report.recommendedTheoryId = recommended.theoryId;
        report.summary =
            QStringLiteral(
                "Best supported historical theory: %1. This is a comparison "
                "of fixed historical rules, not a forecast or recommendation.")
                .arg(recommended.theoryName);
    } else {
        report.summary =
            QStringLiteral(
                "No theory has both positive holdout evidence and adequate "
                "sample reliability at the selected moment.");
    }
    if (std::ranges::any_of(
            report.results,
            [](const TheoryValidationResult& result) {
                return result.reliability ==
                       TheoryReliability::Insufficient;
            })) {
        report.warnings.push_back(
            QStringLiteral(
                "One or more theories have insufficient sample coverage."));
    }
    return report;
}

} // namespace tvchart
