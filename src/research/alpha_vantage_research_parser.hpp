#pragma once

#include "research/research_models.hpp"

#include <QByteArray>
#include <QString>

#include <cstdint>
#include <vector>

namespace tvchart {

struct AlphaVantageOverviewParseResult {
    CompanyResearchSnapshot snapshot;
    std::vector<ResearchEvent> corporateEvents;
    QString error;

    [[nodiscard]] bool ok() const noexcept { return error.isEmpty(); }
};

struct AlphaVantageCalendarParseResult {
    std::vector<ResearchEvent> events;
    QString error;

    [[nodiscard]] bool ok() const noexcept { return error.isEmpty(); }
};

class AlphaVantageResearchParser final {
public:
    [[nodiscard]] static AlphaVantageOverviewParseResult parseOverview(
        const QByteArray& payload,
        std::int64_t retrievedAtUtc);
    [[nodiscard]] static AlphaVantageCalendarParseResult parseEarningsCalendar(
        const QByteArray& payload,
        std::int64_t retrievedAtUtc);
};

} // namespace tvchart
