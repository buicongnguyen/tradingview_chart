#pragma once

#include <QByteArray>
#include <QDate>
#include <QString>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace tvchart {

enum class ResearchEventType {
    Earnings,
    ExDividend,
    DividendPayment,
    Filing,
    EconomicRelease,
    CentralBank,
    OptionsExpiration,
    MarketHoliday,
    Custom,
};

enum class ResearchConfidence {
    Confirmed,
    Estimated,
    Unknown,
};

struct ResearchEvent {
    QString id;
    QString symbol;
    ResearchEventType type{ResearchEventType::Custom};
    QDate scheduledDate;
    QString timeOfDay;
    QString title;
    QString source;
    std::int64_t asOfUtc{};
    ResearchConfidence confidence{ResearchConfidence::Unknown};
    std::optional<double> estimate;
    std::optional<double> actual;
    QString currency;
    QString detail;

    [[nodiscard]] bool operator==(const ResearchEvent&) const = default;
};

enum class TargetEstimateScope {
    Organization,
    AggregatedConsensus,
};

struct AnalystTargetEstimate {
    QString id;
    QString symbol;
    QString organization;
    double targetPrice{};
    QString currency;
    QDate publishedDate;
    QString rating;
    QString sourceUrl;
    TargetEstimateScope scope{TargetEstimateScope::Organization};

    [[nodiscard]] bool operator==(const AnalystTargetEstimate&) const = default;
};

struct AnalystRatingCounts {
    int strongBuy{};
    int buy{};
    int hold{};
    int sell{};
    int strongSell{};

    [[nodiscard]] int total() const noexcept;
    [[nodiscard]] bool operator==(const AnalystRatingCounts&) const = default;
};

struct CompanyResearchSnapshot {
    QString symbol;
    QString provider;
    std::int64_t asOfUtc{};
    QString name;
    QString cik;
    QString exchange;
    QString currency;
    QString sector;
    QString industry;
    std::optional<double> marketCapitalization;
    std::optional<double> eps;
    std::optional<double> peRatio;
    std::optional<double> forwardPe;
    std::optional<double> beta;
    std::optional<double> week52High;
    std::optional<double> week52Low;
    std::optional<double> analystTargetPrice;
    AnalystRatingCounts ratings;

    [[nodiscard]] bool operator==(const CompanyResearchSnapshot&) const = default;
};

struct ResearchWorkspace {
    static constexpr int currentSchemaVersion{1};
    static constexpr std::size_t maximumEvents{5'000};
    static constexpr std::size_t maximumTargetEstimates{2'000};
    static constexpr std::size_t maximumCompanySnapshots{1'000};

    std::vector<ResearchEvent> events;
    std::vector<AnalystTargetEstimate> targetEstimates;
    std::vector<CompanyResearchSnapshot> companySnapshots;

    [[nodiscard]] bool operator==(const ResearchWorkspace&) const = default;
};

struct TargetConsensusSummary {
    std::size_t organizationCount{};
    double minimum{};
    double maximum{};
    double mean{};
    double median{};
    double dispersion{};
};

struct ResearchWorkspaceLoadResult {
    ResearchWorkspace workspace;
    QString error;

    [[nodiscard]] bool ok() const noexcept { return error.isEmpty(); }
};

struct TargetCsvResult {
    std::vector<AnalystTargetEstimate> estimates;
    std::vector<std::size_t> rejectedLines;
    QString error;

    [[nodiscard]] bool ok() const noexcept { return error.isEmpty(); }
};

[[nodiscard]] QString researchEventTypeId(ResearchEventType type);
[[nodiscard]] QString researchEventTypeLabel(ResearchEventType type);
[[nodiscard]] QString researchConfidenceId(ResearchConfidence confidence);
[[nodiscard]] QString targetEstimateScopeId(TargetEstimateScope scope);
[[nodiscard]] QString validateResearchEvent(const ResearchEvent& event);
[[nodiscard]] QString validateTargetEstimate(
    const AnalystTargetEstimate& estimate);
[[nodiscard]] QString validateCompanySnapshot(
    const CompanyResearchSnapshot& snapshot);
[[nodiscard]] std::optional<TargetConsensusSummary> summarizeOrganizationTargets(
    const std::vector<AnalystTargetEstimate>& estimates,
    const QString& symbol,
    const QString& currency);
[[nodiscard]] QByteArray serializeResearchWorkspace(
    const ResearchWorkspace& workspace);
[[nodiscard]] ResearchWorkspaceLoadResult deserializeResearchWorkspace(
    const QByteArray& json);
[[nodiscard]] QByteArray exportTargetEstimatesCsv(
    const std::vector<AnalystTargetEstimate>& estimates);
[[nodiscard]] TargetCsvResult importTargetEstimatesCsv(const QByteArray& csv);

} // namespace tvchart
