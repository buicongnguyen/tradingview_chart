#pragma once

#include "strategy/strategy_engine.hpp"
#include "strategy/strategy_models.hpp"

#include <QString>

#include <cstdint>
#include <vector>

namespace tvchart {

enum class PineDiagnosticSeverity : std::uint8_t {
    Information,
    Warning,
    Error,
};

struct PineDiagnostic {
    PineDiagnosticSeverity severity{PineDiagnosticSeverity::Error};
    int line{};
    int column{1};
    QString message;
};

struct PineStrategyImportResult {
    StrategyDefinition strategy;
    BacktestParameters execution;
    std::vector<PineDiagnostic> diagnostics;

    [[nodiscard]] bool ok() const noexcept;
};

[[nodiscard]] QString pineDiagnosticSeverityLabel(
    PineDiagnosticSeverity severity);
[[nodiscard]] PineStrategyImportResult importPineStrategy(
    const QString& source);
[[nodiscard]] QString pineNativeStrategyPreview(
    const PineStrategyImportResult& result);
[[nodiscard]] QString pineStrategyExample();

} // namespace tvchart
