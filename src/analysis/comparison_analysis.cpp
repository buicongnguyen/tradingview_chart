#include "analysis/comparison_analysis.hpp"

#include <algorithm>
#include <cmath>
#include <numeric>

namespace tvchart {

ComparisonAnalysis compareSeries(
    const Bars& primary,
    const Bars& comparison) {
    if (const auto error = validateBars(primary)) {
        return {.error = QStringLiteral("Primary series is invalid: %1")
                             .arg(QString::fromStdString(*error))};
    }
    if (const auto error = validateBars(comparison)) {
        return {.error = QStringLiteral("Comparison series is invalid: %1")
                             .arg(QString::fromStdString(*error))};
    }

    ComparisonAnalysis result;
    result.points.reserve(std::min(primary.size(), comparison.size()));
    auto primaryIndex = std::size_t{};
    auto comparisonIndex = std::size_t{};
    while (primaryIndex < primary.size() &&
           comparisonIndex < comparison.size()) {
        const auto& left = primary[primaryIndex];
        const auto& right = comparison[comparisonIndex];
        if (left.timestamp < right.timestamp) {
            ++primaryIndex;
            continue;
        }
        if (right.timestamp < left.timestamp) {
            ++comparisonIndex;
            continue;
        }
        result.points.push_back({
            .timestamp = left.timestamp,
            .primaryNormalized = left.close,
            .comparisonNormalized = right.close,
        });
        ++primaryIndex;
        ++comparisonIndex;
    }

    if (result.points.size() < 2) {
        result.points.clear();
        result.error =
            QStringLiteral("At least two exact common bar timestamps are required.");
        return result;
    }

    const auto primaryBase = result.points.front().primaryNormalized;
    const auto comparisonBase = result.points.front().comparisonNormalized;
    for (auto& point : result.points) {
        point.primaryNormalized = 100.0 * point.primaryNormalized / primaryBase;
        point.comparisonNormalized =
            100.0 * point.comparisonNormalized / comparisonBase;
        point.relativeNormalized =
            100.0 * point.primaryNormalized / point.comparisonNormalized;
    }

    result.primaryReturnPercent =
        result.points.back().primaryNormalized - 100.0;
    result.comparisonReturnPercent =
        result.points.back().comparisonNormalized - 100.0;
    result.relativeReturnPercent =
        result.points.back().relativeNormalized - 100.0;

    std::vector<double> primaryReturns;
    std::vector<double> comparisonReturns;
    primaryReturns.reserve(result.points.size() - 1);
    comparisonReturns.reserve(result.points.size() - 1);
    for (std::size_t index = 1; index < result.points.size(); ++index) {
        primaryReturns.push_back(std::log(
            result.points[index].primaryNormalized /
            result.points[index - 1].primaryNormalized));
        comparisonReturns.push_back(std::log(
            result.points[index].comparisonNormalized /
            result.points[index - 1].comparisonNormalized));
    }

    const auto mean = [](const std::vector<double>& values) {
        return std::accumulate(values.begin(), values.end(), 0.0) /
               static_cast<double>(values.size());
    };
    const auto primaryMean = mean(primaryReturns);
    const auto comparisonMean = mean(comparisonReturns);
    auto covariance = 0.0;
    auto primarySquared = 0.0;
    auto comparisonSquared = 0.0;
    for (std::size_t index = 0; index < primaryReturns.size(); ++index) {
        const auto primaryDelta = primaryReturns[index] - primaryMean;
        const auto comparisonDelta =
            comparisonReturns[index] - comparisonMean;
        covariance += primaryDelta * comparisonDelta;
        primarySquared += primaryDelta * primaryDelta;
        comparisonSquared += comparisonDelta * comparisonDelta;
    }
    const auto denominator = std::sqrt(primarySquared * comparisonSquared);
    if (denominator > 0.0 && std::isfinite(denominator)) {
        result.returnCorrelation =
            std::clamp(covariance / denominator, -1.0, 1.0);
    }
    return result;
}

} // namespace tvchart
