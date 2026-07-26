#pragma once

#include "fundamentals/fundamental_models.hpp"

#include <QByteArray>
#include <QString>

#include <cstdint>

namespace tvchart {

struct SecCompanyFactsParseResult {
    FundamentalCompany company;
    std::size_t rejectedFacts{};
    QString error;

    [[nodiscard]] bool ok() const noexcept {
        return error.isEmpty() && !company.facts.empty();
    }
};

class SecCompanyFactsParser final {
public:
    [[nodiscard]] static SecCompanyFactsParseResult parse(
        const QByteArray& payload,
        const QString& requestedSymbol,
        const QString& expectedCik,
        std::int64_t retrievedAtUtc);
};

} // namespace tvchart
