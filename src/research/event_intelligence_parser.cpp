#include "research/event_intelligence_parser.hpp"

#include "watchlists/watchlist_workspace.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QRegularExpression>
#include <QSet>
#include <QDateTime>
#include <QTimeZone>
#include <QUrl>
#include <QUrlQuery>

#include <algorithm>
#include <array>
#include <cmath>
#include <optional>

namespace tvchart {
namespace {

constexpr auto kMaximumProviderEvents = qsizetype{1'000};

[[nodiscard]] QString normalizeCik(QString cik) {
    cik = cik.trimmed();
    static const QRegularExpression digits(QStringLiteral("^\\d{1,10}$"));
    if (!digits.match(cik).hasMatch()) {
        return {};
    }
    while (cik.size() > 1 && cik.startsWith(u'0')) {
        cik.remove(0, 1);
    }
    return cik.rightJustified(10, u'0');
}

[[nodiscard]] QString jsonCik(const QJsonValue& value) {
    if (value.isString()) {
        return normalizeCik(value.toString());
    }
    if (!value.isDouble()) {
        return {};
    }
    const auto number = value.toDouble();
    if (!std::isfinite(number) || number < 0.0 ||
        std::trunc(number) != number || number > 9'999'999'999.0) {
        return {};
    }
    return normalizeCik(
        QString::number(static_cast<qulonglong>(number)));
}

[[nodiscard]] bool trackedSecForm(const QString& form) {
    static const QSet<QString> forms{
        QStringLiteral("10-K"),
        QStringLiteral("10-K/A"),
        QStringLiteral("10-Q"),
        QStringLiteral("10-Q/A"),
        QStringLiteral("8-K"),
        QStringLiteral("8-K/A"),
        QStringLiteral("4"),
        QStringLiteral("4/A"),
        QStringLiteral("20-F"),
        QStringLiteral("20-F/A"),
        QStringLiteral("6-K"),
        QStringLiteral("6-K/A"),
    };
    return forms.contains(form.trimmed().toUpper());
}

[[nodiscard]] QString secFilingUrl(
    const QString& normalizedCik,
    QString accession,
    const QString& primaryDocument) {
    accession.remove(u'-');
    static const QRegularExpression safeAccession(QStringLiteral("^\\d{18}$"));
    static const QRegularExpression safeDocument(
        QStringLiteral("^[A-Za-z0-9_.-]{1,255}$"));
    if (!safeAccession.match(accession).hasMatch() ||
        !safeDocument.match(primaryDocument).hasMatch() ||
        primaryDocument.contains(QStringLiteral(".."))) {
        return {};
    }
    auto cikWithoutZeros = normalizedCik;
    while (cikWithoutZeros.size() > 1 && cikWithoutZeros.startsWith(u'0')) {
        cikWithoutZeros.remove(0, 1);
    }
    return QStringLiteral(
               "https://www.sec.gov/Archives/edgar/data/%1/%2/%3")
        .arg(cikWithoutZeros, accession, primaryDocument);
}

[[nodiscard]] bool majorFredRelease(const QString& name) {
    static const std::array needles{
        QStringLiteral("consumer price index"),
        QStringLiteral("producer price index"),
        QStringLiteral("employment situation"),
        QStringLiteral("gross domestic product"),
        QStringLiteral("personal income and outlays"),
        QStringLiteral("advance monthly sales for retail"),
        QStringLiteral("industrial production and capacity utilization"),
        QStringLiteral("new residential sales"),
        QStringLiteral("federal open market committee"),
    };
    const auto folded = name.toCaseFolded();
    return std::ranges::any_of(
        needles,
        [&](const QString& needle) { return folded.contains(needle); });
}

[[nodiscard]] QString fredError(const QJsonObject& object) {
    const auto message =
        object.value(QStringLiteral("error_message")).toString().trimmed();
    return message.isEmpty()
               ? QString{}
               : QStringLiteral("FRED: %1").arg(message);
}

} // namespace

SecTickerLookupResult EventIntelligenceParser::parseSecTickerMap(
    const QByteArray& payload,
    const QString& requestedSymbol) {
    QJsonParseError parseError;
    const auto document = QJsonDocument::fromJson(payload, &parseError);
    if (parseError.error != QJsonParseError::NoError ||
        !document.isObject()) {
        return {.error = QStringLiteral("SEC ticker map returned invalid JSON.")};
    }
    const auto expected = normalizeWatchlistSymbol(requestedSymbol);
    if (expected.isEmpty()) {
        return {.error = QStringLiteral("A valid SEC ticker is required.")};
    }
    for (const auto& value : document.object()) {
        if (!value.isObject()) {
            continue;
        }
        const auto object = value.toObject();
        const auto ticker = normalizeWatchlistSymbol(
            object.value(QStringLiteral("ticker")).toString());
        if (ticker != expected) {
            continue;
        }
        auto cik = jsonCik(object.value(QStringLiteral("cik_str")));
        if (cik.isEmpty()) {
            return {.error = QStringLiteral("SEC ticker map contains an invalid CIK.")};
        }
        return {
            .cik = std::move(cik),
            .companyName =
                object.value(QStringLiteral("title")).toString().trimmed(),
        };
    }
    return {
        .error =
            QStringLiteral("SEC EDGAR has no ticker mapping for %1.")
                .arg(expected),
    };
}

EventIntelligenceParseResult
EventIntelligenceParser::parseSecSubmissions(
    const QByteArray& payload,
    const QString& requestedSymbol,
    const QString& expectedCik,
    const std::int64_t retrievedAtUtc) {
    QJsonParseError parseError;
    const auto document = QJsonDocument::fromJson(payload, &parseError);
    if (parseError.error != QJsonParseError::NoError ||
        !document.isObject()) {
        return {.error = QStringLiteral("SEC submissions returned invalid JSON.")};
    }
    const auto object = document.object();
    const auto normalizedExpectedCik = normalizeCik(expectedCik);
    const auto returnedCik =
        jsonCik(object.value(QStringLiteral("cik")));
    const auto symbol = normalizeWatchlistSymbol(requestedSymbol);
    if (retrievedAtUtc <= 0 || symbol.isEmpty() ||
        normalizedExpectedCik.isEmpty() ||
        returnedCik != normalizedExpectedCik) {
        return {
            .error =
                QStringLiteral("SEC submissions identity or provenance is invalid."),
        };
    }
    const auto returnedTickers =
        object.value(QStringLiteral("tickers")).toArray();
    if (!returnedTickers.isEmpty()) {
        const auto tickerMatches = std::ranges::any_of(
            returnedTickers,
            [&](const QJsonValue& value) {
                return normalizeWatchlistSymbol(value.toString()) == symbol;
            });
        if (!tickerMatches) {
            return {
                .error =
                    QStringLiteral(
                        "SEC submissions returned a different ticker mapping."),
            };
        }
    }
    const auto filings =
        object.value(QStringLiteral("filings")).toObject();
    const auto recent =
        filings.value(QStringLiteral("recent")).toObject();
    const auto accessions =
        recent.value(QStringLiteral("accessionNumber")).toArray();
    const auto filingDates =
        recent.value(QStringLiteral("filingDate")).toArray();
    const auto reportDates =
        recent.value(QStringLiteral("reportDate")).toArray();
    const auto forms = recent.value(QStringLiteral("form")).toArray();
    const auto primaryDocuments =
        recent.value(QStringLiteral("primaryDocument")).toArray();
    if (accessions.isEmpty() || filingDates.isEmpty() || forms.isEmpty()) {
        return {.error = QStringLiteral("SEC submissions contain no recent filings.")};
    }
    const auto count = std::min({
        accessions.size(),
        filingDates.size(),
        forms.size(),
        kMaximumProviderEvents,
    });

    EventIntelligenceParseResult result;
    result.events.reserve(static_cast<std::size_t>(count));
    QSet<QString> identities;
    for (qsizetype index = 0; index < count; ++index) {
        const auto accession = accessions.at(index).toString().trimmed();
        const auto filingDate = QDate::fromString(
            filingDates.at(index).toString(),
            Qt::ISODate);
        const auto form = forms.at(index).toString().trimmed().toUpper();
        if (accession.isEmpty() || !filingDate.isValid() ||
            !trackedSecForm(form)) {
            continue;
        }
        const auto identity =
            QStringLiteral("sec-edgar-%1").arg(accession);
        if (identities.contains(identity)) {
            continue;
        }
        identities.insert(identity);
        const auto reportDate =
            index < reportDates.size()
                ? QDate::fromString(
                      reportDates.at(index).toString(),
                      Qt::ISODate)
                : QDate{};
        const auto documentName =
            index < primaryDocuments.size()
                ? primaryDocuments.at(index).toString().trimmed()
                : QString{};
        const auto sourceUrl = secFilingUrl(
            normalizedExpectedCik,
            accession,
            documentName);
        QStringList details{
            QStringLiteral("Accession %1").arg(accession),
        };
        if (reportDate.isValid()) {
            details.push_back(
                QStringLiteral("Report period %1")
                    .arg(reportDate.toString(Qt::ISODate)));
        }
        if (!sourceUrl.isEmpty()) {
            details.push_back(sourceUrl);
        }
        ResearchEvent event{
            .id = identity,
            .symbol = symbol,
            .type = ResearchEventType::Filing,
            .scheduledDate = filingDate,
            .timeOfDay = QStringLiteral("filed"),
            .title = QStringLiteral("%1 %2 filing").arg(symbol, form),
            .source = QStringLiteral("SEC EDGAR"),
            .asOfUtc = retrievedAtUtc,
            .confidence = ResearchConfidence::Confirmed,
            .detail = details.join(QStringLiteral(" · ")),
        };
        if (validateResearchEvent(event).isEmpty()) {
            result.events.push_back(std::move(event));
        }
    }
    return result;
}

EventIntelligenceParseResult
EventIntelligenceParser::parseFredReleaseDates(
    const QByteArray& payload,
    const std::int64_t retrievedAtUtc,
    const QDate& firstDate,
    const QDate& lastDate) {
    if (retrievedAtUtc <= 0 || !firstDate.isValid() ||
        !lastDate.isValid() || firstDate > lastDate) {
        return {.error = QStringLiteral("FRED calendar bounds are invalid.")};
    }
    QJsonParseError parseError;
    const auto document = QJsonDocument::fromJson(payload, &parseError);
    if (parseError.error != QJsonParseError::NoError ||
        !document.isObject()) {
        return {.error = QStringLiteral("FRED calendar returned invalid JSON.")};
    }
    const auto object = document.object();
    if (const auto error = fredError(object); !error.isEmpty()) {
        return {.error = error};
    }
    const auto dates = object.value(QStringLiteral("release_dates"));
    if (!dates.isArray()) {
        return {.error = QStringLiteral("FRED calendar has no release dates.")};
    }

    EventIntelligenceParseResult result;
    QSet<QString> identities;
    const auto retrievedDate =
        QDateTime::fromSecsSinceEpoch(retrievedAtUtc, QTimeZone::UTC)
            .date();
    for (const auto& value : dates.toArray()) {
        if (!value.isObject() ||
            result.events.size() >=
                static_cast<std::size_t>(kMaximumProviderEvents)) {
            continue;
        }
        const auto entry = value.toObject();
        const auto date = QDate::fromString(
            entry.value(QStringLiteral("date")).toString(),
            Qt::ISODate);
        const auto releaseName =
            entry.value(QStringLiteral("release_name")).toString().trimmed();
        const auto releaseId =
            entry.value(QStringLiteral("release_id")).toInt(-1);
        if (!date.isValid() || date < firstDate || date > lastDate ||
            releaseId <= 0 || releaseName.isEmpty() ||
            releaseName.size() > 160 || !majorFredRelease(releaseName)) {
            continue;
        }
        const auto identity =
            QStringLiteral("fred-%1-%2")
                .arg(releaseId)
                .arg(date.toString(Qt::ISODate));
        if (identities.contains(identity)) {
            continue;
        }
        identities.insert(identity);
        QUrl sourceUrl(QStringLiteral("https://fred.stlouisfed.org/release"));
        QUrlQuery query;
        query.addQueryItem(QStringLiteral("rid"), QString::number(releaseId));
        sourceUrl.setQuery(query);
        const auto centralBank =
            releaseName.contains(
                QStringLiteral("Federal Open Market Committee"),
                Qt::CaseInsensitive);
        ResearchEvent event{
            .id = identity,
            .type =
                centralBank
                    ? ResearchEventType::CentralBank
                    : ResearchEventType::EconomicRelease,
            .scheduledDate = date,
            .timeOfDay = QStringLiteral("date_only"),
            .title = releaseName,
            .source = QStringLiteral("FRED"),
            .asOfUtc = retrievedAtUtc,
            .confidence =
                date < retrievedDate
                    ? ResearchConfidence::Confirmed
                    : ResearchConfidence::Estimated,
            .detail =
                QStringLiteral(
                    "FRED release %1 · Dates are published by the source "
                    "and may differ from data availability · %2")
                    .arg(releaseId)
                    .arg(sourceUrl.toString()),
        };
        if (validateResearchEvent(event).isEmpty()) {
            result.events.push_back(std::move(event));
        }
    }
    return result;
}

} // namespace tvchart
