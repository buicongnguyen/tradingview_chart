#pragma once

#include "strategy/strategy_engine.hpp"

#include <QString>

#include <cstddef>
#include <cstdint>
#include <vector>

namespace tvchart {

enum class TheoryReliability : std::uint8_t {
    Insufficient,
    Low,
    Moderate,
    High,
};

enum class TheoryEvidence : std::uint8_t {
    Unavailable,
    Negative,
    Mixed,
    Positive,
};

struct TheoryHorizonMetrics {
    std::size_t horizonBars{};
    std::size_t samples{};
    std::size_t positiveOutcomes{};
    double hitRatePercent{};
    double averageReturnPercent{};
    double medianReturnPercent{};
    std::size_t trainingSamples{};
    double trainingHitRatePercent{};
    double trainingAverageReturnPercent{};
    std::size_t holdoutSamples{};
    double holdoutHitRatePercent{};
    double holdoutAverageReturnPercent{};
    double holdoutConfidenceLowerPercent{};
    double holdoutConfidenceUpperPercent{};
    double baselineAverageReturnPercent{};
    double averageExcessReturnPercent{};
};

struct TheoryValidationResult {
    QString theoryId;
    QString theoryName;
    std::size_t rank{};
    bool evaluationAvailable{};
    bool matchesAtAsOf{};
    QString currentDetail;
    TheoryHorizonMetrics shortHorizon;
    TheoryHorizonMetrics longHorizon;
    TheoryReliability reliability{TheoryReliability::Insufficient};
    TheoryEvidence evidence{TheoryEvidence::Unavailable};
    QString explanation;
    QString error;

    [[nodiscard]] bool ok() const noexcept { return error.isEmpty(); }
};

struct TheoryValidationInput {
    QString symbol;
    Bars bars;
    Timeframe primaryTimeframe{Timeframe::OneDay};
    TimeframeSeries additionalSeries;
    std::vector<StrategyDefinition> theories;
    std::int64_t analysisThroughTimestamp{};
    std::size_t shortHorizonBars{5};
    std::size_t longHorizonBars{20};
    double holdoutPercent{30.0};
    std::size_t minimumSamples{30};
    double roundTripCostBasisPoints{};
};

struct TheoryValidationReport {
    QString symbol;
    std::int64_t requestedAsOfTimestamp{};
    std::int64_t asOfTimestamp{};
    std::size_t barsAnalyzed{};
    std::vector<TheoryValidationResult> results;
    QString recommendedTheoryId;
    QString summary;
    std::vector<QString> warnings;
    QString error;

    [[nodiscard]] bool ok() const noexcept { return error.isEmpty(); }
};

[[nodiscard]] QString theoryReliabilityLabel(TheoryReliability reliability);
[[nodiscard]] QString theoryEvidenceLabel(TheoryEvidence evidence);
[[nodiscard]] TheoryValidationReport validateTheories(
    const TheoryValidationInput& input);

} // namespace tvchart
