#pragma once

#include "domain/bar.hpp"

#include <QString>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace tvchart {

enum class PivotType : std::uint8_t {
    High,
    Low,
};

enum class StructureZoneType : std::uint8_t {
    Support,
    Resistance,
};

enum class StructureLineKind : std::uint8_t {
    Trendline,
    ChannelBoundary,
    PatternBoundary,
};

enum class StructureBias : std::uint8_t {
    Unavailable,
    Bullish,
    Neutral,
    Bearish,
};

enum class PatternKind : std::uint8_t {
    DoubleTop,
    DoubleBottom,
    Triangle,
    Rectangle,
    RisingWedge,
    FallingWedge,
    HeadAndShoulders,
    InverseHeadAndShoulders,
};

enum class PatternDirection : std::uint8_t {
    Neutral,
    Bullish,
    Bearish,
};

enum class PatternStatus : std::uint8_t {
    Emerging,
    Confirmed,
    Invalidated,
    TargetReached,
};

struct MarketStructureSettings {
    int pivotStrength{3};
    double zoneAtrMultiplier{0.75};
    int minimumZoneTouches{2};
    std::size_t maximumLookbackBars{1'500};
    std::size_t maximumPivots{64};
    std::size_t maximumZones{12};
    std::size_t maximumPatterns{24};

    [[nodiscard]] bool operator==(
        const MarketStructureSettings&) const = default;
};

struct ConfirmedPivot {
    PivotType type{PivotType::High};
    std::size_t anchorIndex{};
    std::size_t confirmationIndex{};
    std::int64_t anchorTimestamp{};
    std::int64_t confirmationTimestamp{};
    double price{};
    double atr{};

    [[nodiscard]] bool operator==(const ConfirmedPivot&) const = default;
};

struct StructureZone {
    StructureZoneType type{StructureZoneType::Support};
    double low{};
    double high{};
    double center{};
    int touches{};
    double strength{};
    double distanceFromClosePercent{};
    std::int64_t firstAnchorTimestamp{};
    std::int64_t lastAnchorTimestamp{};
    std::int64_t detectionTimestamp{};
    std::optional<std::int64_t> brokenTimestamp;
    bool broken{};
    QString explanation;

    [[nodiscard]] bool operator==(const StructureZone&) const = default;
};

struct StructureLine {
    StructureLineKind kind{StructureLineKind::Trendline};
    std::int64_t startTimestamp{};
    std::int64_t endTimestamp{};
    double startPrice{};
    double endPrice{};
    QString title;
    QString color;
    bool dashed{};

    [[nodiscard]] bool operator==(const StructureLine&) const = default;
};

struct ChartPattern {
    QString id;
    PatternKind kind{PatternKind::Triangle};
    PatternDirection direction{PatternDirection::Neutral};
    PatternStatus status{PatternStatus::Emerging};
    std::size_t formationIndex{};
    std::size_t detectionIndex{};
    std::int64_t startTimestamp{};
    std::int64_t endTimestamp{};
    std::int64_t detectionTimestamp{};
    std::int64_t statusTimestamp{};
    double referencePrice{};
    std::optional<double> targetPrice;
    std::optional<double> invalidationPrice;
    std::vector<ConfirmedPivot> anchors;
    std::vector<StructureLine> boundaries;
    QString explanation;

    [[nodiscard]] bool operator==(const ChartPattern&) const = default;
};

struct PatternOutcomeSummary {
    PatternKind kind{PatternKind::Triangle};
    PatternDirection direction{PatternDirection::Neutral};
    std::size_t samples{};
    std::optional<double> medianSignedReturn5Percent;
    std::optional<double> medianSignedReturn20Percent;
    std::optional<double> positiveSignedReturn20Percent;
    std::optional<double> medianMaximumAdverseExcursion20Percent;
    std::size_t targetHits{};
    std::size_t invalidations{};
    bool sampleAdequate{};
    QString note;
};

struct HigherTimeframeConfluence {
    QString higherTimeframe;
    StructureBias sourceBias{StructureBias::Unavailable};
    StructureBias higherBias{StructureBias::Unavailable};
    QString state;
    std::size_t completedHigherBars{};
    QString explanation;
};

struct MarketStructureInput {
    QString symbol;
    Bars bars;
    Timeframe timeframe{Timeframe::OneDay};
    MarketStructureSettings settings;
    std::optional<std::int64_t> analysisThroughTimestamp;
    bool includeHistoricalValidation{true};
};

struct MarketStructureReport {
    QString symbol;
    Timeframe timeframe{Timeframe::OneDay};
    std::size_t barsAnalyzed{};
    std::int64_t asOfTimestamp{};
    double latestClose{};
    std::vector<ConfirmedPivot> pivots;
    std::vector<StructureZone> zones;
    std::vector<StructureLine> lines;
    std::vector<ChartPattern> patterns;
    std::vector<PatternOutcomeSummary> historicalOutcomes;
    HigherTimeframeConfluence confluence;
    StructureBias bias{StructureBias::Unavailable};
    std::vector<QString> warnings;
    QString error;

    [[nodiscard]] bool ok() const noexcept { return error.isEmpty(); }
};

[[nodiscard]] QString pivotTypeLabel(PivotType type);
[[nodiscard]] QString structureZoneTypeLabel(StructureZoneType type);
[[nodiscard]] QString structureBiasLabel(StructureBias bias);
[[nodiscard]] QString patternKindLabel(PatternKind kind);
[[nodiscard]] QString patternDirectionLabel(PatternDirection direction);
[[nodiscard]] QString patternStatusLabel(PatternStatus status);
[[nodiscard]] QString validateMarketStructureSettings(
    const MarketStructureSettings& settings);
[[nodiscard]] Bars aggregateCompletedHigherTimeframe(
    const Bars& bars,
    Timeframe timeframe);
[[nodiscard]] MarketStructureReport analyzeMarketStructure(
    const MarketStructureInput& input);

} // namespace tvchart
