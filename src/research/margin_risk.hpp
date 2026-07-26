#pragma once

#include <optional>
#include <string>

namespace tvchart {

struct MarginRiskInput {
    double longMarketValue{};
    double marginDebit{};
    double otherEquity{};
    double maintenanceRate{0.25};
    double stressPercent{-10.0};
};

struct MarginRiskResult {
    double currentEquity{};
    double currentRequirement{};
    double currentCushion{};
    double currentEquityPercent{};
    double stressedMarketValue{};
    double stressedEquity{};
    double stressedRequirement{};
    double stressedCushion{};
    std::optional<double> callMarketValue;
    std::optional<double> callDeclinePercent;
    bool currentDeficiency{};
    bool stressedDeficiency{};
};

[[nodiscard]] std::optional<std::string> validateMarginRiskInput(
    const MarginRiskInput& input) noexcept;
[[nodiscard]] MarginRiskResult calculateMarginRisk(
    const MarginRiskInput& input);

} // namespace tvchart
