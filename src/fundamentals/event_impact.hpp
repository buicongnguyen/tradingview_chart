#pragma once

#include "domain/bar.hpp"

#include <QDate>
#include <QString>

#include <cstddef>
#include <optional>
#include <vector>

namespace tvchart {

struct EventImpactWindow {
    int tradingDaysAfter{};
    double securityReturnPercent{};
    std::optional<double> benchmarkReturnPercent;
    std::optional<double> abnormalReturnPercent;
};

struct EventImpactReport {
    QDate requestedEventDate;
    QDate alignedTradingDate;
    std::size_t eventBarIndex{};
    std::optional<double> openingGapPercent;
    std::optional<double> eventDayReturnPercent;
    std::optional<double> eventVolumeRatio20;
    std::vector<EventImpactWindow> windows;
    QString assumption;
    QString error;

    [[nodiscard]] bool ok() const noexcept { return error.isEmpty(); }
};

[[nodiscard]] EventImpactReport calculateEventImpact(
    const Bars& securityBars,
    const Bars& benchmarkBars,
    const QDate& eventDate,
    bool afterMarketClose = false);

} // namespace tvchart
