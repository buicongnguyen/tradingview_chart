#include "research/research_models.hpp"

#include "util/csv_security.hpp"
#include "watchlists/watchlist_workspace.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QRegularExpression>
#include <QUrl>

#include <array>
#include <algorithm>
#include <cmath>
#include <iterator>
#include <optional>
#include <ranges>

namespace tvchart {
namespace {

constexpr auto kMaximumTargetPrice = 1'000'000'000.0;

[[nodiscard]] bool finitePositive(const double value) noexcept {
    return std::isfinite(value) && value > 0.0;
}

[[nodiscard]] bool validSymbol(
    const QString& symbol,
    const bool allowEmpty = false) {
    const auto normalized = normalizeWatchlistSymbol(symbol);
    if (normalized.isEmpty()) {
        return allowEmpty;
    }
    static const QRegularExpression pattern(
        QStringLiteral("^[A-Z0-9.^][A-Z0-9.^=_/-]{0,31}$"));
    return pattern.match(normalized).hasMatch();
}

[[nodiscard]] QString csvField(QString value) {
    value = protectSpreadsheetCsvText(std::move(value));
    value.replace(QStringLiteral("\""), QStringLiteral("\"\""));
    return QStringLiteral("\"%1\"").arg(value);
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

[[nodiscard]] std::optional<ResearchEventType> parseEventType(
    const QString& id) {
    constexpr std::array types{
        ResearchEventType::Earnings,
        ResearchEventType::ExDividend,
        ResearchEventType::DividendPayment,
        ResearchEventType::Filing,
        ResearchEventType::EconomicRelease,
        ResearchEventType::CentralBank,
        ResearchEventType::OptionsExpiration,
        ResearchEventType::MarketHoliday,
        ResearchEventType::Custom,
    };
    for (const auto type : types) {
        if (researchEventTypeId(type) == id) {
            return type;
        }
    }
    return std::nullopt;
}

[[nodiscard]] std::optional<ResearchConfidence> parseConfidence(
    const QString& id) {
    constexpr std::array values{
        ResearchConfidence::Confirmed,
        ResearchConfidence::Estimated,
        ResearchConfidence::Unknown,
    };
    for (const auto value : values) {
        if (researchConfidenceId(value) == id) {
            return value;
        }
    }
    return std::nullopt;
}

[[nodiscard]] std::optional<TargetEstimateScope> parseScope(
    const QString& id) {
    constexpr std::array values{
        TargetEstimateScope::Organization,
        TargetEstimateScope::AggregatedConsensus,
    };
    for (const auto value : values) {
        if (targetEstimateScopeId(value) == id) {
            return value;
        }
    }
    return std::nullopt;
}

[[nodiscard]] QJsonValue optionalNumber(
    const std::optional<double>& value) {
    return value ? QJsonValue(*value) : QJsonValue(QJsonValue::Null);
}

[[nodiscard]] std::optional<double> readOptionalNumber(
    const QJsonValue& value) {
    if (!value.isDouble()) {
        return std::nullopt;
    }
    const auto number = value.toDouble();
    return std::isfinite(number)
               ? std::optional<double>{number}
               : std::nullopt;
}

[[nodiscard]] QJsonObject eventToJson(const ResearchEvent& event) {
    return {
        {QStringLiteral("id"), event.id},
        {QStringLiteral("symbol"), event.symbol},
        {QStringLiteral("type"), researchEventTypeId(event.type)},
        {
            QStringLiteral("scheduledDate"),
            event.scheduledDate.toString(Qt::ISODate),
        },
        {QStringLiteral("timeOfDay"), event.timeOfDay},
        {QStringLiteral("title"), event.title},
        {QStringLiteral("source"), event.source},
        {
            QStringLiteral("asOfUtc"),
            QString::number(event.asOfUtc),
        },
        {
            QStringLiteral("confidence"),
            researchConfidenceId(event.confidence),
        },
        {QStringLiteral("estimate"), optionalNumber(event.estimate)},
        {QStringLiteral("actual"), optionalNumber(event.actual)},
        {QStringLiteral("currency"), event.currency},
        {QStringLiteral("detail"), event.detail},
    };
}

[[nodiscard]] QJsonObject targetToJson(
    const AnalystTargetEstimate& estimate) {
    return {
        {QStringLiteral("id"), estimate.id},
        {QStringLiteral("symbol"), estimate.symbol},
        {QStringLiteral("organization"), estimate.organization},
        {QStringLiteral("targetPrice"), estimate.targetPrice},
        {QStringLiteral("currency"), estimate.currency},
        {
            QStringLiteral("publishedDate"),
            estimate.publishedDate.toString(Qt::ISODate),
        },
        {QStringLiteral("rating"), estimate.rating},
        {QStringLiteral("sourceUrl"), estimate.sourceUrl},
        {QStringLiteral("scope"), targetEstimateScopeId(estimate.scope)},
    };
}

[[nodiscard]] QJsonObject snapshotToJson(
    const CompanyResearchSnapshot& snapshot) {
    return {
        {QStringLiteral("symbol"), snapshot.symbol},
        {QStringLiteral("provider"), snapshot.provider},
        {
            QStringLiteral("asOfUtc"),
            QString::number(snapshot.asOfUtc),
        },
        {QStringLiteral("name"), snapshot.name},
        {QStringLiteral("cik"), snapshot.cik},
        {QStringLiteral("exchange"), snapshot.exchange},
        {QStringLiteral("currency"), snapshot.currency},
        {QStringLiteral("sector"), snapshot.sector},
        {QStringLiteral("industry"), snapshot.industry},
        {
            QStringLiteral("marketCapitalization"),
            optionalNumber(snapshot.marketCapitalization),
        },
        {QStringLiteral("eps"), optionalNumber(snapshot.eps)},
        {QStringLiteral("peRatio"), optionalNumber(snapshot.peRatio)},
        {QStringLiteral("forwardPe"), optionalNumber(snapshot.forwardPe)},
        {QStringLiteral("beta"), optionalNumber(snapshot.beta)},
        {QStringLiteral("week52High"), optionalNumber(snapshot.week52High)},
        {QStringLiteral("week52Low"), optionalNumber(snapshot.week52Low)},
        {
            QStringLiteral("analystTargetPrice"),
            optionalNumber(snapshot.analystTargetPrice),
        },
        {QStringLiteral("ratingStrongBuy"), snapshot.ratings.strongBuy},
        {QStringLiteral("ratingBuy"), snapshot.ratings.buy},
        {QStringLiteral("ratingHold"), snapshot.ratings.hold},
        {QStringLiteral("ratingSell"), snapshot.ratings.sell},
        {QStringLiteral("ratingStrongSell"), snapshot.ratings.strongSell},
    };
}

} // namespace

int AnalystRatingCounts::total() const noexcept {
    return strongBuy + buy + hold + sell + strongSell;
}

QString researchEventTypeId(const ResearchEventType type) {
    switch (type) {
    case ResearchEventType::Earnings:
        return QStringLiteral("earnings");
    case ResearchEventType::ExDividend:
        return QStringLiteral("ex-dividend");
    case ResearchEventType::DividendPayment:
        return QStringLiteral("dividend-payment");
    case ResearchEventType::Filing:
        return QStringLiteral("filing");
    case ResearchEventType::EconomicRelease:
        return QStringLiteral("economic-release");
    case ResearchEventType::CentralBank:
        return QStringLiteral("central-bank");
    case ResearchEventType::OptionsExpiration:
        return QStringLiteral("options-expiration");
    case ResearchEventType::MarketHoliday:
        return QStringLiteral("market-holiday");
    case ResearchEventType::Custom:
        return QStringLiteral("custom");
    }
    return QStringLiteral("custom");
}

QString researchEventTypeLabel(const ResearchEventType type) {
    switch (type) {
    case ResearchEventType::Earnings:
        return QStringLiteral("Earnings");
    case ResearchEventType::ExDividend:
        return QStringLiteral("Ex-dividend");
    case ResearchEventType::DividendPayment:
        return QStringLiteral("Dividend payment");
    case ResearchEventType::Filing:
        return QStringLiteral("SEC filing");
    case ResearchEventType::EconomicRelease:
        return QStringLiteral("Economic release");
    case ResearchEventType::CentralBank:
        return QStringLiteral("Central bank");
    case ResearchEventType::OptionsExpiration:
        return QStringLiteral("Options expiration");
    case ResearchEventType::MarketHoliday:
        return QStringLiteral("Market holiday");
    case ResearchEventType::Custom:
        return QStringLiteral("Custom");
    }
    return QStringLiteral("Custom");
}

QString researchConfidenceId(const ResearchConfidence confidence) {
    switch (confidence) {
    case ResearchConfidence::Confirmed:
        return QStringLiteral("confirmed");
    case ResearchConfidence::Estimated:
        return QStringLiteral("estimated");
    case ResearchConfidence::Unknown:
        return QStringLiteral("unknown");
    }
    return QStringLiteral("unknown");
}

QString targetEstimateScopeId(const TargetEstimateScope scope) {
    return scope == TargetEstimateScope::AggregatedConsensus
               ? QStringLiteral("aggregated-consensus")
               : QStringLiteral("organization");
}

QString validateResearchEvent(const ResearchEvent& event) {
    if (event.id.trimmed().isEmpty() || event.id.size() > 160) {
        return QStringLiteral("Research event identity is missing or too long.");
    }
    if (!validSymbol(event.symbol, true)) {
        return QStringLiteral("Research event symbol is invalid.");
    }
    if (!event.scheduledDate.isValid()) {
        return QStringLiteral("Research event date is invalid.");
    }
    if (event.title.trimmed().isEmpty() || event.title.size() > 160 ||
        event.source.trimmed().isEmpty() || event.source.size() > 120) {
        return QStringLiteral("Research event title or source is invalid.");
    }
    if (event.asOfUtc <= 0 || event.timeOfDay.size() > 40 ||
        event.currency.size() > 12 || event.detail.size() > 1'024) {
        return QStringLiteral("Research event metadata is invalid.");
    }
    if ((event.estimate && !std::isfinite(*event.estimate)) ||
        (event.actual && !std::isfinite(*event.actual))) {
        return QStringLiteral("Research event numeric values are invalid.");
    }
    return {};
}

QString validateTargetEstimate(const AnalystTargetEstimate& estimate) {
    if (estimate.id.trimmed().isEmpty() || estimate.id.size() > 160) {
        return QStringLiteral("Target estimate identity is missing or too long.");
    }
    if (!validSymbol(estimate.symbol) ||
        estimate.organization.trimmed().isEmpty() ||
        estimate.organization.size() > 120 ||
        !finitePositive(estimate.targetPrice) ||
        estimate.targetPrice > kMaximumTargetPrice ||
        estimate.currency.trimmed().isEmpty() ||
        estimate.currency.size() > 12 ||
        !estimate.publishedDate.isValid() ||
        estimate.rating.size() > 80 ||
        estimate.sourceUrl.size() > 1'024) {
        return QStringLiteral("Target estimate fields are invalid.");
    }
    if (!estimate.sourceUrl.isEmpty()) {
        const QUrl url(estimate.sourceUrl);
        if (!url.isValid() ||
            (url.scheme() != QStringLiteral("https") &&
             url.scheme() != QStringLiteral("http"))) {
            return QStringLiteral("Target estimate source URL is invalid.");
        }
    }
    return {};
}

QString validateCompanySnapshot(const CompanyResearchSnapshot& snapshot) {
    if (!validSymbol(snapshot.symbol) ||
        snapshot.provider.trimmed().isEmpty() ||
        snapshot.asOfUtc <= 0 ||
        snapshot.currency.size() > 12) {
        return QStringLiteral("Company research identity or provenance is invalid.");
    }
    const std::array values{
        snapshot.marketCapitalization,
        snapshot.eps,
        snapshot.peRatio,
        snapshot.forwardPe,
        snapshot.beta,
        snapshot.week52High,
        snapshot.week52Low,
        snapshot.analystTargetPrice,
    };
    for (const auto& value : values) {
        if (value && !std::isfinite(*value)) {
            return QStringLiteral("Company research contains a non-finite value.");
        }
    }
    if (snapshot.analystTargetPrice &&
        (!finitePositive(*snapshot.analystTargetPrice) ||
         *snapshot.analystTargetPrice > kMaximumTargetPrice)) {
        return QStringLiteral("Company analyst target must be positive.");
    }
    const std::array ratings{
        snapshot.ratings.strongBuy,
        snapshot.ratings.buy,
        snapshot.ratings.hold,
        snapshot.ratings.sell,
        snapshot.ratings.strongSell,
    };
    if (std::ranges::any_of(ratings, [](const int value) {
            return value < 0 || value > 100'000;
        })) {
        return QStringLiteral("Company analyst rating counts are invalid.");
    }
    return {};
}

std::optional<TargetConsensusSummary> summarizeOrganizationTargets(
    const std::vector<AnalystTargetEstimate>& estimates,
    const QString& symbol,
    const QString& currency) {
    struct LatestOrganizationTarget {
        QString organization;
        QString id;
        QDate publishedDate;
        double value{};
    };
    std::vector<LatestOrganizationTarget> latestTargets;
    const auto normalizedSymbol = normalizeWatchlistSymbol(symbol);
    for (const auto& estimate : estimates) {
        if (estimate.scope == TargetEstimateScope::Organization &&
            normalizeWatchlistSymbol(estimate.symbol) == normalizedSymbol &&
            estimate.currency.compare(currency, Qt::CaseInsensitive) == 0 &&
            validateTargetEstimate(estimate).isEmpty()) {
            const auto existing = std::ranges::find_if(
                latestTargets,
                [&](const LatestOrganizationTarget& target) {
                    return target.organization.compare(
                               estimate.organization,
                               Qt::CaseInsensitive) == 0;
                });
            if (existing == latestTargets.end()) {
                latestTargets.push_back({
                    .organization = estimate.organization,
                    .id = estimate.id,
                    .publishedDate = estimate.publishedDate,
                    .value = estimate.targetPrice,
                });
            } else if (
                estimate.publishedDate > existing->publishedDate ||
                (estimate.publishedDate == existing->publishedDate &&
                 estimate.id > existing->id)) {
                existing->id = estimate.id;
                existing->publishedDate = estimate.publishedDate;
                existing->value = estimate.targetPrice;
            }
        }
    }
    std::vector<double> values;
    values.reserve(latestTargets.size());
    std::ranges::transform(
        latestTargets,
        std::back_inserter(values),
        &LatestOrganizationTarget::value);
    if (values.empty()) {
        return std::nullopt;
    }
    std::ranges::sort(values);
    auto total = 0.0L;
    for (const auto value : values) {
        total += static_cast<long double>(value);
    }
    const auto mean =
        static_cast<double>(total / static_cast<long double>(values.size()));
    const auto middle = values.size() / 2;
    const auto median =
        values.size() % 2 == 0
            ? values[middle - 1] +
                  ((values[middle] - values[middle - 1]) / 2.0)
            : values[middle];
    return TargetConsensusSummary{
        .organizationCount = values.size(),
        .minimum = values.front(),
        .maximum = values.back(),
        .mean = mean,
        .median = median,
        .dispersion =
            mean > 0.0 ? (values.back() - values.front()) / mean : 0.0,
    };
}

QByteArray serializeResearchWorkspace(const ResearchWorkspace& workspace) {
    QJsonArray events;
    for (const auto& event : workspace.events) {
        if (validateResearchEvent(event).isEmpty()) {
            events.append(eventToJson(event));
        }
    }
    QJsonArray targets;
    for (const auto& estimate : workspace.targetEstimates) {
        if (validateTargetEstimate(estimate).isEmpty()) {
            targets.append(targetToJson(estimate));
        }
    }
    QJsonArray snapshots;
    for (const auto& snapshot : workspace.companySnapshots) {
        if (validateCompanySnapshot(snapshot).isEmpty()) {
            snapshots.append(snapshotToJson(snapshot));
        }
    }
    return QJsonDocument(QJsonObject{
                             {
                                 QStringLiteral("schemaVersion"),
                                 ResearchWorkspace::currentSchemaVersion,
                             },
                             {QStringLiteral("events"), events},
                             {QStringLiteral("targetEstimates"), targets},
                             {QStringLiteral("companySnapshots"), snapshots},
                         })
        .toJson(QJsonDocument::Compact);
}

ResearchWorkspaceLoadResult deserializeResearchWorkspace(
    const QByteArray& json) {
    QJsonParseError parseError;
    const auto document = QJsonDocument::fromJson(json, &parseError);
    if (parseError.error != QJsonParseError::NoError ||
        !document.isObject()) {
        return {.error = QStringLiteral("Saved research data contains invalid JSON.")};
    }
    const auto root = document.object();
    if (root.value(QStringLiteral("schemaVersion")).toInt() !=
        ResearchWorkspace::currentSchemaVersion) {
        return {.error = QStringLiteral("Saved research schema is unsupported.")};
    }
    const auto eventValues = root.value(QStringLiteral("events")).toArray();
    const auto targetValues =
        root.value(QStringLiteral("targetEstimates")).toArray();
    const auto snapshotValues =
        root.value(QStringLiteral("companySnapshots")).toArray();
    if (static_cast<std::size_t>(eventValues.size()) >
            ResearchWorkspace::maximumEvents ||
        static_cast<std::size_t>(targetValues.size()) >
            ResearchWorkspace::maximumTargetEstimates ||
        static_cast<std::size_t>(snapshotValues.size()) >
            ResearchWorkspace::maximumCompanySnapshots) {
        return {.error = QStringLiteral("Saved research data exceeds safety limits.")};
    }

    ResearchWorkspace workspace;
    std::vector<QString> identities;
    for (const auto& value : eventValues) {
        const auto object = value.toObject();
        const auto type =
            parseEventType(object.value(QStringLiteral("type")).toString());
        const auto confidence = parseConfidence(
            object.value(QStringLiteral("confidence")).toString());
        bool timestampOk = false;
        const auto asOf = object.value(QStringLiteral("asOfUtc"))
                              .toString()
                              .toLongLong(&timestampOk);
        ResearchEvent event{
            .id = object.value(QStringLiteral("id")).toString(),
            .symbol = normalizeWatchlistSymbol(
                object.value(QStringLiteral("symbol")).toString()),
            .type = type.value_or(ResearchEventType::Custom),
            .scheduledDate = QDate::fromString(
                object.value(QStringLiteral("scheduledDate")).toString(),
                Qt::ISODate),
            .timeOfDay =
                object.value(QStringLiteral("timeOfDay")).toString(),
            .title = object.value(QStringLiteral("title")).toString(),
            .source = object.value(QStringLiteral("source")).toString(),
            .asOfUtc = timestampOk ? asOf : 0,
            .confidence =
                confidence.value_or(ResearchConfidence::Unknown),
            .estimate =
                readOptionalNumber(object.value(QStringLiteral("estimate"))),
            .actual =
                readOptionalNumber(object.value(QStringLiteral("actual"))),
            .currency =
                object.value(QStringLiteral("currency")).toString(),
            .detail = object.value(QStringLiteral("detail")).toString(),
        };
        if (!type || !confidence ||
            !validateResearchEvent(event).isEmpty() ||
            std::ranges::find(identities, event.id) != identities.end()) {
            return {.error = QStringLiteral("Saved research event is invalid.")};
        }
        identities.push_back(event.id);
        workspace.events.push_back(std::move(event));
    }

    identities.clear();
    for (const auto& value : targetValues) {
        const auto object = value.toObject();
        const auto scope =
            parseScope(object.value(QStringLiteral("scope")).toString());
        AnalystTargetEstimate estimate{
            .id = object.value(QStringLiteral("id")).toString(),
            .symbol = normalizeWatchlistSymbol(
                object.value(QStringLiteral("symbol")).toString()),
            .organization =
                object.value(QStringLiteral("organization")).toString(),
            .targetPrice =
                object.value(QStringLiteral("targetPrice")).toDouble(),
            .currency =
                object.value(QStringLiteral("currency")).toString().toUpper(),
            .publishedDate = QDate::fromString(
                object.value(QStringLiteral("publishedDate")).toString(),
                Qt::ISODate),
            .rating = object.value(QStringLiteral("rating")).toString(),
            .sourceUrl =
                object.value(QStringLiteral("sourceUrl")).toString(),
            .scope = scope.value_or(TargetEstimateScope::Organization),
        };
        if (!scope || !validateTargetEstimate(estimate).isEmpty() ||
            std::ranges::find(identities, estimate.id) != identities.end()) {
            return {.error = QStringLiteral("Saved target estimate is invalid.")};
        }
        identities.push_back(estimate.id);
        workspace.targetEstimates.push_back(std::move(estimate));
    }

    for (const auto& value : snapshotValues) {
        const auto object = value.toObject();
        bool timestampOk = false;
        const auto asOf = object.value(QStringLiteral("asOfUtc"))
                              .toString()
                              .toLongLong(&timestampOk);
        CompanyResearchSnapshot snapshot{
            .symbol = normalizeWatchlistSymbol(
                object.value(QStringLiteral("symbol")).toString()),
            .provider =
                object.value(QStringLiteral("provider")).toString(),
            .asOfUtc = timestampOk ? asOf : 0,
            .name = object.value(QStringLiteral("name")).toString(),
            .cik = object.value(QStringLiteral("cik")).toString(),
            .exchange =
                object.value(QStringLiteral("exchange")).toString(),
            .currency =
                object.value(QStringLiteral("currency")).toString().toUpper(),
            .sector = object.value(QStringLiteral("sector")).toString(),
            .industry =
                object.value(QStringLiteral("industry")).toString(),
            .marketCapitalization = readOptionalNumber(
                object.value(QStringLiteral("marketCapitalization"))),
            .eps = readOptionalNumber(object.value(QStringLiteral("eps"))),
            .peRatio =
                readOptionalNumber(object.value(QStringLiteral("peRatio"))),
            .forwardPe =
                readOptionalNumber(object.value(QStringLiteral("forwardPe"))),
            .beta = readOptionalNumber(object.value(QStringLiteral("beta"))),
            .week52High =
                readOptionalNumber(object.value(QStringLiteral("week52High"))),
            .week52Low =
                readOptionalNumber(object.value(QStringLiteral("week52Low"))),
            .analystTargetPrice = readOptionalNumber(
                object.value(QStringLiteral("analystTargetPrice"))),
            .ratings = {
                .strongBuy =
                    object.value(QStringLiteral("ratingStrongBuy")).toInt(),
                .buy = object.value(QStringLiteral("ratingBuy")).toInt(),
                .hold = object.value(QStringLiteral("ratingHold")).toInt(),
                .sell = object.value(QStringLiteral("ratingSell")).toInt(),
                .strongSell =
                    object.value(QStringLiteral("ratingStrongSell")).toInt(),
            },
        };
        if (!timestampOk || !validateCompanySnapshot(snapshot).isEmpty()) {
            return {.error = QStringLiteral("Saved company research is invalid.")};
        }
        workspace.companySnapshots.push_back(std::move(snapshot));
    }
    return {.workspace = std::move(workspace)};
}

QByteArray exportTargetEstimatesCsv(
    const std::vector<AnalystTargetEstimate>& estimates) {
    QString output =
        QStringLiteral(
            "symbol,organization,target,currency,published_date,rating,source_url\r\n");
    for (const auto& estimate : estimates) {
        if (!validateTargetEstimate(estimate).isEmpty()) {
            continue;
        }
        output += csvField(normalizeWatchlistSymbol(estimate.symbol));
        output += u',';
        output += csvField(estimate.organization);
        output += u',';
        output += QString::number(estimate.targetPrice, 'g', 15);
        output += u',';
        output += csvField(estimate.currency.toUpper());
        output += u',';
        output += estimate.publishedDate.toString(Qt::ISODate);
        output += u',';
        output += csvField(estimate.rating);
        output += u',';
        output += csvField(estimate.sourceUrl);
        output += QStringLiteral("\r\n");
    }
    return output.toUtf8();
}

TargetCsvResult importTargetEstimatesCsv(const QByteArray& csv) {
    const auto rows = QString::fromUtf8(csv).split(u'\n');
    if (rows.isEmpty()) {
        return {.error = QStringLiteral("The target-estimate CSV is empty.")};
    }
    bool validHeader = true;
    auto header = parseCsvRow(rows.front().trimmed(), validHeader);
    if (!validHeader) {
        return {.error = QStringLiteral("The target-estimate CSV header is invalid.")};
    }
    for (auto& field : header) {
        field = field.trimmed().toLower();
    }
    const std::array required{
        QStringLiteral("symbol"),
        QStringLiteral("organization"),
        QStringLiteral("target"),
        QStringLiteral("currency"),
        QStringLiteral("published_date"),
    };
    std::array<std::size_t, required.size()> columns{};
    for (std::size_t index = 0; index < required.size(); ++index) {
        const auto found = std::ranges::find(header, required[index]);
        if (found == header.end()) {
            return {
                .error = QStringLiteral("The target-estimate CSV requires %1.")
                             .arg(required[index]),
            };
        }
        columns[index] = static_cast<std::size_t>(
            std::distance(header.begin(), found));
    }
    const auto optionalColumn = [&header](const QString& name)
        -> std::optional<std::size_t> {
        const auto found = std::ranges::find(header, name);
        if (found == header.end()) {
            return std::nullopt;
        }
        return static_cast<std::size_t>(
            std::distance(header.begin(), found));
    };
    const auto ratingColumn = optionalColumn(QStringLiteral("rating"));
    const auto sourceColumn = optionalColumn(QStringLiteral("source_url"));

    TargetCsvResult result;
    std::vector<QString> identities;
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
        const auto lineNumber = static_cast<std::size_t>(rowIndex + 1);
        const auto maximumRequired =
            *std::ranges::max_element(columns);
        if (!validRow || maximumRequired >= fields.size() ||
            (ratingColumn && *ratingColumn >= fields.size()) ||
            (sourceColumn && *sourceColumn >= fields.size())) {
            result.rejectedLines.push_back(lineNumber);
            continue;
        }
        bool priceOk = false;
        const auto target = fields[columns[2]].trimmed().toDouble(&priceOk);
        const auto publishedDate = QDate::fromString(
            fields[columns[4]].trimmed(),
            Qt::ISODate);
        const auto symbol = normalizeWatchlistSymbol(
            restoreSpreadsheetCsvText(fields[columns[0]]));
        const auto organization =
            restoreSpreadsheetCsvText(fields[columns[1]]).trimmed();
        const auto identity =
            symbol + u'|' + organization.toCaseFolded() + u'|' +
            fields[columns[3]].trimmed().toUpper() + u'|' +
            publishedDate.toString(Qt::ISODate);
        AnalystTargetEstimate estimate{
            .id = QStringLiteral("csv-%1-%2")
                      .arg(static_cast<qulonglong>(lineNumber))
                      .arg(QString::number(qHash(identity), 16)),
            .symbol = symbol,
            .organization = organization,
            .targetPrice = priceOk ? target : 0.0,
            .currency = fields[columns[3]].trimmed().toUpper(),
            .publishedDate = publishedDate,
            .rating =
                ratingColumn
                    ? restoreSpreadsheetCsvText(fields[*ratingColumn]).trimmed()
                    : QString{},
            .sourceUrl =
                sourceColumn
                    ? restoreSpreadsheetCsvText(fields[*sourceColumn]).trimmed()
                    : QString{},
        };
        if (!validateTargetEstimate(estimate).isEmpty() ||
            std::ranges::find(identities, identity) != identities.end()) {
            result.rejectedLines.push_back(lineNumber);
            continue;
        }
        identities.push_back(identity);
        result.estimates.push_back(std::move(estimate));
    }
    return result;
}

} // namespace tvchart
