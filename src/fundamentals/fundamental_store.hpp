#pragma once

#include "fundamentals/fundamental_models.hpp"

#include <QString>

#include <memory>
#include <vector>

namespace tvchart {

struct StoredFundamentalCompany {
    FundamentalCompany company;
    QString error;

    [[nodiscard]] bool ok() const noexcept {
        return error.isEmpty() && !company.facts.empty();
    }
};

class FundamentalStore final {
public:
    explicit FundamentalStore(QString databasePath);
    ~FundamentalStore();

    FundamentalStore(const FundamentalStore&) = delete;
    FundamentalStore& operator=(const FundamentalStore&) = delete;
    FundamentalStore(FundamentalStore&&) = delete;
    FundamentalStore& operator=(FundamentalStore&&) = delete;

    [[nodiscard]] bool open();
    [[nodiscard]] bool isOpen() const noexcept;
    [[nodiscard]] QString lastError() const;
    [[nodiscard]] QString upsertCompany(const FundamentalCompany& company);
    [[nodiscard]] StoredFundamentalCompany loadCompany(QString symbol) const;
    [[nodiscard]] std::vector<FundamentalCompanySummary>
    availableCompanies() const;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace tvchart
