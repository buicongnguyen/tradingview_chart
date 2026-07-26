#include "research/margin_risk.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace tvchart {

std::optional<std::string> validateMarginRiskInput(
    const MarginRiskInput& input) noexcept {
    const auto finite = [](const double value) {
        return std::isfinite(value);
    };
    if (!finite(input.longMarketValue) || input.longMarketValue <= 0.0) {
        return "Long market value must be finite and positive.";
    }
    if (!finite(input.marginDebit) || input.marginDebit < 0.0 ||
        !finite(input.otherEquity) || input.otherEquity < 0.0) {
        return "Margin debit and other equity must be finite and non-negative.";
    }
    if (!finite(input.maintenanceRate) ||
        input.maintenanceRate <= 0.0 ||
        input.maintenanceRate >= 1.0) {
        return "Maintenance rate must be greater than 0% and less than 100%.";
    }
    if (!finite(input.stressPercent) ||
        input.stressPercent <= -100.0 ||
        input.stressPercent > 100.0) {
        return "Stress change must be greater than -100% and at most 100%.";
    }
    return std::nullopt;
}

MarginRiskResult calculateMarginRisk(const MarginRiskInput& input) {
    if (const auto error = validateMarginRiskInput(input)) {
        throw std::invalid_argument(*error);
    }
    MarginRiskResult result;
    result.currentEquity =
        input.longMarketValue + input.otherEquity - input.marginDebit;
    result.currentRequirement =
        input.longMarketValue * input.maintenanceRate;
    result.currentCushion =
        result.currentEquity - result.currentRequirement;
    result.currentEquityPercent =
        result.currentEquity / input.longMarketValue * 100.0;
    result.stressedMarketValue =
        input.longMarketValue * (1.0 + input.stressPercent / 100.0);
    result.stressedEquity =
        result.stressedMarketValue + input.otherEquity - input.marginDebit;
    result.stressedRequirement =
        result.stressedMarketValue * input.maintenanceRate;
    result.stressedCushion =
        result.stressedEquity - result.stressedRequirement;
    result.currentDeficiency = result.currentCushion < 0.0;
    result.stressedDeficiency = result.stressedCushion < 0.0;

    const auto financedAmount = input.marginDebit - input.otherEquity;
    if (financedAmount > 0.0) {
        const auto threshold =
            financedAmount / (1.0 - input.maintenanceRate);
        result.callMarketValue = threshold;
        result.callDeclinePercent =
            (threshold / input.longMarketValue - 1.0) * 100.0;
    }
    return result;
}

} // namespace tvchart
