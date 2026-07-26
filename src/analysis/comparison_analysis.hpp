#pragma once

#include "domain/bar.hpp"

#include <QString>

#include <cstdint>
#include <optional>
#include <vector>

namespace tvchart {

struct ComparisonPoint {
    std::int64_t timestamp{};
    double primaryNormalized{};
    double comparisonNormalized{};
    double relativeNormalized{};
};

struct ComparisonAnalysis {
    std::vector<ComparisonPoint> points;
    double primaryReturnPercent{};
    double comparisonReturnPercent{};
    double relativeReturnPercent{};
    std::optional<double> returnCorrelation;
    QString error;

    [[nodiscard]] bool ok() const noexcept { return error.isEmpty(); }
};

[[nodiscard]] ComparisonAnalysis compareSeries(
    const Bars& primary,
    const Bars& comparison);

} // namespace tvchart
