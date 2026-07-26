#include "strategy/pine_strategy_importer.hpp"

#include <QCryptographicHash>
#include <QHash>
#include <QRegularExpression>
#include <QSet>
#include <QStringList>

#include <algorithm>
#include <cmath>
#include <optional>
#include <ranges>
#include <utility>

namespace tvchart {
namespace {

constexpr auto kMaximumSourceBytes = 64 * 1024;
constexpr auto kMaximumLines = 1'000;
constexpr auto kMaximumSymbols = 128;

struct LogicalStatement {
    QString text;
    int line{};
    int indentation{};
};

struct ParsedGroup {
    ConditionMatch match{ConditionMatch::All};
    std::vector<StrategyCondition> conditions;
};

struct ParseContext {
    PineStrategyImportResult result;
    QHash<QString, double> numbers;
    QHash<QString, StrategyOperand> operands;
    QHash<QString, ParsedGroup> groups;
    QString title{QStringLiteral("Imported strategy")};
    bool declarationSeen{};
    bool entrySeen{};
    bool exitSeen{};
};

void addDiagnostic(
    ParseContext& context,
    const PineDiagnosticSeverity severity,
    const int line,
    QString message,
    const int column = 1) {
    context.result.diagnostics.push_back({
        .severity = severity,
        .line = line,
        .column = std::max(1, column),
        .message = std::move(message),
    });
}

[[nodiscard]] QString withoutLineComment(const QString& line) {
    bool quoted = false;
    bool escaped = false;
    for (auto index = 0; index + 1 < line.size(); ++index) {
        const auto value = line[index];
        if (escaped) {
            escaped = false;
            continue;
        }
        if (value == u'\\' && quoted) {
            escaped = true;
            continue;
        }
        if (value == u'"') {
            quoted = !quoted;
            continue;
        }
        if (!quoted && value == u'/' && line[index + 1] == u'/') {
            return line.left(index);
        }
    }
    return line;
}

[[nodiscard]] int parenthesisDelta(const QString& text) {
    auto result = 0;
    bool quoted = false;
    bool escaped = false;
    for (const auto value : text) {
        if (escaped) {
            escaped = false;
            continue;
        }
        if (value == u'\\' && quoted) {
            escaped = true;
            continue;
        }
        if (value == u'"') {
            quoted = !quoted;
        } else if (!quoted && value == u'(') {
            ++result;
        } else if (!quoted && value == u')') {
            --result;
        }
    }
    return result;
}

[[nodiscard]] std::vector<LogicalStatement> logicalStatements(
    const QStringList& lines,
    ParseContext& context) {
    std::vector<LogicalStatement> result;
    QString current;
    auto startLine = 0;
    auto indentation = 0;
    auto balance = 0;
    for (auto index = 0; index < lines.size(); ++index) {
        const auto raw = lines[index];
        if (raw.trimmed().startsWith(QStringLiteral("//@version"))) {
            continue;
        }
        const auto uncommented = withoutLineComment(raw);
        const auto trimmed = uncommented.trimmed();
        if (trimmed.isEmpty()) {
            continue;
        }
        auto leading = 0;
        while (leading < uncommented.size() &&
               uncommented[leading].isSpace()) {
            leading += uncommented[leading] == u'\t' ? 4 : 1;
        }
        if (current.isEmpty()) {
            current = trimmed;
            startLine = index + 1;
            indentation = leading;
            balance = parenthesisDelta(trimmed);
        } else {
            current += u' ';
            current += trimmed;
            balance += parenthesisDelta(trimmed);
        }
        if (balance < 0) {
            addDiagnostic(
                context,
                PineDiagnosticSeverity::Error,
                index + 1,
                QStringLiteral("Unexpected closing parenthesis."));
            current.clear();
            balance = 0;
        } else if (balance == 0) {
            result.push_back({
                .text = std::move(current),
                .line = startLine,
                .indentation = indentation,
            });
            current.clear();
        }
    }
    if (!current.isEmpty()) {
        addDiagnostic(
            context,
            PineDiagnosticSeverity::Error,
            startLine,
            QStringLiteral("Unclosed parenthesis in statement."));
    }
    return result;
}

[[nodiscard]] QString stripOuterParentheses(QString expression) {
    expression = expression.trimmed();
    while (expression.size() >= 2 &&
           expression.front() == u'(' &&
           expression.back() == u')') {
        auto balance = 0;
        bool enclosesAll = true;
        bool quoted = false;
        for (auto index = 0; index < expression.size(); ++index) {
            const auto value = expression[index];
            if (value == u'"') {
                quoted = !quoted;
            } else if (!quoted && value == u'(') {
                ++balance;
            } else if (!quoted && value == u')') {
                --balance;
                if (balance == 0 && index + 1 < expression.size()) {
                    enclosesAll = false;
                    break;
                }
            }
        }
        if (!enclosesAll || balance != 0) {
            break;
        }
        expression = expression.mid(1, expression.size() - 2).trimmed();
    }
    return expression;
}

[[nodiscard]] QStringList splitTopLevel(
    const QString& text,
    const QChar delimiter) {
    QStringList result;
    auto start = 0;
    auto depth = 0;
    bool quoted = false;
    bool escaped = false;
    for (auto index = 0; index < text.size(); ++index) {
        const auto value = text[index];
        if (escaped) {
            escaped = false;
            continue;
        }
        if (value == u'\\' && quoted) {
            escaped = true;
            continue;
        }
        if (value == u'"') {
            quoted = !quoted;
        } else if (!quoted && value == u'(') {
            ++depth;
        } else if (!quoted && value == u')') {
            --depth;
        } else if (!quoted && depth == 0 && value == delimiter) {
            result.push_back(text.mid(start, index - start).trimmed());
            start = index + 1;
        }
    }
    result.push_back(text.mid(start).trimmed());
    return result;
}

[[nodiscard]] QStringList splitTopLevelWord(
    const QString& text,
    const QString& word) {
    QStringList result;
    auto start = 0;
    auto depth = 0;
    bool quoted = false;
    const auto marker = u' ' + word + u' ';
    for (auto index = 0;
         index + marker.size() <= text.size();
         ++index) {
        const auto value = text[index];
        if (value == u'"') {
            quoted = !quoted;
            continue;
        }
        if (quoted) {
            continue;
        }
        if (value == u'(') {
            ++depth;
        } else if (value == u')') {
            --depth;
        } else if (depth == 0 &&
                   text.mid(index, marker.size()) == marker) {
            result.push_back(
                text.mid(start, index - start).trimmed());
            index += marker.size() - 1;
            start = index + 1;
        }
    }
    result.push_back(text.mid(start).trimmed());
    return result;
}

[[nodiscard]] bool functionCall(
    const QString& expression,
    const QString& name,
    QStringList& arguments) {
    const auto trimmed = expression.trimmed();
    if (!trimmed.startsWith(name + u'(') ||
        !trimmed.endsWith(u')')) {
        return false;
    }
    const auto inner = trimmed.mid(
        name.size() + 1,
        trimmed.size() - name.size() - 2);
    arguments = splitTopLevel(inner, u',');
    return true;
}

[[nodiscard]] std::optional<double> numericValue(
    const QString& expression,
    const ParseContext& context) {
    const auto trimmed = stripOuterParentheses(expression);
    bool ok = false;
    const auto literal = trimmed.toDouble(&ok);
    if (ok && std::isfinite(literal)) {
        return literal;
    }
    const auto found = context.numbers.constFind(trimmed);
    return found == context.numbers.cend()
               ? std::nullopt
               : std::optional<double>{*found};
}

[[nodiscard]] std::optional<StrategyOperand> parseOperand(
    const QString& expression,
    ParseContext& context,
    const int line,
    const bool reportErrors = true) {
    const auto trimmed = stripOuterParentheses(expression);
    const auto found = context.operands.constFind(trimmed);
    if (found != context.operands.cend()) {
        return *found;
    }
    const QHash<QString, StrategyField> rawFields{
        {QStringLiteral("open"), StrategyField::Open},
        {QStringLiteral("high"), StrategyField::High},
        {QStringLiteral("low"), StrategyField::Low},
        {QStringLiteral("close"), StrategyField::Close},
        {QStringLiteral("volume"), StrategyField::Volume},
    };
    if (const auto raw = rawFields.constFind(trimmed);
        raw != rawFields.cend()) {
        return StrategyOperand{.field = *raw};
    }

    struct IndicatorSyntax {
        QString name;
        StrategyField field;
    };
    const std::array syntaxes{
        IndicatorSyntax{
            QStringLiteral("ta.sma"),
            StrategyField::SimpleMovingAverage},
        IndicatorSyntax{
            QStringLiteral("ta.ema"),
            StrategyField::ExponentialMovingAverage},
        IndicatorSyntax{
            QStringLiteral("ta.rsi"),
            StrategyField::RelativeStrengthIndex},
    };
    for (const auto& syntax : syntaxes) {
        QStringList arguments;
        if (!functionCall(trimmed, syntax.name, arguments)) {
            continue;
        }
        if (arguments.size() != 2 ||
            arguments.front().trimmed() != QStringLiteral("close")) {
            if (reportErrors) {
                addDiagnostic(
                    context,
                    PineDiagnosticSeverity::Error,
                    line,
                    QStringLiteral(
                        "%1 supports only close and one numeric period in this importer.")
                        .arg(syntax.name));
            }
            return std::nullopt;
        }
        const auto periodValue =
            numericValue(arguments[1], context);
        if (!periodValue ||
            *periodValue < 1.0 ||
            *periodValue > 500.0 ||
            std::floor(*periodValue) != *periodValue) {
            if (reportErrors) {
                addDiagnostic(
                    context,
                    PineDiagnosticSeverity::Error,
                    line,
                    QStringLiteral(
                        "%1 period must resolve to an integer from 1 to 500.")
                        .arg(syntax.name));
            }
            return std::nullopt;
        }
        return StrategyOperand{
            .field = syntax.field,
            .period = static_cast<std::uint32_t>(*periodValue),
        };
    }
    if (reportErrors) {
        addDiagnostic(
            context,
            PineDiagnosticSeverity::Error,
            line,
            QStringLiteral("Unsupported series expression “%1”.")
                .arg(trimmed.left(120)));
    }
    return std::nullopt;
}

[[nodiscard]] std::optional<StrategyCondition> parseAtomicCondition(
    const QString& expression,
    ParseContext& context,
    const int line) {
    const auto trimmed = stripOuterParentheses(expression);
    QStringList arguments;
    StrategyComparison comparison{};
    if (functionCall(trimmed, QStringLiteral("ta.crossover"), arguments) ||
        functionCall(trimmed, QStringLiteral("ta.crossunder"), arguments)) {
        const auto crossesAbove =
            trimmed.startsWith(QStringLiteral("ta.crossover("));
        comparison =
            crossesAbove
                ? StrategyComparison::CrossesAbove
                : StrategyComparison::CrossesBelow;
        if (arguments.size() != 2) {
            addDiagnostic(
                context,
                PineDiagnosticSeverity::Error,
                line,
                QStringLiteral(
                    "Crossover functions require exactly two operands."));
            return std::nullopt;
        }
        const auto left =
            parseOperand(arguments.front(), context, line);
        if (!left) {
            return std::nullopt;
        }
        if (const auto right =
                parseOperand(arguments[1], context, line, false)) {
            return StrategyCondition{
                .left = *left,
                .comparison = comparison,
                .right = *right,
            };
        }
        if (const auto constant =
                numericValue(arguments[1], context)) {
            return StrategyCondition{
                .left = *left,
                .comparison = comparison,
                .constant = *constant,
            };
        }
        addDiagnostic(
            context,
            PineDiagnosticSeverity::Error,
            line,
            QStringLiteral(
                "Crossover right side must be a supported series or numeric value."));
        return std::nullopt;
    }

    auto operatorIndex = -1;
    QChar operatorValue;
    auto depth = 0;
    for (auto index = 0; index < trimmed.size(); ++index) {
        const auto value = trimmed[index];
        if (value == u'(') {
            ++depth;
        } else if (value == u')') {
            --depth;
        } else if (depth == 0 && (value == u'>' || value == u'<')) {
            if (index + 1 < trimmed.size() &&
                trimmed[index + 1] == u'=') {
                addDiagnostic(
                    context,
                    PineDiagnosticSeverity::Error,
                    line,
                    QStringLiteral(
                        "Only strict > and < comparisons are supported."));
                return std::nullopt;
            }
            operatorIndex = index;
            operatorValue = value;
            break;
        }
    }
    if (operatorIndex < 0) {
        const auto group = context.groups.constFind(trimmed);
        if (group != context.groups.cend() &&
            group->conditions.size() == 1) {
            return group->conditions.front();
        }
        addDiagnostic(
            context,
            PineDiagnosticSeverity::Error,
            line,
            QStringLiteral("Unsupported condition “%1”.")
                .arg(trimmed.left(120)));
        return std::nullopt;
    }
    const auto leftText = trimmed.left(operatorIndex).trimmed();
    const auto rightText = trimmed.mid(operatorIndex + 1).trimmed();
    const auto left = parseOperand(leftText, context, line);
    if (!left) {
        return std::nullopt;
    }
    comparison =
        operatorValue == u'>'
            ? StrategyComparison::GreaterThan
            : StrategyComparison::LessThan;
    if (const auto right =
            parseOperand(rightText, context, line, false)) {
        return StrategyCondition{
            .left = *left,
            .comparison = comparison,
            .right = *right,
        };
    }
    if (const auto constant = numericValue(rightText, context)) {
        return StrategyCondition{
            .left = *left,
            .comparison = comparison,
            .constant = *constant,
        };
    }
    addDiagnostic(
        context,
        PineDiagnosticSeverity::Error,
        line,
        QStringLiteral(
            "Comparison right side must be a supported series or numeric value."));
    return std::nullopt;
}

[[nodiscard]] std::optional<ParsedGroup> parseGroup(
    const QString& expression,
    ParseContext& context,
    const int line) {
    auto trimmed = stripOuterParentheses(expression);
    if (const auto found = context.groups.constFind(trimmed);
        found != context.groups.cend()) {
        return *found;
    }
    const auto andParts =
        splitTopLevelWord(trimmed, QStringLiteral("and"));
    const auto orParts =
        splitTopLevelWord(trimmed, QStringLiteral("or"));
    const auto hasAnd = andParts.size() > 1;
    const auto hasOr = orParts.size() > 1;
    if (hasAnd && hasOr) {
        addDiagnostic(
            context,
            PineDiagnosticSeverity::Error,
            line,
            QStringLiteral(
                "Mixed or nested and/or expressions are unsupported; use one flat operator."));
        return std::nullopt;
    }
    const auto parts =
        hasAnd ? andParts : (hasOr ? orParts : QStringList{trimmed});
    ParsedGroup result{
        .match = hasOr ? ConditionMatch::Any : ConditionMatch::All,
    };
    for (const auto& part : parts) {
        if (const auto referenced = context.groups.constFind(
                stripOuterParentheses(part));
            referenced != context.groups.cend()) {
            if (referenced->conditions.size() > 1 &&
                referenced->match != result.match) {
                addDiagnostic(
                    context,
                    PineDiagnosticSeverity::Error,
                    line,
                    QStringLiteral(
                        "Nested condition groups with different boolean operators are unsupported."));
                return std::nullopt;
            }
            result.conditions.insert(
                result.conditions.end(),
                referenced->conditions.begin(),
                referenced->conditions.end());
        } else {
            const auto condition =
                parseAtomicCondition(part, context, line);
            if (!condition) {
                return std::nullopt;
            }
            result.conditions.push_back(*condition);
        }
    }
    if (result.conditions.empty() ||
        result.conditions.size() > 16) {
        addDiagnostic(
            context,
            PineDiagnosticSeverity::Error,
            line,
            QStringLiteral(
                "A compiled entry or exit requires between 1 and 16 conditions."));
        return std::nullopt;
    }
    return result;
}

[[nodiscard]] QHash<QString, QString> namedArguments(
    const QStringList& arguments) {
    QHash<QString, QString> result;
    for (const auto& argument : arguments) {
        auto depth = 0;
        bool quoted = false;
        for (auto index = 0; index < argument.size(); ++index) {
            const auto value = argument[index];
            if (value == u'"') {
                quoted = !quoted;
            } else if (!quoted && value == u'(') {
                ++depth;
            } else if (!quoted && value == u')') {
                --depth;
            } else if (!quoted && depth == 0 && value == u'=') {
                result.insert(
                    argument.left(index).trimmed(),
                    argument.mid(index + 1).trimmed());
                break;
            }
        }
    }
    return result;
}

[[nodiscard]] QString unquoted(QString text) {
    text = text.trimmed();
    if (text.size() >= 2 &&
        text.front() == u'"' &&
        text.back() == u'"') {
        return text.mid(1, text.size() - 2);
    }
    return {};
}

void parseStrategyDeclaration(
    const LogicalStatement& statement,
    ParseContext& context) {
    QStringList arguments;
    if (!functionCall(
            statement.text,
            QStringLiteral("strategy"),
            arguments)) {
        return;
    }
    if (context.declarationSeen) {
        addDiagnostic(
            context,
            PineDiagnosticSeverity::Error,
            statement.line,
            QStringLiteral("Only one strategy() declaration is allowed."));
        return;
    }
    context.declarationSeen = true;
    if (arguments.isEmpty()) {
        addDiagnostic(
            context,
            PineDiagnosticSeverity::Error,
            statement.line,
            QStringLiteral("strategy() requires a title."));
        return;
    }
    if (const auto title = unquoted(arguments.front());
        !title.isEmpty()) {
        context.title = title.left(120);
    } else {
        addDiagnostic(
            context,
            PineDiagnosticSeverity::Error,
            statement.line,
            QStringLiteral(
                "The first strategy() argument must be a quoted title."));
    }
    const auto named = namedArguments(arguments);
    const QSet<QString> supportedProperties{
        QStringLiteral("initial_capital"),
        QStringLiteral("default_qty_type"),
        QStringLiteral("default_qty_value"),
        QStringLiteral("commission_type"),
        QStringLiteral("commission_value"),
        QStringLiteral("slippage"),
        QStringLiteral("pyramiding"),
        QStringLiteral("calc_on_every_tick"),
        QStringLiteral("calc_on_order_fills"),
        QStringLiteral("process_orders_on_close"),
        QStringLiteral("overlay"),
    };
    for (auto property = named.cbegin();
         property != named.cend();
         ++property) {
        if (!supportedProperties.contains(property.key())) {
            addDiagnostic(
                context,
                PineDiagnosticSeverity::Error,
                statement.line,
                QStringLiteral(
                    "strategy() property “%1” is unsupported and was not approximated.")
                    .arg(property.key()));
        }
    }
    const auto numericProperty =
        [&](const QString& name) -> std::optional<double> {
        const auto found = named.constFind(name);
        return found == named.cend()
                   ? std::nullopt
                   : numericValue(*found, context);
    };
    if (const auto value =
            numericProperty(QStringLiteral("initial_capital"))) {
        context.result.execution.initialCapital = *value;
    } else if (named.contains(QStringLiteral("initial_capital"))) {
        addDiagnostic(
            context,
            PineDiagnosticSeverity::Error,
            statement.line,
            QStringLiteral(
                "initial_capital must be a numeric literal or previously declared input."));
    }
    const auto hasQuantityType =
        named.contains(QStringLiteral("default_qty_type"));
    const auto hasQuantityValue =
        named.contains(QStringLiteral("default_qty_value"));
    if (hasQuantityType != hasQuantityValue) {
        addDiagnostic(
            context,
            PineDiagnosticSeverity::Error,
            statement.line,
            QStringLiteral(
                "default_qty_type and default_qty_value must be supplied together so sizing is not guessed."));
    } else if (hasQuantityType) {
        const auto type =
            named.value(QStringLiteral("default_qty_type")).trimmed();
        const auto quantity =
            numericProperty(QStringLiteral("default_qty_value"));
        if (!quantity) {
            addDiagnostic(
                context,
                PineDiagnosticSeverity::Error,
                statement.line,
                QStringLiteral(
                    "default_qty_value must be a numeric literal or previously declared input."));
        } else if (
            type != QStringLiteral("strategy.percent_of_equity")) {
            addDiagnostic(
                context,
                PineDiagnosticSeverity::Error,
                statement.line,
                QStringLiteral(
                    "Only strategy.percent_of_equity sizing maps to the native simulator."));
        } else {
            context.result.execution.allocationPercent =
                *quantity;
        }
    }
    const auto hasCommissionType =
        named.contains(QStringLiteral("commission_type"));
    const auto hasCommissionValue =
        named.contains(QStringLiteral("commission_value"));
    if (hasCommissionType != hasCommissionValue) {
        addDiagnostic(
            context,
            PineDiagnosticSeverity::Error,
            statement.line,
            QStringLiteral(
                "commission_type and commission_value must be supplied together so costs are not guessed."));
    } else if (hasCommissionType) {
        const auto commission =
            numericProperty(QStringLiteral("commission_value"));
        const auto type =
            named.value(QStringLiteral("commission_type")).trimmed();
        if (!commission) {
            addDiagnostic(
                context,
                PineDiagnosticSeverity::Error,
                statement.line,
                QStringLiteral(
                    "commission_value must be a numeric literal or previously declared input."));
        } else if (
            type !=
            QStringLiteral(
                "strategy.commission.cash_per_order")) {
            addDiagnostic(
                context,
                PineDiagnosticSeverity::Error,
                statement.line,
                QStringLiteral(
                    "Only strategy.commission.cash_per_order maps to fixed per-side commission."));
        } else {
            context.result.execution.commissionPerSide = *commission;
        }
    }
    const auto rejectUnsupportedBoolean =
        [&](const QString& name, const QString& reason) {
            const auto value = named.value(name).trimmed();
            if (value.isEmpty()) {
                return;
            }
            if (value == QStringLiteral("true")) {
                addDiagnostic(
                    context,
                    PineDiagnosticSeverity::Error,
                    statement.line,
                    reason);
            } else if (value != QStringLiteral("false")) {
                addDiagnostic(
                    context,
                    PineDiagnosticSeverity::Error,
                    statement.line,
                    QStringLiteral(
                        "%1 must be the literal false in this importer.")
                        .arg(name));
            }
        };
    rejectUnsupportedBoolean(
        QStringLiteral("calc_on_every_tick"),
        QStringLiteral(
            "calc_on_every_tick is unsupported because historical candles contain no full tick stream."));
    rejectUnsupportedBoolean(
        QStringLiteral("calc_on_order_fills"),
        QStringLiteral(
            "calc_on_order_fills is unsupported by the deterministic next-open engine."));
    rejectUnsupportedBoolean(
        QStringLiteral("process_orders_on_close"),
        QStringLiteral(
            "process_orders_on_close is unsupported; orders execute at the next open."));
    if (named.contains(QStringLiteral("pyramiding"))) {
        const auto pyramiding =
            numericProperty(QStringLiteral("pyramiding"));
        if (!pyramiding) {
            addDiagnostic(
                context,
                PineDiagnosticSeverity::Error,
                statement.line,
                QStringLiteral(
                    "pyramiding must be a numeric literal or previously declared input."));
        } else if (*pyramiding < 0.0 ||
                   *pyramiding > 1.0) {
            addDiagnostic(
                context,
                PineDiagnosticSeverity::Error,
                statement.line,
                QStringLiteral(
                    "Only zero or one long position is supported."));
        }
    }
    if (named.contains(QStringLiteral("slippage"))) {
        const auto slippage =
            numericProperty(QStringLiteral("slippage"));
        if (!slippage) {
            addDiagnostic(
                context,
                PineDiagnosticSeverity::Error,
                statement.line,
                QStringLiteral(
                    "slippage must be a numeric literal or previously declared input."));
        } else if (*slippage != 0.0) {
            addDiagnostic(
                context,
                PineDiagnosticSeverity::Error,
                statement.line,
                QStringLiteral(
                    "Pine slippage is expressed in ticks and cannot be safely converted to basis points."));
        }
    }
}

[[nodiscard]] bool parseInputAssignment(
    const QString& identifier,
    const QString& expression,
    const int line,
    ParseContext& context) {
    QStringList arguments;
    const auto isInteger =
        functionCall(
            expression,
            QStringLiteral("input.int"),
            arguments);
    const auto isFloat =
        !isInteger &&
        functionCall(
            expression,
            QStringLiteral("input.float"),
            arguments);
    if (!isInteger && !isFloat) {
        return false;
    }
    if (arguments.isEmpty()) {
        addDiagnostic(
            context,
            PineDiagnosticSeverity::Error,
            line,
            QStringLiteral("Numeric input requires a default value."));
        return true;
    }
    const auto named = namedArguments(arguments);
    const auto defaultExpression =
        named.contains(QStringLiteral("defval"))
            ? named.value(QStringLiteral("defval"))
            : arguments.front();
    bool ok = false;
    const auto value = defaultExpression.toDouble(&ok);
    if (!ok || !std::isfinite(value) ||
        (isInteger && std::floor(value) != value)) {
        addDiagnostic(
            context,
            PineDiagnosticSeverity::Error,
            line,
            QStringLiteral(
                "Numeric input default must be a finite literal."));
        return true;
    }
    context.numbers.insert(identifier, value);
    return true;
}

void parseAssignment(
    const LogicalStatement& statement,
    ParseContext& context) {
    static const QRegularExpression assignment(
        QStringLiteral(
            R"(^(?:(?:int|float|bool)\s+)?([A-Za-z_][A-Za-z0-9_]*)\s*=\s*(.+)$)"));
    const auto match = assignment.match(statement.text);
    if (!match.hasMatch()) {
        addDiagnostic(
            context,
            PineDiagnosticSeverity::Error,
            statement.line,
            QStringLiteral("Unsupported statement “%1”.")
                .arg(statement.text.left(120)));
        return;
    }
    const auto identifier = match.captured(1);
    const auto expression = match.captured(2).trimmed();
    if (context.numbers.size() +
            context.operands.size() +
            context.groups.size() >=
        kMaximumSymbols) {
        addDiagnostic(
            context,
            PineDiagnosticSeverity::Error,
            statement.line,
            QStringLiteral("Script declares too many symbols."));
        return;
    }
    if (context.numbers.contains(identifier) ||
        context.operands.contains(identifier) ||
        context.groups.contains(identifier)) {
        addDiagnostic(
            context,
            PineDiagnosticSeverity::Error,
            statement.line,
            QStringLiteral(
                "Symbol “%1” is declared more than once; reassignment is unsupported.")
                .arg(identifier));
        return;
    }
    if (parseInputAssignment(
            identifier,
            expression,
            statement.line,
            context)) {
        return;
    }
    if (const auto operand =
            parseOperand(
                expression,
                context,
                statement.line,
                false)) {
        context.operands.insert(identifier, *operand);
        return;
    }
    if (const auto numberValue = numericValue(expression, context)) {
        context.numbers.insert(identifier, *numberValue);
        return;
    }
    if (const auto group =
            parseGroup(expression, context, statement.line)) {
        context.groups.insert(identifier, *group);
    }
}

void applyOrderCall(
    const LogicalStatement& statement,
    const QString& enclosingCondition,
    ParseContext& context) {
    QStringList arguments;
    const auto isEntry = functionCall(
        statement.text,
        QStringLiteral("strategy.entry"),
        arguments);
    const auto isClose =
        !isEntry &&
        functionCall(
            statement.text,
            QStringLiteral("strategy.close"),
            arguments);
    if (!isEntry && !isClose) {
        addDiagnostic(
            context,
            PineDiagnosticSeverity::Error,
            statement.line,
            QStringLiteral(
                "An if block in this subset must contain strategy.entry() or strategy.close()."));
        return;
    }
    const auto named = namedArguments(arguments);
    const QSet<QString> supportedOrderArguments =
        isEntry
            ? QSet<QString>{
                  QStringLiteral("id"),
                  QStringLiteral("direction"),
                  QStringLiteral("when"),
              }
            : QSet<QString>{
                  QStringLiteral("id"),
                  QStringLiteral("when"),
              };
    for (auto argument = named.cbegin();
         argument != named.cend();
         ++argument) {
        if (!supportedOrderArguments.contains(argument.key())) {
            addDiagnostic(
                context,
                PineDiagnosticSeverity::Error,
                statement.line,
                QStringLiteral(
                    "Order argument “%1” is unsupported; stop, limit, quantity overrides, and immediate fills are not approximated.")
                    .arg(argument.key()));
            return;
        }
    }
    auto conditionText = enclosingCondition.trimmed();
    if (conditionText.isEmpty()) {
        conditionText = named.value(QStringLiteral("when")).trimmed();
    } else if (named.contains(QStringLiteral("when"))) {
        addDiagnostic(
            context,
            PineDiagnosticSeverity::Error,
            statement.line,
            QStringLiteral(
                "Use either an if condition or when=, not both."));
        return;
    }
    if (conditionText.isEmpty()) {
        addDiagnostic(
            context,
            PineDiagnosticSeverity::Error,
            statement.line,
            QStringLiteral(
                "Order calls require an if condition or when= condition."));
        return;
    }
    if (isEntry) {
        const auto direction =
            named.contains(QStringLiteral("direction"))
                ? named.value(QStringLiteral("direction")).trimmed()
                : arguments.size() >= 2
                ? arguments[1].trimmed()
                : QString{};
        if (direction != QStringLiteral("strategy.long")) {
            addDiagnostic(
                context,
                PineDiagnosticSeverity::Error,
                statement.line,
                QStringLiteral(
                    "Only long strategy.entry() orders are supported."));
            return;
        }
        if (context.entrySeen) {
            addDiagnostic(
                context,
                PineDiagnosticSeverity::Error,
                statement.line,
                QStringLiteral(
                    "The initial importer supports one entry order rule."));
            return;
        }
    } else if (context.exitSeen) {
        addDiagnostic(
            context,
            PineDiagnosticSeverity::Error,
            statement.line,
            QStringLiteral(
                "The initial importer supports one close order rule."));
        return;
    }
    const auto group =
        parseGroup(conditionText, context, statement.line);
    if (!group) {
        return;
    }
    if (isEntry) {
        context.result.strategy.entry = {
            .match = group->match,
            .conditions = group->conditions,
        };
        context.entrySeen = true;
    } else {
        context.result.strategy.exit = {
            .match = group->match,
            .conditions = group->conditions,
        };
        context.exitSeen = true;
    }
}

[[nodiscard]] bool forbiddenStatement(
    const LogicalStatement& statement,
    ParseContext& context) {
    struct Forbidden {
        QString token;
        QString message;
    };
    const std::array forbidden{
        Forbidden{
            QStringLiteral("request."),
            QStringLiteral(
                "request.* data calls are unsupported; use cached application data.")},
        Forbidden{
            QStringLiteral("strategy.exit"),
            QStringLiteral(
                "Stop, limit, trailing, and partial strategy.exit() orders are unsupported.")},
        Forbidden{
            QStringLiteral("strategy.order"),
            QStringLiteral(
                "strategy.order() is unsupported; use long strategy.entry() and strategy.close().")},
        Forbidden{
            QStringLiteral("strategy.short"),
            QStringLiteral("Short positions are unsupported.")},
        Forbidden{
            QStringLiteral("=>"),
            QStringLiteral("User-defined functions are unsupported.")},
        Forbidden{
            QStringLiteral(":="),
            QStringLiteral(
                "Persistent reassignment is unsupported by the declarative importer.")},
        Forbidden{
            QStringLiteral("["),
            QStringLiteral(
                "History offsets, arrays, tuples, and lookahead syntax are unsupported.")},
    };
    for (const auto& item : forbidden) {
        if (statement.text.contains(item.token)) {
            addDiagnostic(
                context,
                PineDiagnosticSeverity::Error,
                statement.line,
                item.message,
                statement.text.indexOf(item.token) + 1);
            return true;
        }
    }
    const auto trimmed = statement.text.trimmed();
    if (trimmed.startsWith(QStringLiteral("import ")) ||
        trimmed.startsWith(QStringLiteral("for ")) ||
        trimmed.startsWith(QStringLiteral("while ")) ||
        trimmed.startsWith(QStringLiteral("switch ")) ||
        trimmed.startsWith(QStringLiteral("type ")) ||
        trimmed.startsWith(QStringLiteral("library(")) ||
        trimmed.startsWith(QStringLiteral("indicator("))) {
        addDiagnostic(
            context,
            PineDiagnosticSeverity::Error,
            statement.line,
            QStringLiteral(
                "This construct is outside the safe strategy-import subset."));
        return true;
    }
    return false;
}

} // namespace

bool PineStrategyImportResult::ok() const noexcept {
    return std::ranges::none_of(
        diagnostics,
        [](const PineDiagnostic& diagnostic) {
            return diagnostic.severity ==
                   PineDiagnosticSeverity::Error;
        });
}

QString pineDiagnosticSeverityLabel(
    const PineDiagnosticSeverity severity) {
    switch (severity) {
    case PineDiagnosticSeverity::Information:
        return QStringLiteral("Info");
    case PineDiagnosticSeverity::Warning:
        return QStringLiteral("Warning");
    case PineDiagnosticSeverity::Error:
        return QStringLiteral("Error");
    }
    return QStringLiteral("Error");
}

PineStrategyImportResult importPineStrategy(
    const QString& source) {
    ParseContext context;
    context.result.execution = {};
    const auto sourceBytes = source.toUtf8();
    if (sourceBytes.isEmpty() ||
        sourceBytes.size() > kMaximumSourceBytes) {
        addDiagnostic(
            context,
            PineDiagnosticSeverity::Error,
            1,
            QStringLiteral(
                "Script must contain between 1 byte and 64 KiB."));
        return context.result;
    }
    const auto lines = source.split(u'\n');
    if (lines.size() > kMaximumLines) {
        addDiagnostic(
            context,
            PineDiagnosticSeverity::Error,
            1,
            QStringLiteral("Script exceeds the 1,000-line limit."));
        return context.result;
    }
    static const QRegularExpression versionPattern(
        QStringLiteral(R"(^\s*//@version\s*=\s*([56])\s*$)"));
    auto versionSeen = false;
    for (auto index = 0; index < lines.size(); ++index) {
        const auto match = versionPattern.match(lines[index]);
        if (match.hasMatch()) {
            versionSeen = true;
            break;
        }
        if (!lines[index].trimmed().isEmpty()) {
            break;
        }
    }
    if (!versionSeen) {
        addDiagnostic(
            context,
            PineDiagnosticSeverity::Error,
            1,
            QStringLiteral(
                "Start the script with //@version=5 or //@version=6."));
    }

    const auto statements = logicalStatements(lines, context);
    for (auto index = std::size_t{};
         index < statements.size();
         ++index) {
        const auto& statement = statements[index];
        if (forbiddenStatement(statement, context)) {
            continue;
        }
        if (statement.text.startsWith(QStringLiteral("strategy("))) {
            parseStrategyDeclaration(statement, context);
            continue;
        }
        if (statement.text.startsWith(QStringLiteral("plot(")) ||
            statement.text.startsWith(QStringLiteral("plotshape("))) {
            addDiagnostic(
                context,
                PineDiagnosticSeverity::Information,
                statement.line,
                QStringLiteral(
                    "Visual plot statement was ignored; enable the corresponding native chart indicator after import."));
            continue;
        }
        if (statement.text.startsWith(QStringLiteral("if "))) {
            const auto condition =
                statement.text.mid(3).trimmed();
            if (condition.isEmpty() ||
                index + 1 >= statements.size() ||
                statements[index + 1].indentation <=
                    statement.indentation) {
                addDiagnostic(
                    context,
                    PineDiagnosticSeverity::Error,
                    statement.line,
                    QStringLiteral(
                        "if requires one indented order statement in this subset."));
                continue;
            }
            applyOrderCall(
                statements[index + 1],
                condition,
                context);
            ++index;
            continue;
        }
        if (statement.text.startsWith(
                QStringLiteral("strategy.entry(")) ||
            statement.text.startsWith(
                QStringLiteral("strategy.close("))) {
            applyOrderCall(statement, {}, context);
            continue;
        }
        parseAssignment(statement, context);
    }

    if (!context.declarationSeen) {
        addDiagnostic(
            context,
            PineDiagnosticSeverity::Error,
            1,
            QStringLiteral("A strategy() declaration is required."));
    }
    if (!context.entrySeen) {
        addDiagnostic(
            context,
            PineDiagnosticSeverity::Error,
            1,
            QStringLiteral(
                "A supported long strategy.entry() rule is required."));
    }
    if (!context.exitSeen) {
        addDiagnostic(
            context,
            PineDiagnosticSeverity::Error,
            1,
            QStringLiteral(
                "A supported strategy.close() rule is required."));
    }
    const auto identity = QCryptographicHash::hash(
        sourceBytes,
        QCryptographicHash::Sha256)
                              .toHex()
                              .left(16);
    context.result.strategy.id =
        QStringLiteral("script-import-%1")
            .arg(QString::fromLatin1(identity));
    context.result.strategy.name = context.title;

    if (context.result.ok()) {
        if (const auto error =
                validateBacktestParameters(
                    context.result.execution);
            !error.isEmpty()) {
            addDiagnostic(
                context,
                PineDiagnosticSeverity::Error,
                1,
                error);
        }
        if (const auto error =
                validateStrategy(context.result.strategy);
            !error.isEmpty()) {
            addDiagnostic(
                context,
                PineDiagnosticSeverity::Error,
                1,
                QStringLiteral(
                    "Compiled native strategy is invalid: %1")
                    .arg(error));
        }
    }
    if (context.result.ok()) {
        addDiagnostic(
            context,
            PineDiagnosticSeverity::Information,
            1,
            QStringLiteral(
                "Compiled safely into native completed-bar, next-open rules."));
    }
    return context.result;
}

QString pineNativeStrategyPreview(
    const PineStrategyImportResult& result) {
    if (!result.ok()) {
        return QStringLiteral(
            "Compilation failed; fix Error diagnostics before applying.");
    }
    const auto groupText =
        [](const QString& label, const ConditionGroup& group) {
        QStringList conditions;
        for (const auto& condition : group.conditions) {
            const auto right =
                condition.right
                    ? strategyFieldLabel(condition.right->field) +
                          (condition.right->period > 0
                               ? QStringLiteral(" %1")
                                     .arg(condition.right->period)
                               : QString{})
                    : QString::number(condition.constant, 'g', 12);
            conditions.push_back(
                QStringLiteral("%1 %2 %3")
                    .arg(
                        strategyFieldLabel(condition.left.field),
                        strategyComparisonLabel(condition.comparison),
                        right));
        }
        return QStringLiteral("%1 (%2): %3")
            .arg(
                label,
                group.match == ConditionMatch::All
                    ? QStringLiteral("all")
                    : QStringLiteral("any"),
                conditions.join(
                    group.match == ConditionMatch::All
                        ? QStringLiteral(" AND ")
                        : QStringLiteral(" OR ")));
    };
    return QStringLiteral(
               "Native strategy: %1\n%2\n%3\n"
               "Initial capital: %4 · Allocation: %5% · "
               "Commission/side: %6 · Slippage: set in Strategy Lab")
        .arg(
            result.strategy.name,
            groupText(
                QStringLiteral("Entry"),
                result.strategy.entry),
            groupText(
                QStringLiteral("Exit"),
                result.strategy.exit),
            QString::number(
                result.execution.initialCapital,
                'f',
                2),
            QString::number(
                result.execution.allocationPercent,
                'f',
                2),
            QString::number(
                result.execution.commissionPerSide,
                'f',
                2));
}

QString pineStrategyExample() {
    return QStringLiteral(
        "//@version=6\n"
        "strategy(\"EMA Cross Simulation\", "
        "initial_capital=100000, "
        "default_qty_type=strategy.percent_of_equity, "
        "default_qty_value=25)\n"
        "\n"
        "fastLength = input.int(20, \"Fast length\")\n"
        "slowLength = input.int(50, \"Slow length\")\n"
        "fast = ta.ema(close, fastLength)\n"
        "slow = ta.ema(close, slowLength)\n"
        "enterLong = ta.crossover(fast, slow)\n"
        "exitLong = ta.crossunder(fast, slow)\n"
        "\n"
        "if enterLong\n"
        "    strategy.entry(\"Long\", strategy.long)\n"
        "\n"
        "if exitLong\n"
        "    strategy.close(\"Long\")\n"
        "\n"
        "plot(fast, \"Fast EMA\")\n"
        "plot(slow, \"Slow EMA\")\n");
}

} // namespace tvchart
