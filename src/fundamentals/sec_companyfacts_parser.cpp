#include "fundamentals/sec_companyfacts_parser.hpp"

#include "watchlists/watchlist_workspace.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QRegularExpression>
#include <QSet>

#include <array>
#include <cmath>
#include <limits>
#include <optional>
#include <ranges>
#include <tuple>

namespace tvchart {
namespace {

constexpr auto kMaximumFacts = std::size_t{50'000};

struct TagMapping {
    FundamentalMetric metric;
    const char* tag;
};

constexpr std::array kMappings{
    TagMapping{FundamentalMetric::Revenue,
               "RevenueFromContractWithCustomerExcludingAssessedTax"},
    TagMapping{FundamentalMetric::Revenue, "Revenues"},
    TagMapping{FundamentalMetric::Revenue, "SalesRevenueNet"},
    TagMapping{FundamentalMetric::Revenue, "Revenue"},
    TagMapping{FundamentalMetric::GrossProfit, "GrossProfit"},
    TagMapping{FundamentalMetric::OperatingIncome, "OperatingIncomeLoss"},
    TagMapping{FundamentalMetric::NetIncome, "NetIncomeLoss"},
    TagMapping{FundamentalMetric::NetIncome, "ProfitLoss"},
    TagMapping{FundamentalMetric::DilutedEps, "EarningsPerShareDiluted"},
    TagMapping{FundamentalMetric::Cash,
               "CashAndCashEquivalentsAtCarryingValue"},
    TagMapping{FundamentalMetric::Cash,
               "CashCashEquivalentsRestrictedCashAndRestrictedCashEquivalents"},
    TagMapping{FundamentalMetric::TotalDebt,
               "LongTermDebtAndFinanceLeaseObligations"},
    TagMapping{FundamentalMetric::TotalDebt,
               "LongTermDebtAndCapitalLeaseObligations"},
    TagMapping{FundamentalMetric::TotalDebt, "LongTermDebt"},
    TagMapping{FundamentalMetric::Assets, "Assets"},
    TagMapping{FundamentalMetric::Liabilities, "Liabilities"},
    TagMapping{FundamentalMetric::Equity, "StockholdersEquity"},
    TagMapping{FundamentalMetric::Equity,
               "StockholdersEquityIncludingPortionAttributableToNoncontrollingInterest"},
    TagMapping{FundamentalMetric::OperatingCashFlow,
               "NetCashProvidedByUsedInOperatingActivities"},
    TagMapping{FundamentalMetric::CapitalExpenditure,
               "PaymentsToAcquirePropertyPlantAndEquipment"},
    TagMapping{FundamentalMetric::CapitalExpenditure,
               "PaymentsForAdditionsToPropertyPlantAndEquipment"},
    TagMapping{FundamentalMetric::DilutedShares,
               "WeightedAverageNumberOfDilutedSharesOutstanding"},
};

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
        number > 9'999'999'999.0 || std::trunc(number) != number) {
        return {};
    }
    return normalizeCik(QString::number(
        static_cast<qulonglong>(number)));
}

[[nodiscard]] std::optional<FundamentalMetric> metricForTag(
    const QString& tag) {
    for (const auto& mapping : kMappings) {
        if (tag == QString::fromLatin1(mapping.tag)) {
            return mapping.metric;
        }
    }
    return std::nullopt;
}

[[nodiscard]] bool supportedForm(const QString& form) {
    static const QSet<QString> forms{
        QStringLiteral("10-K"),
        QStringLiteral("10-K/A"),
        QStringLiteral("10-Q"),
        QStringLiteral("10-Q/A"),
        QStringLiteral("20-F"),
        QStringLiteral("20-F/A"),
        QStringLiteral("40-F"),
        QStringLiteral("40-F/A"),
        QStringLiteral("6-K"),
        QStringLiteral("6-K/A"),
    };
    return forms.contains(form);
}

[[nodiscard]] QString sourceUrl(
    QString cik,
    QString accession) {
    while (cik.size() > 1 && cik.startsWith(u'0')) {
        cik.remove(0, 1);
    }
    auto directory = accession;
    directory.remove(u'-');
    static const QRegularExpression accessionPattern(
        QStringLiteral("^\\d{10}-\\d{2}-\\d{6}$"));
    if (!accessionPattern.match(accession).hasMatch() ||
        directory.size() != 18) {
        return {};
    }
    return QStringLiteral(
               "https://www.sec.gov/Archives/edgar/data/%1/%2/%3-index.html")
        .arg(cik, directory, accession);
}

[[nodiscard]] QString identity(const FundamentalFact& fact) {
    return QStringLiteral("%1|%2|%3|%4|%5|%6|%7")
        .arg(
            fundamentalMetricId(fact.metric),
            fact.tag,
            fact.unit,
            fact.periodStart.toString(Qt::ISODate),
            fact.periodEnd.toString(Qt::ISODate),
            fact.filedDate.toString(Qt::ISODate),
            fact.accession);
}

} // namespace

