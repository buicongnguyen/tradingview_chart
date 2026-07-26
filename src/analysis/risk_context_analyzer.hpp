#pragma once

#include "data/market_data_quality.hpp"
#include "domain/bar.hpp"
#include "fundamentals/fundamental_models.hpp"
#include "research/research_models.hpp"

#include <QDate>
#include <QString>

#include <cstdint>
#include <optional>
#include <vector>

namespace tvchart {

enum class RiskLevel : std::uint8_t {
    Unavailable,
    Lower,
    Moderate,
    Elevated,
    High,
};

enum class RiskEvidenceKind : std::uint8_t {
    Adverse,
    Constructive,
};

enum class RiskCategory : std::uint8_t {
    MarketRegime,
    TrendMomentum,
    PriceAction,
    VolatilityLiquidity,
    PriceLocation,
    EventRisk,
    Fundamentals,
};

struct RiskEvidence {
    RiskEvidenceKind kind{RiskEvidenceKind::Adverse};
    RiskCategory category{RiskCategory::TrendMomentum};
    int points{};
    QString title;
    QString observed;
    QString explanation;
    QString source;
    QDate asOfDate;

    [[nodiscard]] bool operator==(const RiskEvidence&) const = default;
};

struct HistoricalRiskValidation {
    std::size_t comparableSetups{};
    std::optional<double> medianForwardReturn5Percent;
    std::optional<double> medianForwardReturn20Percent;
    std::optional<double> negativeForwardReturn20Percent;
    std::optional<double> medianMaximumDrawdown20Percent;
    std::optional<double> worstMaximumDrawdown20Percent;
    std::optional<double> medianBenchmarkRelativeReturn20Percent;
    bool sampleAdequate{};
    QString note;
};

struct RiskContextInput {
    QString symbol;
    QString benchmarkSymbol{QStringLiteral("SPY")};
    Bars securityBars;
    Bars benchmarkBars;
    MarketDataQualityReport securityQuality;
    MarketDataQualityReport benchmarkQuality;
    std::optional<FundamentalCompany> fundamentals;
    std::vector<ResearchEvent> events;
    bool eventCalendarCoverageKnown{};
    QDate analysisThroughDate;
    QDate observationDate;
    bool includeHistoricalValidation{true};
};

struct RiskContextReport {
    QString symbol;
    QString benchmarkSymbol;
    RiskLevel level{RiskLevel::Unavailable};
    int score{};
    int adversePoints{};
    int availableWeight{};
    double coveragePercent{};
    QString confidence;
    QDate securityAsOfDate;
    QDate benchmarkAsOfDate;
    std::vector<RiskEvidence> evidence;
    std::vector<QString> missingInputs;
    std::vector<QString> warnings;
    HistoricalRiskValidation historical;
    QString error;

    [[nodiscard]] bool ok() const noexcept { return error.isEmpty(); }
};

[[nodiscard]] QString riskLevelLabel(RiskLevel level);
[[nodiscard]] QString riskCategoryLabel(RiskCategory category);
[[nodiscard]] RiskContextReport analyzeRiskContext(
    const RiskContextInput& input);

} // namespace tvchart
