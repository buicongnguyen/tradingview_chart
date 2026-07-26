#pragma once

#include "research/research_models.hpp"

#include <QByteArray>
#include <QDate>
#include <QString>

#include <cstdint>
#include <vector>

namespace tvchart {

struct SecTickerLookupResult {
    QString cik;
    QString companyName;
    QString error;

    [[nodiscard]] bool ok() const noexcept { return error.isEmpty(); }
};

struct EventIntelligenceParseResult {
    std::vector<ResearchEvent> events;
    QString error;

    [[nodiscard]] bool ok() const noexcept { return error.isEmpty(); }
};

class EventIntelligenceParser final {
public:
    [[nodiscard]] static SecTickerLookupResult parseSecTickerMap(
        const QByteArray& payload,
        const QString& requestedSymbol);
    [[nodiscard]] static EventIntelligenceParseResult parseSecSubmissions(
        const QByteArray& payload,
        const QString& requestedSymbol,
        const QString& expectedCik,
        std::int64_t retrievedAtUtc);
    [[nodiscard]] static EventIntelligenceParseResult parseFredReleaseDates(
        const QByteArray& payload,
        std::int64_t retrievedAtUtc,
        const QDate& firstDate,
        const QDate& lastDate);
};

} // namespace tvchart