SecCompanyFactsParseResult SecCompanyFactsParser::parse(
    const QByteArray& payload,
    const QString& requestedSymbol,
    const QString& expectedCik,
    const std::int64_t retrievedAtUtc) {
    QJsonParseError parseError;
    const auto document = QJsonDocument::fromJson(payload, &parseError);
    if (parseError.error != QJsonParseError::NoError ||
        !document.isObject()) {
        return {.error = QStringLiteral(
                    "SEC CompanyFacts returned invalid JSON.")};
    }
    const auto symbol = normalizeWatchlistSymbol(requestedSymbol);
    const auto cik = normalizeCik(expectedCik);
    const auto root = document.object();
    if (symbol.isEmpty() || cik.isEmpty() ||
        retrievedAtUtc <= 0 ||
        jsonCik(root.value(QStringLiteral("cik"))) != cik) {
        return {.error = QStringLiteral(
                    "SEC CompanyFacts identity or provenance is invalid.")};
    }
    FundamentalCompany company{
        .symbol = symbol,
        .cik = cik,
        .name = root.value(QStringLiteral("entityName"))
                    .toString()
                    .trimmed(),
        .retrievedAtUtc = retrievedAtUtc,
    };
    if (company.name.isEmpty() || company.name.size() > 300) {
        return {.error = QStringLiteral(
                    "SEC CompanyFacts company identity is invalid.")};
    }

    const auto factsRoot = root.value(QStringLiteral("facts")).toObject();
    if (factsRoot.isEmpty()) {
        return {.error = QStringLiteral(
                    "SEC CompanyFacts contains no facts.")};
    }
    SecCompanyFactsParseResult result;
    QSet<QString> identities;
    for (const auto& taxonomy :
         {QStringLiteral("us-gaap"), QStringLiteral("ifrs-full")}) {
        const auto concepts =
            factsRoot.value(taxonomy).toObject();
        for (auto conceptIterator = concepts.begin();
             conceptIterator != concepts.end();
             ++conceptIterator) {
            const auto metric = metricForTag(conceptIterator.key());
            if (!metric || !conceptIterator.value().isObject()) {
                continue;
            }
            const auto units =
                conceptIterator.value()
                    .toObject()
                    .value(QStringLiteral("units"))
                    .toObject();
            for (auto unit = units.begin(); unit != units.end(); ++unit) {
                if (!unit.value().isArray()) {
                    continue;
                }
                const auto unitName = unit.key().trimmed();
                for (const auto& value : unit.value().toArray()) {
                    if (company.facts.size() >= kMaximumFacts) {
                        return {
                            .rejectedFacts = result.rejectedFacts,
                            .error = QStringLiteral(
                                "SEC CompanyFacts exceeded the 50,000-fact "
                                "safety limit."),
                        };
                    }
                    const auto object = value.toObject();
                    const auto form =
                        object.value(QStringLiteral("form"))
                            .toString()
                            .trimmed()
                            .toUpper();
                    const auto periodEnd = QDate::fromString(
                        object.value(QStringLiteral("end")).toString(),
                        Qt::ISODate);
                    const auto filedDate = QDate::fromString(
                        object.value(QStringLiteral("filed")).toString(),
                        Qt::ISODate);
                    const auto periodStart = QDate::fromString(
                        object.value(QStringLiteral("start")).toString(),
                        Qt::ISODate);
                    const auto accession =
                        object.value(QStringLiteral("accn"))
                            .toString()
                            .trimmed();
                    const auto number =
                        object.value(QStringLiteral("val")).toDouble(
                            std::numeric_limits<double>::quiet_NaN());
                    if (!supportedForm(form) ||
                        !periodEnd.isValid() ||
                        !filedDate.isValid() ||
                        !std::isfinite(number)) {
                        ++result.rejectedFacts;
                        continue;
                    }
                    FundamentalFact fact{
                        .symbol = symbol,
                        .cik = cik,
                        .metric = *metric,
                        .taxonomy = taxonomy,
                        .tag = conceptIterator.key(),
                        .unit = unitName,
                        .value = number,
                        .periodStart = periodStart,
                        .periodEnd = periodEnd,
                        .filedDate = filedDate,
                        .form = form,
                        .fiscalYear =
                            object.value(QStringLiteral("fy")).toInt(),
                        .fiscalPeriod =
                            object.value(QStringLiteral("fp"))
                                .toString()
                                .trimmed()
                                .toUpper(),
                        .accession = accession,
                        .frame =
                            object.value(QStringLiteral("frame"))
                                .toString()
                                .trimmed(),
                        .sourceUrl = sourceUrl(cik, accession),
                        .retrievedAtUtc = retrievedAtUtc,
                    };
                    if (fact.frame.isEmpty()) {
                        fact.frame = QStringLiteral("");
                    }
                    const auto factIdentity = identity(fact);
                    if (!validateFundamentalFact(fact).isEmpty() ||
                        identities.contains(factIdentity)) {
                        ++result.rejectedFacts;
                        continue;
                    }
                    identities.insert(factIdentity);
                    company.facts.push_back(std::move(fact));
                }
            }
        }
    }
    if (company.facts.empty()) {
        return {
            .rejectedFacts = result.rejectedFacts,
            .error = QStringLiteral(
                "SEC CompanyFacts contained no supported usable facts."),
        };
    }
    std::ranges::sort(
        company.facts,
        [](const FundamentalFact& left,
           const FundamentalFact& right) {
            return std::tie(
                       left.filedDate,
                       left.periodEnd,
                       left.metric,
                       left.tag,
                       left.unit,
                       left.periodStart,
                       left.accession) <
                   std::tie(
                       right.filedDate,
                       right.periodEnd,
                       right.metric,
                       right.tag,
                       right.unit,
                       right.periodStart,
                       right.accession);
        });
    result.company = std::move(company);
    return result;
}

} // namespace tvchart
