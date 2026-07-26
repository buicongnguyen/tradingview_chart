#include "fundamentals/fundamental_analysis.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <map>
#include <numeric>
#include <ranges>
#include <set>
#include <tuple>

namespace tvchart {
namespace {

using FactPointers = std::vector<const FundamentalFact*>;

[[nodiscard]] int durationDays(const FundamentalFact& fact) {
    return fact.periodStart.isValid()
               ? fact.periodStart.daysTo(fact.periodEnd) + 1
               : 0;
}

[[nodiscard]] bool compatibleUnit(
    const FundamentalMetric metric,
    const QString& unit) {
    if (metric == FundamentalMetric::DilutedShares) {
        return unit.compare(QStringLiteral("shares"), Qt::CaseInsensitive) == 0;
    }
    if (metric == FundamentalMetric::DilutedEps) {
        return unit.contains(
            QStringLiteral("shares"),
            Qt::CaseInsensitive);
    }
    return !unit.contains(
        QStringLiteral("shares"),
        Qt::CaseInsensitive);
}

[[nodiscard]] QString preferredUnit(const FactPointers& facts) {
    std::map<QString, std::size_t> counts;
    for (const auto* fact : facts) {
        ++counts[fact->unit];
    }
    QString unit;
    auto count = std::size_t{};
    for (const auto& [candidate, candidateCount] : counts) {
        if (candidateCount > count) {
            unit = candidate;
            count = candidateCount;
        }
    }
    return unit;
}

[[nodiscard]] bool betterFact(
    const FundamentalFact* candidate,
    const FundamentalFact* current) {
    if (!current) {
        return true;
    }
    if (candidate->filedDate != current->filedDate) {
        return candidate->filedDate > current->filedDate;
    }
    const auto candidatePriority =
        fundamentalTagPriority(candidate->metric, candidate->tag);
    const auto currentPriority =
        fundamentalTagPriority(current->metric, current->tag);
    if (candidatePriority != currentPriority) {
        return candidatePriority < currentPriority;
    }
    const auto candidateAmendment = candidate->form.endsWith(
        QStringLiteral("/A"));
    const auto currentAmendment = current->form.endsWith(
        QStringLiteral("/A"));
    if (candidateAmendment != currentAmendment) {
        return candidateAmendment;
    }
    return candidate->accession > current->accession;
}

[[nodiscard]] FactPointers eligibleFacts(
    const std::vector<FundamentalFact>& facts,
    const FundamentalMetric metric,
    const QDate& asOfDate) {
    FactPointers result;
    for (const auto& fact : facts) {
        if (fact.metric == metric &&
            fact.filedDate <= asOfDate &&
            compatibleUnit(metric, fact.unit) &&
            validateFundamentalFact(fact).isEmpty()) {
            result.push_back(&fact);
        }
    }
    const auto unit = preferredUnit(result);
    std::erase_if(
        result,
        [&](const FundamentalFact* fact) {
            return fact->unit != unit;
        });
    return result;
}

[[nodiscard]] FundamentalSeriesPoint point(
    const FundamentalFact& fact) {
    return {
        .periodStart = fact.periodStart,
        .periodEnd = fact.periodEnd,
        .filedDate = fact.filedDate,
        .value = fact.value,
        .unit = fact.unit,
        .derived = false,
        .provenance =
            QStringLiteral("%1 · %2 · filed %3 · %4")
                .arg(
                    fact.form,
                    fact.accession,
                    fact.filedDate.toString(Qt::ISODate),
                    fact.sourceUrl),
    };
}

[[nodiscard]] std::vector<FundamentalSeriesPoint> annualSeries(
    const FactPointers& facts) {
    std::map<QDate, const FundamentalFact*> selected;
    for (const auto* fact : facts) {
        const auto duration = durationDays(*fact);
        if (fact->periodStart.isValid() &&
            (duration < 250 || duration > 450)) {
            continue;
        }
        if (!isInstantFundamentalMetric(fact->metric) &&
            fact->fiscalPeriod != QStringLiteral("FY")) {
            continue;
        }
        auto& current = selected[fact->periodEnd];
        if (betterFact(fact, current)) {
            current = fact;
        }
    }
    std::vector<FundamentalSeriesPoint> result;
    for (const auto& [date, fact] : selected) {
        static_cast<void>(date);
        result.push_back(point(*fact));
    }
    return result;
}

[[nodiscard]] FundamentalSeriesPoint derivedQuarter(
    const FundamentalFact& total,
    const FundamentalFact& previousTotal) {
    return {
        .periodStart = previousTotal.periodEnd.addDays(1),
        .periodEnd = total.periodEnd,
        .filedDate = std::max(
            total.filedDate,
            previousTotal.filedDate),
        .value = total.value - previousTotal.value,
        .unit = total.unit,
        .derived = true,
        .provenance =
            QStringLiteral(
                "Derived from cumulative SEC facts %1 and %2; "
                "latest filed %3")
                .arg(
                    total.accession,
                    previousTotal.accession,
                    std::max(
                        total.filedDate,
                        previousTotal.filedDate)
                        .toString(Qt::ISODate)),
    };
}

[[nodiscard]] std::vector<FundamentalSeriesPoint> quarterlySeries(
    const FactPointers& facts,
    const FundamentalMetric metric) {
    if (isInstantFundamentalMetric(metric) ||
        metric == FundamentalMetric::DilutedShares) {
        std::map<QDate, const FundamentalFact*> selected;
        for (const auto* fact : facts) {
            auto& current = selected[fact->periodEnd];
            if (betterFact(fact, current)) {
                current = fact;
            }
        }
        std::vector<FundamentalSeriesPoint> result;
        for (const auto& [date, fact] : selected) {
            static_cast<void>(date);
            result.push_back(point(*fact));
        }
        return result;
    }

    std::map<QDate, const FundamentalFact*> direct;
    std::map<QDate, const FundamentalFact*> q2Cumulative;
    std::map<QDate, const FundamentalFact*> q3Cumulative;
    std::map<QDate, const FundamentalFact*> annual;
    for (const auto* fact : facts) {
        const auto duration = durationDays(*fact);
        auto* destination =
            duration >= 60 && duration <= 125 &&
                    (fact->fiscalPeriod == QStringLiteral("Q1") ||
                     fact->fiscalPeriod == QStringLiteral("Q2") ||
                     fact->fiscalPeriod == QStringLiteral("Q3"))
                ? &direct
                : duration >= 126 && duration <= 220 &&
                          fact->fiscalPeriod == QStringLiteral("Q2")
                      ? &q2Cumulative
                      : duration >= 190 && duration <= 315 &&
                                fact->fiscalPeriod == QStringLiteral("Q3")
                            ? &q3Cumulative
                            : duration >= 250 && duration <= 450 &&
                                      fact->fiscalPeriod ==
                                          QStringLiteral("FY")
                                  ? &annual
                                  : nullptr;
        if (!destination) {
            continue;
        }
        auto& selected = (*destination)[fact->periodEnd];
        if (betterFact(fact, selected)) {
            selected = fact;
        }
    }
    std::vector<FundamentalSeriesPoint> result;
    for (const auto& [end, fact] : direct) {
        static_cast<void>(end);
        result.push_back(point(*fact));
    }
    if (isAdditiveFundamentalMetric(metric)) {
        for (const auto& [end, total] : q2Cumulative) {
            if (direct.contains(end)) {
                continue;
            }
            const auto prior = std::ranges::find_if(
                direct,
                [&](const auto& entry) {
                    return entry.second->periodStart ==
                               total->periodStart &&
                           entry.second->periodEnd <
                               total->periodEnd;
                });
            if (prior != direct.end()) {
                result.push_back(
                    derivedQuarter(*total, *prior->second));
            }
        }
        for (const auto& [end, total] : q3Cumulative) {
            if (direct.contains(end)) {
                continue;
            }
            const auto prior = std::ranges::find_if(
                q2Cumulative,
                [&](const auto& entry) {
                    return entry.second->periodStart ==
                               total->periodStart &&
                           entry.second->periodEnd <
                               total->periodEnd;
                });
            if (prior != q2Cumulative.end()) {
                result.push_back(
                    derivedQuarter(*total, *prior->second));
            }
        }
        for (const auto& [end, annualFact] : annual) {
            static_cast<void>(end);
            const auto prior = std::ranges::find_if(
                q3Cumulative,
                [&](const auto& entry) {
                    return entry.second->periodStart ==
                               annualFact->periodStart &&
                           entry.second->periodEnd <
                               annualFact->periodEnd;
                });
            if (prior != q3Cumulative.end()) {
                result.push_back(
                    derivedQuarter(
                        *annualFact,
                        *prior->second));
            }
        }
    } else if (metric == FundamentalMetric::DilutedEps) {
        for (const auto& [end, annualFact] : annual) {
            static_cast<void>(end);
            std::vector<const FundamentalFact*> quarters;
            for (const auto& [quarterEnd, quarter] : direct) {
                static_cast<void>(quarterEnd);
                if (quarter->periodStart >= annualFact->periodStart &&
                    quarter->periodEnd < annualFact->periodEnd) {
                    quarters.push_back(quarter);
                }
            }
            std::ranges::sort(
                quarters,
                {},
                &FundamentalFact::periodEnd);
            if (quarters.size() < 3) {
                continue;
            }
            quarters.erase(
                quarters.begin(),
                quarters.end() - 3);
            const auto interim =
                std::accumulate(
                    quarters.begin(),
                    quarters.end(),
                    0.0,
                    [](const double total,
                       const FundamentalFact* fact) {
                        return total + fact->value;
                    });
            result.push_back(
                {
                .periodStart =
                    quarters.back()->periodEnd.addDays(1),
                .periodEnd = annualFact->periodEnd,
                .filedDate = annualFact->filedDate,
                .value =
                    annualFact->value - interim,
                .unit = annualFact->unit,
                .derived = true,
                .provenance =
                    QStringLiteral(
                        "Approximate Q4 diluted EPS derived from annual "
                        "and reported interim EPS; filed %1")
                        .arg(
                            annualFact->filedDate.toString(
                                Qt::ISODate)),
            });
        }
    }
    std::ranges::sort(
        result,
        {},
        &FundamentalSeriesPoint::periodEnd);
    std::vector<FundamentalSeriesPoint> deduplicated;
    for (auto& value : result) {
        if (!deduplicated.empty() &&
            deduplicated.back().periodEnd == value.periodEnd) {
            if (value.filedDate >=
                deduplicated.back().filedDate) {
                deduplicated.back() = std::move(value);
            }
        } else {
            deduplicated.push_back(std::move(value));
        }
    }
    return deduplicated;
}

[[nodiscard]] std::optional<double> value(
    const std::map<FundamentalMetric, FundamentalSeriesPoint>& values,
    const FundamentalMetric metric) {
    const auto found = values.find(metric);
    return found == values.end()
               ? std::nullopt
               : std::optional<double>{found->second.value};
}

[[nodiscard]] std::optional<double> latestValue(
    const FundamentalSnapshot& snapshot,
    const FundamentalMetric metric) {
    if (const auto quarterly =
            value(snapshot.latestQuarter, metric)) {
        return quarterly;
    }
    return value(snapshot.latestAnnual, metric);
}

[[nodiscard]] std::optional<double> safeRatio(
    const std::optional<double> numerator,
    const std::optional<double> denominator,
    const double scale = 1.0) {
    if (!numerator || !denominator ||
        std::abs(*denominator) <= 1.0e-12) {
        return std::nullopt;
    }
    const auto result = *numerator / *denominator * scale;
    return std::isfinite(result)
               ? std::optional<double>{result}
               : std::nullopt;
}

[[nodiscard]] std::optional<double> growthYoY(
    const std::vector<FundamentalSeriesPoint>& series) {
    if (series.size() < 2) {
        return std::nullopt;
    }
    const auto& latestPoint = series.back();
    const FundamentalSeriesPoint* priorPoint = nullptr;
    auto bestDistance = std::numeric_limits<int>::max();
    for (auto candidate = series.rbegin() + 1;
         candidate != series.rend();
         ++candidate) {
        const auto days =
            candidate->periodEnd.daysTo(
                latestPoint.periodEnd);
        if (days < 300 || days > 400) {
            continue;
        }
        const auto distance = std::abs(days - 365);
        if (distance < bestDistance) {
            priorPoint = &*candidate;
            bestDistance = distance;
        }
    }
    if (!priorPoint) {
        return std::nullopt;
    }
    const auto latest = latestPoint.value;
    const auto prior = priorPoint->value;
    if (std::abs(prior) <= 1.0e-12) {
        return std::nullopt;
    }
    const auto growth = (latest / prior - 1.0) * 100.0;
    return std::isfinite(growth)
               ? std::optional<double>{growth}
               : std::nullopt;
}

[[nodiscard]] bool consecutiveQuarterWindow(
    const std::vector<FundamentalSeriesPoint>& quarterly,
    const std::size_t first,
    const std::size_t last) {
    if (last <= first ||
        last >= quarterly.size() ||
        !quarterly[first].periodStart.isValid()) {
        return false;
    }
    for (auto index = first + 1;
         index <= last;
         ++index) {
        if (!quarterly[index].periodStart.isValid()) {
            return false;
        }
        const auto endGap =
            quarterly[index - 1].periodEnd.daysTo(
                quarterly[index].periodEnd);
        const auto startGap =
            quarterly[index - 1].periodEnd.daysTo(
                quarterly[index].periodStart);
        if (endGap < 45 || endGap > 140 ||
            startGap < 1 || startGap > 45) {
            return false;
        }
    }
    const auto duration =
        quarterly[first].periodStart.daysTo(
            quarterly[last].periodEnd) +
        1;
    return duration >= 300 && duration <= 400;
}

[[nodiscard]] double dcfEnterpriseValue(
    const double startingFcf,
    const DcfAssumptions& assumptions) {
    const auto growth = assumptions.annualGrowthPercent / 100.0;
    const auto discount = assumptions.discountRatePercent / 100.0;
    const auto terminal = assumptions.terminalGrowthPercent / 100.0;
    auto fcf = startingFcf;
    auto presentValue = 0.0;
    for (auto year = 1; year <= assumptions.forecastYears; ++year) {
        fcf *= 1.0 + growth;
        presentValue +=
            fcf / std::pow(1.0 + discount, year);
    }
    const auto terminalValue =
        fcf * (1.0 + terminal) / (discount - terminal);
    return presentValue +
           terminalValue /
               std::pow(
                   1.0 + discount,
                   assumptions.forecastYears);
}

} // namespace

std::vector<FundamentalSeriesPoint> fundamentalSeries(
    const std::vector<FundamentalFact>& facts,
    const FundamentalMetric metric,
    const FundamentalPeriodMode mode,
    const QDate& asOfDate) {
    if (!asOfDate.isValid()) {
        return {};
    }
    const auto eligible = eligibleFacts(facts, metric, asOfDate);
    if (mode == FundamentalPeriodMode::Annual) {
        return annualSeries(eligible);
    }
    auto quarterly = quarterlySeries(eligible, metric);
    if (mode == FundamentalPeriodMode::Quarterly) {
        return quarterly;
    }
    if (isInstantFundamentalMetric(metric) ||
        metric == FundamentalMetric::DilutedShares) {
        return quarterly.empty()
                   ? std::vector<FundamentalSeriesPoint>{}
                   : std::vector<FundamentalSeriesPoint>{
                         quarterly.back()};
    }
    if (quarterly.size() < 4) {
        return {};
    }
    std::vector<FundamentalSeriesPoint> result;
    for (std::size_t index = 3; index < quarterly.size(); ++index) {
        const auto first = index - 3;
        if (!consecutiveQuarterWindow(
                quarterly,
                first,
                index)) {
            continue;
        }
        auto total = 0.0;
        auto filedDate = quarterly[first].filedDate;
        auto derived = false;
        for (auto position = first;
             position <= index;
             ++position) {
            total += quarterly[position].value;
            filedDate = std::max(
                filedDate,
                quarterly[position].filedDate);
            derived =
                derived || quarterly[position].derived;
        }
        result.push_back({
            .periodStart = quarterly[first].periodStart,
            .periodEnd = quarterly[index].periodEnd,
            .filedDate = filedDate,
            .value = total,
            .unit = quarterly[index].unit,
            .derived = true,
            .provenance =
                QStringLiteral(
                    "TTM sum of four point-in-time quarterly values; "
                    "available %1%2")
                    .arg(
                        filedDate.toString(Qt::ISODate),
                        derived
                            ? QStringLiteral(
                                  " · includes derived quarter(s)")
                            : QString{}),
        });
    }
    return result;
}

FundamentalSnapshot buildFundamentalSnapshot(
    const FundamentalCompany& company,
    const QDate& asOfDate,
    const std::optional<double> currentPrice) {
    FundamentalSnapshot snapshot{
        .symbol = company.symbol,
        .asOfDate = asOfDate,
    };
    if (!asOfDate.isValid()) {
        snapshot.error = QStringLiteral(
            "A valid point-in-time analysis date is required.");
        return snapshot;
    }
    if (company.facts.empty()) {
        snapshot.error = QStringLiteral(
            "No SEC facts are available for this symbol.");
        return snapshot;
    }
    constexpr std::array metrics{
        FundamentalMetric::Revenue,
        FundamentalMetric::GrossProfit,
        FundamentalMetric::OperatingIncome,
        FundamentalMetric::NetIncome,
        FundamentalMetric::DilutedEps,
        FundamentalMetric::Cash,
        FundamentalMetric::TotalDebt,
        FundamentalMetric::Assets,
        FundamentalMetric::Liabilities,
        FundamentalMetric::Equity,
        FundamentalMetric::OperatingCashFlow,
        FundamentalMetric::CapitalExpenditure,
        FundamentalMetric::DilutedShares,
    };
    for (const auto metric : metrics) {
        auto annual = fundamentalSeries(
            company.facts,
            metric,
            FundamentalPeriodMode::Annual,
            asOfDate);
        auto quarter = fundamentalSeries(
            company.facts,
            metric,
            FundamentalPeriodMode::Quarterly,
            asOfDate);
        auto ttm = fundamentalSeries(
            company.facts,
            metric,
            FundamentalPeriodMode::TrailingTwelveMonths,
            asOfDate);
        if (!annual.empty()) {
            snapshot.latestAnnual.emplace(
                metric,
                annual.back());
            snapshot.latestFiledDate = std::max(
                snapshot.latestFiledDate,
                annual.back().filedDate);
        }
        if (!quarter.empty()) {
            snapshot.latestQuarter.emplace(
                metric,
                quarter.back());
            snapshot.latestFiledDate = std::max(
                snapshot.latestFiledDate,
                quarter.back().filedDate);
        }
        if (!ttm.empty()) {
            snapshot.trailingTwelveMonths.emplace(
                metric,
                ttm.back());
            snapshot.latestFiledDate = std::max(
                snapshot.latestFiledDate,
                ttm.back().filedDate);
        }
    }
    if (snapshot.latestAnnual.empty() &&
        snapshot.latestQuarter.empty()) {
        snapshot.error = QStringLiteral(
            "No SEC facts were public by the selected as-of date.");
        return snapshot;
    }
    const auto revenueTtm =
        value(snapshot.trailingTwelveMonths, FundamentalMetric::Revenue);
    const auto grossTtm =
        value(snapshot.trailingTwelveMonths, FundamentalMetric::GrossProfit);
    const auto operatingTtm =
        value(
            snapshot.trailingTwelveMonths,
            FundamentalMetric::OperatingIncome);
    const auto netTtm =
        value(snapshot.trailingTwelveMonths, FundamentalMetric::NetIncome);
    const auto epsTtm =
        value(snapshot.trailingTwelveMonths, FundamentalMetric::DilutedEps);
    const auto operatingCashTtm =
        value(
            snapshot.trailingTwelveMonths,
            FundamentalMetric::OperatingCashFlow);
    const auto capexTtm =
        value(
            snapshot.trailingTwelveMonths,
            FundamentalMetric::CapitalExpenditure);
    const auto equity =
        latestValue(snapshot, FundamentalMetric::Equity);
    const auto debt =
        latestValue(snapshot, FundamentalMetric::TotalDebt);
    const auto cash =
        latestValue(snapshot, FundamentalMetric::Cash);
    const auto shares =
        latestValue(snapshot, FundamentalMetric::DilutedShares);
    snapshot.derived.grossMarginPercent =
        safeRatio(grossTtm, revenueTtm, 100.0);
    snapshot.derived.operatingMarginPercent =
        safeRatio(operatingTtm, revenueTtm, 100.0);
    snapshot.derived.netMarginPercent =
        safeRatio(netTtm, revenueTtm, 100.0);
    if (operatingCashTtm && capexTtm) {
        snapshot.derived.freeCashFlow =
            *operatingCashTtm - std::abs(*capexTtm);
    }
    snapshot.derived.freeCashFlowMarginPercent =
        safeRatio(
            snapshot.derived.freeCashFlow,
            revenueTtm,
            100.0);
    snapshot.derived.debtToEquity =
        safeRatio(debt, equity);
    snapshot.derived.returnOnEquityPercent =
        safeRatio(netTtm, equity, 100.0);
    snapshot.derived.revenueGrowthYoYPercent =
        growthYoY(fundamentalSeries(
            company.facts,
            FundamentalMetric::Revenue,
            FundamentalPeriodMode::Quarterly,
            asOfDate));
    snapshot.derived.epsGrowthYoYPercent =
        growthYoY(fundamentalSeries(
            company.facts,
            FundamentalMetric::DilutedEps,
            FundamentalPeriodMode::Quarterly,
            asOfDate));

    if (currentPrice && *currentPrice > 0.0 &&
        shares && *shares > 0.0) {
        snapshot.derived.marketCapitalization =
            *currentPrice * *shares;
        snapshot.derived.priceToEarnings =
            safeRatio(*currentPrice, epsTtm);
        snapshot.derived.priceToSales =
            safeRatio(
                snapshot.derived.marketCapitalization,
                revenueTtm);
        snapshot.derived.priceToBook =
            safeRatio(
                snapshot.derived.marketCapitalization,
                equity);
        snapshot.derived.priceToFreeCashFlow =
            safeRatio(
                snapshot.derived.marketCapitalization,
                snapshot.derived.freeCashFlow);
    }
    for (const auto* values : {
             &snapshot.trailingTwelveMonths,
             &snapshot.latestAnnual,
             &snapshot.latestQuarter,
         }) {
        const auto currencySource =
            values->find(FundamentalMetric::Revenue);
        if (currencySource == values->end()) {
            continue;
        }
        const auto unit = currencySource->second.unit;
        if (unit.size() == 3) {
            snapshot.currency = unit.toUpper();
            break;
        }
    }
    if (snapshot.trailingTwelveMonths.empty()) {
        snapshot.warning = QStringLiteral(
            "TTM values are unavailable until four compatible "
            "point-in-time quarters can be reconstructed.");
    }
    static_cast<void>(cash);
    return snapshot;
}

DcfReport calculateDcf(
    const FundamentalSnapshot& snapshot,
    const DcfAssumptions& assumptions,
    const std::optional<double> currentPrice) {
    DcfReport report{
        .currency = snapshot.currency,
    };
    if (!snapshot.ok() ||
        assumptions.forecastYears < 1 ||
        assumptions.forecastYears > 20 ||
        assumptions.annualGrowthPercent <= -90.0 ||
        assumptions.annualGrowthPercent > 100.0 ||
        assumptions.discountRatePercent <= 0.0 ||
        assumptions.discountRatePercent > 100.0 ||
        assumptions.terminalGrowthPercent <= -10.0 ||
        assumptions.terminalGrowthPercent >=
            assumptions.discountRatePercent) {
        report.error = QStringLiteral(
            "DCF assumptions are invalid or the discount rate does not "
            "exceed terminal growth.");
        return report;
    }
    const auto fcf = snapshot.derived.freeCashFlow;
    const auto shares =
        latestValue(snapshot, FundamentalMetric::DilutedShares);
    const auto debt =
        latestValue(snapshot, FundamentalMetric::TotalDebt)
            .value_or(0.0);
    const auto cash =
        latestValue(snapshot, FundamentalMetric::Cash)
            .value_or(0.0);
    if (!fcf || *fcf <= 0.0 || !shares || *shares <= 0.0) {
        report.error = QStringLiteral(
            "DCF requires positive TTM free cash flow and diluted shares.");
        return report;
    }
    report.startingFreeCashFlow = *fcf;
    report.shares = *shares;
    report.netDebt = debt - cash;
    const auto growth = assumptions.annualGrowthPercent / 100.0;
    const auto discount = assumptions.discountRatePercent / 100.0;
    const auto terminal = assumptions.terminalGrowthPercent / 100.0;
    auto forecastFcf = *fcf;
    for (auto year = 1; year <= assumptions.forecastYears; ++year) {
        forecastFcf *= 1.0 + growth;
        report.presentValueForecast +=
            forecastFcf /
            std::pow(1.0 + discount, year);
    }
    const auto terminalValue =
        forecastFcf * (1.0 + terminal) /
        (discount - terminal);
    report.presentValueTerminal =
        terminalValue /
        std::pow(
            1.0 + discount,
            assumptions.forecastYears);
    report.enterpriseValue =
        report.presentValueForecast +
        report.presentValueTerminal;
    report.equityValue =
        report.enterpriseValue - report.netDebt;
    report.valuePerShare =
        report.equityValue / report.shares;
    if (!std::isfinite(report.valuePerShare)) {
        report.error = QStringLiteral(
            "DCF produced a non-finite valuation.");
        return report;
    }
    if (currentPrice && *currentPrice > 0.0) {
        const auto targetEnterpriseValue =
            *currentPrice * report.shares + report.netDebt;
        auto low = -50.0;
        auto high = 100.0;
        auto lowAssumptions = assumptions;
        auto highAssumptions = assumptions;
        lowAssumptions.annualGrowthPercent = low;
        highAssumptions.annualGrowthPercent = high;
        const auto lowValue =
            dcfEnterpriseValue(*fcf, lowAssumptions);
        const auto highValue =
            dcfEnterpriseValue(*fcf, highAssumptions);
        if (targetEnterpriseValue >= lowValue &&
            targetEnterpriseValue <= highValue) {
            for (auto iteration = 0; iteration < 100; ++iteration) {
                const auto middle = low + (high - low) / 2.0;
                auto candidate = assumptions;
                candidate.annualGrowthPercent = middle;
                if (dcfEnterpriseValue(*fcf, candidate) <
                    targetEnterpriseValue) {
                    low = middle;
                } else {
                    high = middle;
                }
            }
            report.impliedAnnualGrowthPercent =
                low + (high - low) / 2.0;
        }
    }
    return report;
}

} // namespace tvchart
