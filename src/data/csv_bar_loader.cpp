#include "data/csv_bar_loader.hpp"

#include <QDateTime>
#include <QFile>
#include <QHash>
#include <QStringList>
#include <QTimeZone>

#include <cmath>
#include <optional>

namespace tvchart {
namespace {

constexpr auto kMaximumUnixTimestamp = std::int64_t{253'402'300'799};
constexpr auto kMaximumCsvFileBytes = qint64{64 * 1024 * 1024};

struct CsvLineResult {
    QStringList fields;
    QString error;
};

[[nodiscard]] CsvLineResult splitCsvLine(const QStringView line) {
    QStringList fields;
    QString current;
    bool quoted = false;
    bool closedQuote = false;

    for (qsizetype index = 0; index < line.size(); ++index) {
        const auto character = line[index];
        if (quoted) {
            if (character == u'"') {
                if (index + 1 < line.size() && line[index + 1] == u'"') {
                    current.append(u'"');
                    ++index;
                } else {
                    quoted = false;
                    closedQuote = true;
                }
            } else {
                current.append(character);
            }
            continue;
        }
        if (closedQuote && character != u',') {
            return {.error = QStringLiteral("unexpected text after quoted field")};
        }
        if (character == u',') {
            fields.push_back(current.trimmed());
            current.clear();
            closedQuote = false;
        } else if (character == u'"') {
            if (!current.isEmpty()) {
                return {.error = QStringLiteral("unexpected quote in unquoted field")};
            }
            quoted = true;
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
        if (integerValue > 0 && integerValue <= kMaximumUnixTimestamp) {
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
    if (dateTime.timeSpec() == Qt::LocalTime) {
        dateTime = QDateTime(dateTime.date(), dateTime.time(), QTimeZone::utc());
    }
    const auto timestamp = dateTime.toSecsSinceEpoch();
    if (timestamp <= 0 || timestamp > kMaximumUnixTimestamp) {
        return std::nullopt;
    }
    return timestamp;
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
    if (file.size() < 0 || file.size() > kMaximumCsvFileBytes) {
        return {
            .error =
                QStringLiteral("The CSV file exceeds the 64 MiB import limit."),
        };
    }
    const auto bytes = file.read(kMaximumCsvFileBytes + 1);
    if (file.error() != QFileDevice::NoError) {
        return {.error = QStringLiteral("Could not read %1: %2").arg(path, file.errorString())};
    }
    if (bytes.size() > kMaximumCsvFileBytes || !file.atEnd()) {
        return {
            .error =
                QStringLiteral("The CSV file exceeds the 64 MiB import limit."),
        };
    }
    const auto content = QString::fromUtf8(bytes);
    return parse(content);
}

CsvLoadResult CsvBarLoader::parse(const QStringView content) {
    auto normalized =
        content.toString().replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    normalized.replace(u'\r', u'\n');
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
            if (columns.contains(name)) {
                return {
                    .error = QStringLiteral("Duplicate column in CSV header: %1").arg(name),
                };
            }
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
