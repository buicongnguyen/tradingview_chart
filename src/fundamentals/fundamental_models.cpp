#include "fundamentals/fundamental_models.hpp"

#include "watchlists/watchlist_workspace.hpp"

#include <QRegularExpression>

#include <array>
#include <cmath>
#include <limits>

namespace tvchart {
namespace {

struct TagMapping {
    FundamentalMetric metric;
    const char* tag;
};

constexpr std::array kTagMappings{
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
    TagMapping{FundamentalMetric::Equity, "StockholdersEquityIncludingPortionAttributableToNoncontrollingInterest"},
    TagMapping{FundamentalMetric::OperatingCashFlow,
               "NetCashProvidedByUsedInOperatingActivities"},
    TagMapping{FundamentalMetric::CapitalExpenditure,
               "PaymentsToAcquirePropertyPlantAndEquipment"},
    TagMapping{FundamentalMetric::CapitalExpenditure,
               "PaymentsForAdditionsToPropertyPlantAndEquipment"},
    TagMapping{FundamentalMetric::DilutedShares,
               "WeightedAverageNumberOfDilutedSharesOutstanding"},
};

[[nodiscard]] bool validText(
    const QString& value,
    const qsizetype maximum,
    const bool required = true) {
    const auto trimmed = value.trimmed();
    return (!required || !trimmed.isEmpty()) &&
           trimmed.size() <= maximum &&
           !trimmed.contains(u'\0');
}

[[nodiscard]] bool validForm(const QString& form) {
    static const QRegularExpression pattern(
        QStringLiteral("^(10-K|10-Q|20-F|40-F|6-K)(/A)?$"));
    return pattern.match(form.trimmed().toUpper()).hasMatch();
}

} // namespace

QString fundamentalMetricId(const FundamentalMetric metric) {
    switch (metric) {
    case FundamentalMetric::Revenue:
        return QStringLiteral("revenue");
    case FundamentalMetric::GrossProfit:
        return QStringLiteral("gross-profit");
    case FundamentalMetric::OperatingIncome:
        return QStringLiteral("operating-income");
    case FundamentalMetric::NetIncome:
        return QStringLiteral("net-income");
    case FundamentalMetric::DilutedEps:
        return QStringLiteral("diluted-eps");
    case FundamentalMetric::Cash:
        return QStringLiteral("cash");
    case FundamentalMetric::TotalDebt:
        return QStringLiteral("total-debt");
    case FundamentalMetric::Assets:
        return QStringLiteral("assets");
    case FundamentalMetric::Liabilities:
        return QStringLiteral("liabilities");
    case FundamentalMetric::Equity:
        return QStringLiteral("equity");
    case FundamentalMetric::OperatingCashFlow:
        return QStringLiteral("operating-cash-flow");
    case FundamentalMetric::CapitalExpenditure:
        return QStringLiteral("capital-expenditure");
    case FundamentalMetric::DilutedShares:
        return QStringLiteral("diluted-shares");
    }
    return QStringLiteral("unknown");
}

QString fundamentalMetricLabel(const FundamentalMetric metric) {
    switch (metric) {
    case FundamentalMetric::Revenue:
        return QStringLiteral("Revenue");
    case FundamentalMetric::GrossProfit:
        return QStringLiteral("Gross profit");
    case FundamentalMetric::OperatingIncome:
        return QStringLiteral("Operating income");
    case FundamentalMetric::NetIncome:
        return QStringLiteral("Net income");
    case FundamentalMetric::DilutedEps:
        return QStringLiteral("Diluted EPS");
    case FundamentalMetric::Cash:
        return QStringLiteral("Cash");
    case FundamentalMetric::TotalDebt:
        return QStringLiteral("Long-term debt");
    case FundamentalMetric::Assets:
        return QStringLiteral("Assets");
    case FundamentalMetric::Liabilities:
        return QStringLiteral("Liabilities");
    case FundamentalMetric::Equity:
        return QStringLiteral("Stockholders' equity");
    case FundamentalMetric::OperatingCashFlow:
        return QStringLiteral("Operating cash flow");
    case FundamentalMetric::CapitalExpenditure:
        return QStringLiteral("Capital expenditure");
    case FundamentalMetric::DilutedShares:
        return QStringLiteral("Diluted shares");
    }
    return QStringLiteral("Unknown");
}

QString fundamentalPeriodModeLabel(const FundamentalPeriodMode mode) {
    switch (mode) {
    case FundamentalPeriodMode::Annual:
        return QStringLiteral("Annual");
    case FundamentalPeriodMode::Quarterly:
        return QStringLiteral("Quarterly");
    case FundamentalPeriodMode::TrailingTwelveMonths:
        return QStringLiteral("TTM");
    }
    return QStringLiteral("Unknown");
}

QString validateFundamentalFact(const FundamentalFact& fact) {
    const auto normalizedSymbol =
        normalizeWatchlistSymbol(fact.symbol);
    const NamedWatchlist symbolValidation{
        .id = QStringLiteral("fundamental-validation"),
        .name = QStringLiteral("Fundamental validation"),
        .entries = {{.symbol = normalizedSymbol}},
    };
    static const QRegularExpression cikPattern(
        QStringLiteral("^\\d{10}$"));
    static const QRegularExpression accessionPattern(
        QStringLiteral("^\\d{10}-\\d{2}-\\d{6}$"));
    static const QRegularExpression tokenPattern(
        QStringLiteral("^[A-Za-z0-9_.:/-]+$"));
    auto unpaddedCik = fact.cik;
    while (unpaddedCik.size() > 1 &&
           unpaddedCik.startsWith(u'0')) {
        unpaddedCik.remove(0, 1);
    }
    if (fact.symbol != normalizedSymbol ||
        !validateWatchlist(symbolValidation).isEmpty() ||
        !cikPattern.match(fact.cik).hasMatch()) {
        return QStringLiteral("Fundamental symbol or CIK is invalid.");
    }
    if (!std::isfinite(fact.value) ||
        std::abs(fact.value) > 1.0e20) {
        return QStringLiteral("Fundamental numeric value is invalid.");
    }
    if (!fact.periodEnd.isValid() || !fact.filedDate.isValid() ||
        fact.filedDate < fact.periodEnd ||
        (fact.periodStart.isValid() &&
         fact.periodStart > fact.periodEnd)) {
        return QStringLiteral("Fundamental period or filing date is invalid.");
    }
    if (!validText(fact.taxonomy, 32) ||
        !validText(fact.tag, 160) ||
        !validText(fact.unit, 40) ||
        !tokenPattern.match(fact.taxonomy).hasMatch() ||
        !tokenPattern.match(fact.tag).hasMatch() ||
        !tokenPattern.match(fact.unit).hasMatch() ||
        !validForm(fact.form)) {
        return QStringLiteral("Fundamental taxonomy, unit, or form is invalid.");
    }
    if (fact.fiscalYear < 1900 || fact.fiscalYear > 2200 ||
        !validText(fact.fiscalPeriod, 8) ||
        !accessionPattern.match(fact.accession).hasMatch() ||
        !validText(fact.frame, 40, false) ||
        !validText(fact.sourceUrl, 512) ||
        !fact.sourceUrl.startsWith(
            QStringLiteral(
                "https://www.sec.gov/Archives/edgar/data/%1/")
                .arg(unpaddedCik)) ||
        fact.retrievedAtUtc <= 0) {
        return QStringLiteral("Fundamental provenance is invalid.");
    }
    return {};
}

QString validateFundamentalCompany(const FundamentalCompany& company) {
    if (company.facts.empty() ||
        company.facts.size() > 50'000 ||
        !validText(company.name, 300) ||
        !validText(company.provider, 80) ||
        company.retrievedAtUtc <= 0) {
        return QStringLiteral("Fundamental company metadata is invalid.");
    }
    for (const auto& fact : company.facts) {
        if (fact.symbol != company.symbol ||
            fact.cik != company.cik ||
            !validateFundamentalFact(fact).isEmpty()) {
            return QStringLiteral(
                "Fundamental company contains an invalid or mismatched fact.");
        }
    }
    return {};
}

int fundamentalTagPriority(
    const FundamentalMetric metric,
    const QString& tag) {
    auto priority = 0;
    for (const auto& mapping : kTagMappings) {
        if (mapping.metric == metric) {
            if (tag == QString::fromLatin1(mapping.tag)) {
                return priority;
            }
            ++priority;
        }
    }
    return std::numeric_limits<int>::max();
}

bool isAdditiveFundamentalMetric(const FundamentalMetric metric) {
    switch (metric) {
    case FundamentalMetric::Revenue:
    case FundamentalMetric::GrossProfit:
    case FundamentalMetric::OperatingIncome:
    case FundamentalMetric::NetIncome:
    case FundamentalMetric::OperatingCashFlow:
    case FundamentalMetric::CapitalExpenditure:
        return true;
    case FundamentalMetric::DilutedEps:
    case FundamentalMetric::Cash:
    case FundamentalMetric::TotalDebt:
    case FundamentalMetric::Assets:
    case FundamentalMetric::Liabilities:
    case FundamentalMetric::Equity:
    case FundamentalMetric::DilutedShares:
        return false;
    }
    return false;
}

bool isInstantFundamentalMetric(const FundamentalMetric metric) {
    switch (metric) {
    case FundamentalMetric::Cash:
    case FundamentalMetric::TotalDebt:
    case FundamentalMetric::Assets:
    case FundamentalMetric::Liabilities:
    case FundamentalMetric::Equity:
        return true;
    default:
        return false;
    }
}

} // namespace tvchart
