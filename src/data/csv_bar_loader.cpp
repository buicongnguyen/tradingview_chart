#include "data/csv_bar_loader.hpp"

#include <QDateTime>
#include <QFile>
#include <QHash>
#include <QStringList>

#include <cmath>
#include <optional>

namespace tvchart {
namespace {

struct CsvLineResult {
    QStringList fields;
    QString error;
};

[[nodiscard]] CsvLineResult splitCsvLine(const QStringView line) {
    QStringList fields;
    QString current;
    bool quoted = false;

    for (qsizetype index = 0; index < line.size(); ++index) {
        const auto character = line[index];
        if (character == u'"') {
            if (quoted && index + 1 < line.size() && line[index + 1] == u'"') {
                current.append(u'"');
                ++index;
            } else {
                quoted = !quoted;
            }
        } else if (character == u',' && !quoted) {
            fields.push_back(current.trimmed());
            current.clear();
        } else {
            current.append(character);
        }
    }

    if (quoted) {
        return {.error = QStringLiteral("unterminated quoted field")};
    }
    fields.push_back(current.trimmed());
    return {.fields = std::move(fields)};
}

[[nodiscard]] std::optional<std::int64_t> parseTimestamp(const QString& text) {
    bool integerOk = false;
    auto integerValue = text.trimmed().toLongLong(&integerOk);
    if (integerOk) {
        if (integerValue > 100'000'000'000LL) {
            integerValue /= 1'000;
        }
        if (integerValue > 0) {
            return integerValue;
        }
        return std::nullopt;
    }

    auto dateTime = QDateTime::fromString(text.trimmed(), Qt::ISODateWithMs);
    if (!dateTime.isValid()) {
        dateTime = QDateTime::fromString(text.trimmed(), Qt::ISODate);
    }
    if (!dateTime.isValid()) {
        return std::nullopt;
    }
    return dateTime.toUTC().toSecsSinceEpoch();
}

[[nodiscard]] std::optional<double> parseNumber(const QString& text) {
    bool ok = false;
    const auto value = text.trimmed().toDouble(&ok);
    if (!ok || !std::isfinite(value)) {
        return std::nullopt;
    }
    return value;
}

[[nodiscard]] QString lineError(const qsizetype lineNumber, const QString& detail) {
    return QStringLiteral("Line %1: %2").arg(lineNumber).arg(detail);
}

} // namespace

CsvLoadResult CsvBarLoader::loadFile(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return {.error = QStringLiteral("Could not open %1: %2").arg(path, file.errorString())};
    }
    const auto content = QString::fromUtf8(file.readAll());
    if (file.error() != QFileDevice::NoError) {
        return {.error = QStringLiteral("Could not read %1: %2").arg(path, file.errorString())};
    }
    return parse(content);
}

CsvLoadResult CsvBarLoader::parse(const QStringView content) {
    const auto normalized = content.toString().replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    const auto lines = normalized.split(u'\n');
    if (lines.isEmpty() || lines.front().trimmed().isEmpty()) {
        return {.error = QStringLiteral("The CSV file is empty.")};
    }

    const auto headerResult = splitCsvLine(lines.front());
    if (!headerResult.error.isEmpty()) {
        return {.error = lineError(1, headerResult.error)};
    }

    QHash<QString, qsizetype> columns;
    for (qsizetype index = 0; index < headerResult.fields.size(); ++index) {
        auto name = headerResult.fields[index].trimmed().toLower();
        if (index == 0 && !name.isEmpty() && name.front() == QChar{0xFEFF}) {
            name.removeFirst();
        }
        if (!name.isEmpty()) {
            columns.insert(name, index);
        }
    }

    const QStringList required{
        QStringLiteral("timestamp"),
        QStringLiteral("open"),
        QStringLiteral("high"),
        QStringLiteral("low"),
        QStringLiteral("close"),
        QStringLiteral("volume"),
    };
    for (const auto& name : required) {
        if (!columns.contains(name)) {
            return {.error = QStringLiteral("Missing required column: %1").arg(name)};
        }
    }

    Bars bars;
    bars.reserve(static_cast<std::size_t>(std::max<qsizetype>(0, lines.size() - 1)));
    for (qsizetype index = 1; index < lines.size(); ++index) {
        const auto trimmed = lines[index].trimmed();
        if (trimmed.isEmpty()) {
            continue;
        }

        const auto row = splitCsvLine(lines[index]);
        if (!row.error.isEmpty()) {
            return {.error = lineError(index + 1, row.error)};
        }
        if (row.fields.size() < headerResult.fields.size()) {
            return {.error = lineError(index + 1, QStringLiteral("not enough columns"))};
        }

        const auto timestamp = parseTimestamp(row.fields[columns[QStringLiteral("timestamp")]]);
        const auto open = parseNumber(row.fields[columns[QStringLiteral("open")]]);
        const auto high = parseNumber(row.fields[columns[QStringLiteral("high")]]);
        const auto low = parseNumber(row.fields[columns[QStringLiteral("low")]]);
        const auto close = parseNumber(row.fields[columns[QStringLiteral("close")]]);
        const auto volume = parseNumber(row.fields[columns[QStringLiteral("volume")]]);

        if (!timestamp) {
            return {.error = lineError(index + 1, QStringLiteral("invalid timestamp"))};
        }
        if (!open || !high || !low || !close || !volume) {
            return {.error = lineError(index + 1, QStringLiteral("invalid numeric value"))};
        }

        Bar bar{
            .timestamp = *timestamp,
            .open = *open,
            .high = *high,
            .low = *low,
            .close = *close,
            .volume = *volume,
        };
        if (const auto error = validateBar(bar)) {
            return {.error = lineError(index + 1, QString::fromStdString(*error))};
        }
        if (!bars.empty() && bar.timestamp <= bars.back().timestamp) {
            return {.error = lineError(
                        index + 1,
                        QStringLiteral("timestamps must be strictly increasing"))};
        }
        bars.push_back(bar);
    }

    if (bars.empty()) {
        return {.error = QStringLiteral("The CSV file contains no data rows.")};
    }
    return {.bars = std::move(bars)};
}

} // namespace tvchart
