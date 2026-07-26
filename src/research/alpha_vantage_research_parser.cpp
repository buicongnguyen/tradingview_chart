#include "research/alpha_vantage_research_parser.hpp"

#include "watchlists/watchlist_workspace.hpp"

#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>

#include <array>
#include <cmath>
#include <optional>
#include <ranges>

namespace tvchart {
namespace {

[[nodiscard]] std::optional<double> finiteNumber(
    const QJsonObject& object,
    const QString& key) {
    const auto value = object.value(key);
    bool ok = false;
    const auto number = value.isDouble()
                            ? value.toDouble()
                            : value.toString().trimmed().toDouble(&ok);
    if (value.isDouble()) {
        ok = true;
    }
    return ok && std::isfinite(number)
               ? std::optional<double>{number}
               : std::nullopt;
}

[[nodiscard]] int nonNegativeInteger(
    const QJsonObject& object,
    const QString& key) {
    bool ok = false;
    const auto value = object.value(key).toString().toInt(&ok);
    return ok && value >= 0 ? value : 0;
}

[[nodiscard]] QString providerError(const QJsonObject& object) {
    constexpr std::array keys{
        "Error Message",
        "Information",
        "Note",
    };
    for (const auto* key : keys) {
        const auto message =
            object.value(QString::fromLatin1(key)).toString().trimmed();
        if (!message.isEmpty()) {
            return message;
        }
    }
    return {};
}

[[nodiscard]] std::vector<QString> parseCsvRow(
    const QString& row,
    bool& valid) {
    std::vector<QString> fields;
    QString field;
    bool quoted = false;
    bool closedQuote = false;
    valid = true;
    for (qsizetype index = 0; index < row.size(); ++index) {
        const auto character = row.at(index);
        if (quoted) {
            if (character == u'"') {
                if (index + 1 < row.size() && row.at(index + 1) == u'"') {
                    field += u'"';
                    ++index;
                } else {
                    quoted = false;
                    closedQuote = true;
                }
            } else {
                field += character;
            }
            continue;
        }
        if (closedQuote && character != u',') {
            valid = false;
            return {};
        }
        if (character == u',') {
            fields.push_back(field);
            field.clear();
            closedQuote = false;
        } else if (character == u'"' && field.isEmpty()) {
            quoted = true;
        } else {
            field += character;
        }
    }
    if (quoted) {
        valid = false;
        return {};
    }
    fields.push_back(field);
    return fields;
}

[[nodiscard]] ResearchEvent corporateEvent(
    const QString& symbol,
    const ResearchEventType type,
    const QDate& date,
    const std::int64_t retrievedAtUtc,
    const QString& title) {
    return {
        .id = QStringLiteral("alpha-vantage-%1-%2-%3")
                  .arg(
                      researchEventTypeId(type),
                      symbol,
                      date.toString(Qt::ISODate)),
        .symbol = symbol,
        .type = type,
        .scheduledDate = date,
        .timeOfDay = QStringLiteral("date_only"),
        .title = title,
        .source = QStringLiteral("Alpha Vantage"),
        .asOfUtc = retrievedAtUtc,
        .confidence = ResearchConfidence::Confirmed,
    };
}

} // namespace

AlphaVantageOverviewParseResult
AlphaVantageResearchParser::parseOverview(
    const QByteArray& payload,
    const std::int64_t retrievedAtUtc) {
    QJsonParseError parseError;
    const auto document = QJsonDocument::fromJson(payload, &parseError);
    if (parseError.error != QJsonParseError::NoError ||
        !document.isObject()) {
        return {
            .error =
                QStringLiteral("Alpha Vantage overview returned invalid JSON."),
        };
    }
    const auto object = document.object();
    if (const auto error = providerError(object); !error.isEmpty()) {
        return {.error = QStringLiteral("Alpha Vantage: %1").arg(error)};
    }
    const auto symbol = normalizeWatchlistSymbol(
        object.value(QStringLiteral("Symbol")).toString());
    if (symbol.isEmpty()) {
        return {
            .error =
                QStringLiteral("Alpha Vantage overview returned no company data."),
        };
    }

    CompanyResearchSnapshot snapshot{
        .symbol = symbol,
        .provider = QStringLiteral("Alpha Vantage"),
        .asOfUtc = retrievedAtUtc,
        .name = object.value(QStringLiteral("Name")).toString(),
        .cik = object.value(QStringLiteral("CIK")).toString(),
        .exchange = object.value(QStringLiteral("Exchange")).toString(),
        .currency =
            object.value(QStringLiteral("Currency")).toString().toUpper(),
        .sector = object.value(QStringLiteral("Sector")).toString(),
        .industry = object.value(QStringLiteral("Industry")).toString(),
        .marketCapitalization =
            finiteNumber(object, QStringLiteral("MarketCapitalization")),
        .eps = finiteNumber(object, QStringLiteral("EPS")),
        .peRatio = finiteNumber(object, QStringLiteral("PERatio")),
        .forwardPe = finiteNumber(object, QStringLiteral("ForwardPE")),
        .beta = finiteNumber(object, QStringLiteral("Beta")),
        .week52High = finiteNumber(object, QStringLiteral("52WeekHigh")),
        .week52Low = finiteNumber(object, QStringLiteral("52WeekLow")),
        .analystTargetPrice =
            finiteNumber(object, QStringLiteral("AnalystTargetPrice")),
        .ratings = {
            .strongBuy = nonNegativeInteger(
                object,
                QStringLiteral("AnalystRatingStrongBuy")),
            .buy = nonNegativeInteger(
                object,
                QStringLiteral("AnalystRatingBuy")),
            .hold = nonNegativeInteger(
                object,
                QStringLiteral("AnalystRatingHold")),
            .sell = nonNegativeInteger(
                object,
                QStringLiteral("AnalystRatingSell")),
            .strongSell = nonNegativeInteger(
                object,
                QStringLiteral("AnalystRatingStrongSell")),
        },
    };
    if (const auto error = validateCompanySnapshot(snapshot);
        !error.isEmpty()) {
        return {.error = error};
    }

    std::vector<ResearchEvent> events;
    const auto exDividend = QDate::fromString(
        object.value(QStringLiteral("ExDividendDate")).toString(),
        Qt::ISODate);
    if (exDividend.isValid()) {
        events.push_back(corporateEvent(
            symbol,
            ResearchEventType::ExDividend,
            exDividend,
            retrievedAtUtc,
            QStringLiteral("%1 ex-dividend").arg(symbol)));
    }
    const auto dividend = QDate::fromString(
        object.value(QStringLiteral("DividendDate")).toString(),
        Qt::ISODate);
    if (dividend.isValid()) {
        events.push_back(corporateEvent(
            symbol,
            ResearchEventType::DividendPayment,
            dividend,
            retrievedAtUtc,
            QStringLiteral("%1 dividend payment").arg(symbol)));
    }
    return {
        .snapshot = std::move(snapshot),
        .corporateEvents = std::move(events),
    };
}

AlphaVantageCalendarParseResult
AlphaVantageResearchParser::parseEarningsCalendar(
    const QByteArray& payload,
    const std::int64_t retrievedAtUtc) {
    const auto text = QString::fromUtf8(payload);
    if (text.trimmed().startsWith(u'{')) {
        QJsonParseError parseError;
        const auto document = QJsonDocument::fromJson(payload, &parseError);
        if (parseError.error == QJsonParseError::NoError &&
            document.isObject()) {
            const auto error = providerError(document.object());
            return {
                .error = error.isEmpty()
                             ? QStringLiteral(
                                   "Alpha Vantage earnings calendar returned "
                                   "an unexpected JSON response.")
                             : QStringLiteral("Alpha Vantage: %1").arg(error),
            };
        }
    }
    const auto rows = text.split(u'\n');
    if (rows.isEmpty()) {
        return {
            .error =
                QStringLiteral("Alpha Vantage earnings calendar is empty."),
        };
    }
    bool validHeader = true;
    auto header = parseCsvRow(rows.front().trimmed(), validHeader);
    if (!validHeader) {
        return {
            .error =
                QStringLiteral("Alpha Vantage earnings header is invalid."),
        };
    }
    for (auto& field : header) {
        field = field.trimmed();
    }
    const auto column = [&header](const QString& name)
        -> std::optional<std::size_t> {
        const auto found = std::ranges::find(header, name);
        if (found == header.end()) {
            return std::nullopt;
        }
        return static_cast<std::size_t>(
            std::distance(header.begin(), found));
    };
    const auto symbolColumn = column(QStringLiteral("symbol"));
    const auto nameColumn = column(QStringLiteral("name"));
    const auto dateColumn = column(QStringLiteral("reportDate"));
    const auto fiscalColumn = column(QStringLiteral("fiscalDateEnding"));
    const auto estimateColumn = column(QStringLiteral("estimate"));
    const auto currencyColumn = column(QStringLiteral("currency"));
    const auto timeColumn = column(QStringLiteral("timeOfTheDay"));
    if (!symbolColumn || !dateColumn) {
        return {
            .error =
                QStringLiteral("Alpha Vantage earnings columns are missing."),
        };
    }

    AlphaVantageCalendarParseResult result;
    for (qsizetype rowIndex = 1; rowIndex < rows.size(); ++rowIndex) {
        auto row = rows.at(rowIndex);
        if (row.endsWith(u'\r')) {
            row.chop(1);
        }
        if (row.trimmed().isEmpty()) {
            continue;
        }
        bool validRow = true;
        const auto fields = parseCsvRow(row, validRow);
        const auto field = [&fields](const std::optional<std::size_t> index) {
            return index && *index < fields.size()
                       ? fields[*index].trimmed()
                       : QString{};
        };
        const auto symbol =
            normalizeWatchlistSymbol(field(symbolColumn));
        const auto reportDate =
            QDate::fromString(field(dateColumn), Qt::ISODate);
        if (!validRow || symbol.isEmpty() || !reportDate.isValid()) {
            continue;
        }
        bool estimateOk = false;
        const auto estimate = field(estimateColumn).toDouble(&estimateOk);
        ResearchEvent event{
            .id = QStringLiteral("alpha-vantage-earnings-%1-%2")
                      .arg(symbol, reportDate.toString(Qt::ISODate)),
            .symbol = symbol,
            .type = ResearchEventType::Earnings,
            .scheduledDate = reportDate,
            .timeOfDay =
                field(timeColumn).isEmpty()
                    ? QStringLiteral("unknown")
                    : field(timeColumn),
            .title =
                field(nameColumn).isEmpty()
                    ? QStringLiteral("%1 earnings").arg(symbol)
                    : QStringLiteral("%1 earnings").arg(field(nameColumn)),
            .source = QStringLiteral("Alpha Vantage"),
            .asOfUtc = retrievedAtUtc,
            .confidence = ResearchConfidence::Estimated,
            .estimate =
                estimateOk && std::isfinite(estimate)
                    ? std::optional<double>{estimate}
                    : std::nullopt,
            .currency = field(currencyColumn).toUpper(),
            .detail =
                field(fiscalColumn).isEmpty()
                    ? QString{}
                    : QStringLiteral("Fiscal period ending %1")
                          .arg(field(fiscalColumn)),
        };
        if (validateResearchEvent(event).isEmpty()) {
            result.events.push_back(std::move(event));
        }
    }
    return result;
}

} // namespace tvchart
