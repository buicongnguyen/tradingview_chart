#include "strategy/strategy_models.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>

#include <cmath>
#include <limits>
#include <optional>
#include <utility>

namespace tvchart {
namespace {

constexpr auto kMaximumConditions = std::size_t{16};
constexpr auto kMaximumPeriod = std::uint32_t{500};
constexpr auto kMaximumAbsoluteConstant = 1.0e12;
constexpr auto kMaximumMultiplier = 1.0e6;

[[nodiscard]] bool periodField(const StrategyField field) noexcept {
    return field == StrategyField::SimpleMovingAverage ||
           field == StrategyField::ExponentialMovingAverage ||
           field == StrategyField::RelativeStrengthIndex ||
           field == StrategyField::VolumeRatio;
}

[[nodiscard]] std::optional<StrategyField> parseField(const QString& id) {
    if (id == QStringLiteral("open")) {
        return StrategyField::Open;
    }
    if (id == QStringLiteral("high")) {
        return StrategyField::High;
    }
    if (id == QStringLiteral("low")) {
        return StrategyField::Low;
    }
    if (id == QStringLiteral("close")) {
        return StrategyField::Close;
    }
    if (id == QStringLiteral("volume")) {
        return StrategyField::Volume;
    }
    if (id == QStringLiteral("sma")) {
        return StrategyField::SimpleMovingAverage;
    }
    if (id == QStringLiteral("ema")) {
        return StrategyField::ExponentialMovingAverage;
    }
    if (id == QStringLiteral("rsi")) {
        return StrategyField::RelativeStrengthIndex;
    }
    if (id == QStringLiteral("volume-ratio")) {
        return StrategyField::VolumeRatio;
    }
    return std::nullopt;
}

[[nodiscard]] std::optional<StrategyComparison> parseComparison(
    const QString& id) {
    if (id == QStringLiteral("greater-than")) {
        return StrategyComparison::GreaterThan;
    }
    if (id == QStringLiteral("less-than")) {
        return StrategyComparison::LessThan;
    }
    if (id == QStringLiteral("crosses-above")) {
        return StrategyComparison::CrossesAbove;
    }
    if (id == QStringLiteral("crosses-below")) {
        return StrategyComparison::CrossesBelow;
    }
    return std::nullopt;
}

[[nodiscard]] QJsonObject operandToJson(const StrategyOperand& operand) {
    return {
        {QStringLiteral("field"), strategyFieldId(operand.field)},
        {QStringLiteral("period"), static_cast<int>(operand.period)},
        {QStringLiteral("multiplier"), operand.multiplier},
    };
}

[[nodiscard]] std::optional<StrategyOperand> operandFromJson(
    const QJsonValue& value) {
    if (!value.isObject()) {
        return std::nullopt;
    }
    const auto object = value.toObject();
    const auto field =
        parseField(object.value(QStringLiteral("field")).toString());
    const auto period = object.value(QStringLiteral("period")).toInt(-1);
    const auto multiplier =
        object.value(QStringLiteral("multiplier")).toDouble(
            std::numeric_limits<double>::quiet_NaN());
    if (!field || period < 0) {
        return std::nullopt;
    }
    StrategyOperand operand{
        .field = *field,
        .period = static_cast<std::uint32_t>(period),
        .multiplier = multiplier,
    };
    if (!validateStrategyOperand(operand).isEmpty()) {
        return std::nullopt;
    }
    return operand;
}

[[nodiscard]] QJsonObject groupToJson(const ConditionGroup& group) {
    QJsonArray conditions;
    for (const auto& condition : group.conditions) {
        QJsonObject object{
            {QStringLiteral("left"), operandToJson(condition.left)},
            {
                QStringLiteral("comparison"),
                strategyComparisonId(condition.comparison),
            },
            {QStringLiteral("constant"), condition.constant},
        };
        if (condition.right) {
            object.insert(
                QStringLiteral("right"),
                operandToJson(*condition.right));
        }
        conditions.append(object);
    }
    return {
        {
            QStringLiteral("match"),
            group.match == ConditionMatch::Any
                ? QStringLiteral("any")
                : QStringLiteral("all"),
        },
        {QStringLiteral("conditions"), conditions},
    };
}

[[nodiscard]] std::optional<ConditionGroup> groupFromJson(
    const QJsonValue& value) {
    if (!value.isObject()) {
        return std::nullopt;
    }
    const auto object = value.toObject();
    const auto match = object.value(QStringLiteral("match")).toString();
    if (match != QStringLiteral("all") && match != QStringLiteral("any")) {
        return std::nullopt;
    }
    const auto array = object.value(QStringLiteral("conditions"));
    if (!array.isArray()) {
        return std::nullopt;
    }

    ConditionGroup group{
        .match =
            match == QStringLiteral("any")
                ? ConditionMatch::Any
                : ConditionMatch::All,
    };
    for (const auto& conditionValue : array.toArray()) {
        if (!conditionValue.isObject()) {
            return std::nullopt;
        }
        const auto conditionObject = conditionValue.toObject();
        const auto left =
            operandFromJson(conditionObject.value(QStringLiteral("left")));
        const auto comparison = parseComparison(
            conditionObject.value(QStringLiteral("comparison")).toString());
        if (!left || !comparison) {
            return std::nullopt;
        }
        std::optional<StrategyOperand> right;
        if (conditionObject.contains(QStringLiteral("right"))) {
            right =
                operandFromJson(conditionObject.value(QStringLiteral("right")));
            if (!right) {
                return std::nullopt;
            }
        }
        const auto constant =
            conditionObject.value(QStringLiteral("constant")).toDouble(
                std::numeric_limits<double>::quiet_NaN());
        group.conditions.push_back({
            .left = *left,
            .comparison = *comparison,
            .right = right,
            .constant = constant,
        });
    }
    if (!validateConditionGroup(group).isEmpty()) {
        return std::nullopt;
    }
    return group;
}

} // namespace

QString strategyFieldId(const StrategyField field) {
    switch (field) {
    case StrategyField::Open:
        return QStringLiteral("open");
    case StrategyField::High:
        return QStringLiteral("high");
    case StrategyField::Low:
        return QStringLiteral("low");
    case StrategyField::Close:
        return QStringLiteral("close");
    case StrategyField::Volume:
        return QStringLiteral("volume");
    case StrategyField::SimpleMovingAverage:
        return QStringLiteral("sma");
    case StrategyField::ExponentialMovingAverage:
        return QStringLiteral("ema");
    case StrategyField::RelativeStrengthIndex:
        return QStringLiteral("rsi");
    case StrategyField::VolumeRatio:
        return QStringLiteral("volume-ratio");
    }
    return QStringLiteral("close");
}

QString strategyFieldLabel(const StrategyField field) {
    switch (field) {
    case StrategyField::Open:
        return QStringLiteral("Open");
    case StrategyField::High:
        return QStringLiteral("High");
    case StrategyField::Low:
        return QStringLiteral("Low");
    case StrategyField::Close:
        return QStringLiteral("Close");
    case StrategyField::Volume:
        return QStringLiteral("Volume");
    case StrategyField::SimpleMovingAverage:
        return QStringLiteral("SMA");
    case StrategyField::ExponentialMovingAverage:
        return QStringLiteral("EMA");
    case StrategyField::RelativeStrengthIndex:
        return QStringLiteral("RSI");
    case StrategyField::VolumeRatio:
        return QStringLiteral("Volume ratio");
    }
    return QStringLiteral("Close");
}

QString strategyComparisonId(const StrategyComparison comparison) {
    switch (comparison) {
    case StrategyComparison::GreaterThan:
        return QStringLiteral("greater-than");
    case StrategyComparison::LessThan:
        return QStringLiteral("less-than");
    case StrategyComparison::CrossesAbove:
        return QStringLiteral("crosses-above");
    case StrategyComparison::CrossesBelow:
        return QStringLiteral("crosses-below");
    }
    return QStringLiteral("greater-than");
}

QString strategyComparisonLabel(const StrategyComparison comparison) {
    switch (comparison) {
    case StrategyComparison::GreaterThan:
        return QStringLiteral(">");
    case StrategyComparison::LessThan:
        return QStringLiteral("<");
    case StrategyComparison::CrossesAbove:
        return QStringLiteral("crosses above");
    case StrategyComparison::CrossesBelow:
        return QStringLiteral("crosses below");
    }
    return QStringLiteral(">");
}

QString validateStrategyOperand(const StrategyOperand& operand) {
    if (!std::isfinite(operand.multiplier) || operand.multiplier <= 0.0 ||
        operand.multiplier > kMaximumMultiplier) {
        return QStringLiteral("Strategy operand multiplier is invalid.");
    }
    if (periodField(operand.field) &&
        (operand.period == 0 || operand.period > kMaximumPeriod)) {
        return QStringLiteral("Strategy indicator period is invalid.");
    }
    return {};
}

QString validateConditionGroup(const ConditionGroup& group) {
    if (group.conditions.empty() ||
        group.conditions.size() > kMaximumConditions) {
        return QStringLiteral(
            "A condition group requires between 1 and 16 conditions.");
    }
    for (const auto& condition : group.conditions) {
        if (!validateStrategyOperand(condition.left).isEmpty() ||
            (condition.right &&
             !validateStrategyOperand(*condition.right).isEmpty()) ||
            !std::isfinite(condition.constant) ||
            std::abs(condition.constant) > kMaximumAbsoluteConstant) {
            return QStringLiteral("A strategy condition is invalid.");
        }
    }
    return {};
}

QString validateStrategy(const StrategyDefinition& strategy) {
    if (strategy.id.trimmed().isEmpty() || strategy.id.size() > 120 ||
        strategy.name.trimmed().isEmpty() || strategy.name.size() > 120) {
        return QStringLiteral("Strategy identity or name is invalid.");
    }
    if (const auto error = validateConditionGroup(strategy.entry);
        !error.isEmpty()) {
        return QStringLiteral("Entry rule: %1").arg(error);
    }
    if (const auto error = validateConditionGroup(strategy.exit);
        !error.isEmpty()) {
        return QStringLiteral("Exit rule: %1").arg(error);
    }
    return {};
}

QByteArray serializeStrategy(const StrategyDefinition& strategy) {
    if (!validateStrategy(strategy).isEmpty()) {
        return {};
    }
    const QJsonObject object{
        {
            QStringLiteral("schemaVersion"),
            StrategyDefinition::currentSchemaVersion,
        },
        {QStringLiteral("id"), strategy.id},
        {QStringLiteral("name"), strategy.name},
        {QStringLiteral("entry"), groupToJson(strategy.entry)},
        {QStringLiteral("exit"), groupToJson(strategy.exit)},
    };
    return QJsonDocument(object).toJson(QJsonDocument::Compact);
}

StrategyLoadResult deserializeStrategy(const QByteArray& json) {
    QJsonParseError parseError;
    const auto document = QJsonDocument::fromJson(json, &parseError);
    if (parseError.error != QJsonParseError::NoError ||
        !document.isObject()) {
        return {.error = QStringLiteral("Saved strategy JSON is invalid.")};
    }
    const auto object = document.object();
    if (object.value(QStringLiteral("schemaVersion")).toInt(-1) !=
        StrategyDefinition::currentSchemaVersion) {
        return {.error = QStringLiteral("Saved strategy schema is unsupported.")};
    }
    const auto entry = groupFromJson(object.value(QStringLiteral("entry")));
    const auto exit = groupFromJson(object.value(QStringLiteral("exit")));
    if (!entry || !exit) {
        return {.error = QStringLiteral("Saved strategy conditions are invalid.")};
    }
    StrategyDefinition strategy{
        .id = object.value(QStringLiteral("id")).toString(),
        .name = object.value(QStringLiteral("name")).toString(),
        .entry = *entry,
        .exit = *exit,
    };
    if (const auto error = validateStrategy(strategy); !error.isEmpty()) {
        return {.error = error};
    }
    return {.strategy = std::move(strategy)};
}

} // namespace tvchart
