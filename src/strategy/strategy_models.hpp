#pragma once

#include <QByteArray>
#include <QString>

#include <cstdint>
#include <optional>
#include <vector>

namespace tvchart {

enum class StrategyField : std::uint8_t {
    Open,
    High,
    Low,
    Close,
    Volume,
    SimpleMovingAverage,
    ExponentialMovingAverage,
    RelativeStrengthIndex,
    VolumeRatio,
};

enum class StrategyComparison : std::uint8_t {
    GreaterThan,
    LessThan,
    CrossesAbove,
    CrossesBelow,
};

enum class ConditionMatch : std::uint8_t {
    All,
    Any,
};

struct StrategyOperand {
    StrategyField field{StrategyField::Close};
    std::uint32_t period{20};
    double multiplier{1.0};

    [[nodiscard]] bool operator==(const StrategyOperand&) const = default;
};

struct StrategyCondition {
    StrategyOperand left;
    StrategyComparison comparison{StrategyComparison::GreaterThan};
    std::optional<StrategyOperand> right;
    double constant{};

    [[nodiscard]] bool operator==(const StrategyCondition&) const = default;
};

struct ConditionGroup {
    ConditionMatch match{ConditionMatch::All};
    std::vector<StrategyCondition> conditions;

    [[nodiscard]] bool operator==(const ConditionGroup&) const = default;
};

struct StrategyDefinition {
    static constexpr int currentSchemaVersion{1};

    QString id;
    QString name;
    ConditionGroup entry;
    ConditionGroup exit;

    [[nodiscard]] bool operator==(const StrategyDefinition&) const = default;
};

struct StrategyLoadResult {
    StrategyDefinition strategy;
    QString error;

    [[nodiscard]] bool ok() const noexcept { return error.isEmpty(); }
};

[[nodiscard]] QString strategyFieldId(StrategyField field);
[[nodiscard]] QString strategyFieldLabel(StrategyField field);
[[nodiscard]] QString strategyComparisonId(StrategyComparison comparison);
[[nodiscard]] QString strategyComparisonLabel(StrategyComparison comparison);
[[nodiscard]] QString validateStrategyOperand(const StrategyOperand& operand);
[[nodiscard]] QString validateConditionGroup(const ConditionGroup& group);
[[nodiscard]] QString validateStrategy(const StrategyDefinition& strategy);
[[nodiscard]] QByteArray serializeStrategy(const StrategyDefinition& strategy);
[[nodiscard]] StrategyLoadResult deserializeStrategy(const QByteArray& json);

} // namespace tvchart
