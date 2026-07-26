#include "fundamentals/event_impact.hpp"

#include <QDateTime>
#include <QTimeZone>

#include <algorithm>
#include <array>
#include <map>
#include <numeric>

namespace tvchart {
namespace {

[[nodiscard]] QDate barDate(const Bar& bar) {
    return QDateTime::fromSecsSinceEpoch(
               bar.timestamp,
               QTimeZone::UTC)
        .date();
}

[[nodiscard]] std::optional<double> returnPercent(
    const double start,
    const double end) {
    if (start <= 0.0 || end <= 0.0) {
        return std::nullopt;
    }
    return (end / start - 1.0) * 100.0;
}

} // namespace

EventImpactReport calculateEventImpact(
    const Bars& securityBars,
    const Bars& benchmarkBars,
    const QDate& eventDate,
    const bool afterMarketClose) {
    EventImpactReport report{
        .requestedEventDate = eventDate,
        .assumption =
            QStringLiteral(
                "The event aligns to the first UTC trading date %1 the "
                "supplied date. Returns start at the prior close; "
                "benchmark differences are descriptive, not causal.")
                .arg(
                    afterMarketClose
                        ? QStringLiteral("after")
                        : QStringLiteral("on or after")),
    };
    if (!eventDate.isValid() || validateBars(securityBars) ||
        securityBars.size() < 3) {
        report.error = QStringLiteral(
            "Event impact requires valid daily security history.");
        return report;
    }
    auto eventIndex = securityBars.size();
    for (std::size_t index = 0;
         index < securityBars.size();
         ++index) {
        if (afterMarketClose
                ? barDate(securityBars[index]) > eventDate
                : barDate(securityBars[index]) >= eventDate) {
            eventIndex = index;
            break;
        }
    }
    if (eventIndex == 0 || eventIndex >= securityBars.size()) {
        report.error = QStringLiteral(
            "The event is outside the completed price-history window.");
        return report;
    }
    report.eventBarIndex = eventIndex;
    report.alignedTradingDate = barDate(securityBars[eventIndex]);
    report.openingGapPercent = returnPercent(
        securityBars[eventIndex - 1].close,
        securityBars[eventIndex].open);
    report.eventDayReturnPercent = returnPercent(
        securityBars[eventIndex - 1].close,
        securityBars[eventIndex].close);
    if (eventIndex >= 20) {
        auto volume = 0.0;
        for (auto index = eventIndex - 20;
             index < eventIndex;
             ++index) {
            volume += securityBars[index].volume;
        }
        const auto average = volume / 20.0;
        if (average > 0.0) {
            report.eventVolumeRatio20 =
                securityBars[eventIndex].volume / average;
        }
    }

    std::map<QDate, double> benchmarkCloses;
    if (!validateBars(benchmarkBars)) {
        for (const auto& bar : benchmarkBars) {
            benchmarkCloses[barDate(bar)] = bar.close;
        }
    }
    const auto baselineDate =
        barDate(securityBars[eventIndex - 1]);
    const auto benchmarkBaseline =
        benchmarkCloses.find(baselineDate);
    constexpr std::array windows{1, 5, 20};
    for (const auto days : windows) {
        const auto endIndex =
            eventIndex + static_cast<std::size_t>(days - 1);
        if (endIndex >= securityBars.size()) {
            continue;
        }
        const auto securityReturn =
            returnPercent(
                securityBars[eventIndex - 1].close,
                securityBars[endIndex].close)
                .value_or(0.0);
        std::optional<double> benchmarkReturn;
        const auto benchmarkEnd =
            benchmarkCloses.find(barDate(securityBars[endIndex]));
        if (benchmarkBaseline != benchmarkCloses.end() &&
            benchmarkEnd != benchmarkCloses.end()) {
            benchmarkReturn = returnPercent(
                benchmarkBaseline->second,
                benchmarkEnd->second);
        }
        report.windows.push_back({
            .tradingDaysAfter = days,
            .securityReturnPercent = securityReturn,
            .benchmarkReturnPercent = benchmarkReturn,
            .abnormalReturnPercent =
                benchmarkReturn
                    ? std::optional<double>{
                          securityReturn - *benchmarkReturn}
                    : std::nullopt,
        });
    }
    if (report.windows.empty()) {
        report.error = QStringLiteral(
            "No completed post-event return window is available.");
    }
    return report;
}

} // namespace tvchart
