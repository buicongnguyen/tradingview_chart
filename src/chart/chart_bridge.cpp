#include "chart/chart_bridge.hpp"

#include <QJsonObject>
#include <QDateTime>
#include <QHash>
#include <QRegularExpression>
#include <QTimeZone>

#include <cmath>
#include <limits>

namespace tvchart {

ChartBridge::ChartBridge(QObject* parent)
    : QObject(parent) {}

bool ChartBridge::setSeries(
    QString symbol,
    QString timeframe,
    QString source,
    Bars bars) {
    if (const auto error = validateBars(bars)) {
        emit errorReported(QString::fromStdString(*error));
        return false;
    }

    symbol_ = std::move(symbol);
    timeframe_ = std::move(timeframe);
    source_ = std::move(source);
    bars_ = std::move(bars);
    if (ready_) {
        emit seriesChanged(symbol_, timeframe_, source_, toJson(bars_));
        emit researchEventsChanged(researchEventsToJson());
        emit priceLevelsChanged(priceLevelsToJson());
    }
    return true;
}

void ChartBridge::setDarkTheme(const bool dark) {
    dark_ = dark;
    if (ready_) {
        emit themeChanged(dark_);
    }
}

void ChartBridge::setChartStyle(QString style) {
    if (style != QStringLiteral("candlestick") &&
        style != QStringLiteral("line") &&
        style != QStringLiteral("area")) {
        emit errorReported(QStringLiteral("Unsupported chart style: %1").arg(style));
        return;
    }
    style_ = std::move(style);
    if (ready_) {
        emit chartStyleChanged(style_);
    }
}

void ChartBridge::setPriceScaleMode(QString mode) {
    if (mode != QStringLiteral("normal") &&
        mode != QStringLiteral("logarithmic") &&
        mode != QStringLiteral("percentage")) {
        emit errorReported(QStringLiteral("Unsupported price scale mode: %1").arg(mode));
        return;
    }
    priceScaleMode_ = std::move(mode);
    if (ready_) {
        emit priceScaleModeChanged(priceScaleMode_);
    }
}

void ChartBridge::setIndicators(
    std::vector<IndicatorCalculation> calculations) {
    indicators_ = std::move(calculations);
    if (ready_) {
        emit indicatorsChanged(toJson(indicators_));
    }
}

void ChartBridge::setResearchEvents(std::vector<ResearchEvent> events) {
    researchEvents_ = std::move(events);
    if (ready_) {
        emit researchEventsChanged(researchEventsToJson());
    }
}

bool ChartBridge::setPriceLevels(std::vector<ChartPriceLevel> levels) {
    constexpr auto maximumLevels = std::size_t{32};
    static const QRegularExpression colorPattern(
        QStringLiteral("^#[0-9A-Fa-f]{6}([0-9A-Fa-f]{2})?$"));
    static const QRegularExpression symbolPattern(
        QStringLiteral("^[A-Z0-9.^][A-Z0-9.^=_/-]{0,31}$"));
    if (levels.size() > maximumLevels) {
        emit errorReported(QStringLiteral("At most 32 chart price levels are supported."));
        return false;
    }
    QHash<QString, bool> identities;
    for (auto& level : levels) {
        level.id = level.id.trimmed();
        level.symbol = level.symbol.trimmed().toUpper();
        level.title = level.title.trimmed();
        if (level.id.isEmpty() || level.id.size() > 64 ||
            identities.contains(level.id) ||
            !symbolPattern.match(level.symbol).hasMatch()) {
            emit errorReported(QStringLiteral(
                "Every chart price level needs a valid symbol and a unique short ID."));
            return false;
        }
        if (!std::isfinite(level.price) || level.price <= 0.0 ||
            level.title.size() > 80 ||
            !colorPattern.match(level.color).hasMatch()) {
            emit errorReported(QStringLiteral(
                "Chart price levels require a positive price, a short title, and a hex color."));
            return false;
        }
        identities.insert(level.id, true);
    }
    priceLevels_ = std::move(levels);
    if (ready_) {
        emit priceLevelsChanged(priceLevelsToJson());
    }
    return true;
}

void ChartBridge::setVisibleRange(
    const std::int64_t from,
    const std::int64_t to) {
    if (ready_ && from > 0 && to > from) {
        emit visibleRangeChanged(from, to);
    }
}

void ChartBridge::setCrosshairTime(const std::int64_t timestamp) {
    if (ready_ && timestamp >= -1) {
        emit crosshairTimeChanged(timestamp);
    }
}

void ChartBridge::requestFit() {
    if (ready_) {
        emit fitRequested();
    }
}

bool ChartBridge::isReady() const noexcept {
    return ready_;
}

void ChartBridge::webReady() {
    const auto firstReady = !ready_;
    ready_ = true;
    publishState();
    if (firstReady) {
        emit ready();
    }
}

void ChartBridge::reportError(const QString& message) {
    emit errorReported(message);
}

void ChartBridge::reportVisibleRange(const double from, const double to) {
    if (!std::isfinite(from) || !std::isfinite(to) ||
        from <= 0.0 || to <= from ||
        from > static_cast<double>(std::numeric_limits<qint64>::max()) ||
        to > static_cast<double>(std::numeric_limits<qint64>::max())) {
        return;
    }
    emit visibleRangeReported(
        static_cast<qint64>(from),
        static_cast<qint64>(to));
}

void ChartBridge::reportCrosshairTime(const double timestamp) {
    if (!std::isfinite(timestamp) || timestamp < -1.0 ||
        timestamp > static_cast<double>(std::numeric_limits<qint64>::max())) {
        return;
    }
    emit crosshairTimeReported(static_cast<qint64>(timestamp));
}

void ChartBridge::publishState() {
    emit themeChanged(dark_);
    emit chartStyleChanged(style_);
    emit priceScaleModeChanged(priceScaleMode_);
    if (!bars_.empty()) {
        emit seriesChanged(symbol_, timeframe_, source_, toJson(bars_));
    }
    emit indicatorsChanged(toJson(indicators_));
    emit researchEventsChanged(researchEventsToJson());
    emit priceLevelsChanged(priceLevelsToJson());
}

QJsonArray ChartBridge::toJson(const Bars& bars) {
    QJsonArray output;
    for (const auto& bar : bars) {
        output.append(QJsonObject{
            {
                QStringLiteral("time"),
                static_cast<qint64>(bar.timestamp),
            },
            {QStringLiteral("open"), bar.open},
            {QStringLiteral("high"), bar.high},
            {QStringLiteral("low"), bar.low},
            {QStringLiteral("close"), bar.close},
            {QStringLiteral("volume"), bar.volume},
        });
    }
    return output;
}

QJsonArray ChartBridge::toJson(
    const std::vector<IndicatorCalculation>& calculations) {
    QJsonArray output;
    for (const auto& calculation : calculations) {
        output.append(indicatorToJson(calculation));
    }
    return output;
}

QJsonObject ChartBridge::indicatorToJson(
    const IndicatorCalculation& calculation) {
    const auto kindId = indicatorId(calculation.kind);
    const auto pointsToJson = [](const std::vector<IndicatorPoint>& points) {
        QJsonArray output;
        for (const auto& point : points) {
            output.append(QJsonObject{
                {
                    QStringLiteral("time"),
                    static_cast<qint64>(point.timestamp),
                },
                {QStringLiteral("value"), point.value},
            });
        }
        return output;
    };

    return {
        {
            QStringLiteral("kind"),
            QString::fromLatin1(
                kindId.data(),
                static_cast<qsizetype>(kindId.size())),
        },
        {
            QStringLiteral("label"),
            QString::fromStdString(calculation.label),
        },
        {QStringLiteral("period"), static_cast<int>(calculation.spec.period)},
        {QStringLiteral("fastPeriod"), static_cast<int>(calculation.spec.fastPeriod)},
        {QStringLiteral("slowPeriod"), static_cast<int>(calculation.spec.slowPeriod)},
        {QStringLiteral("signalPeriod"), static_cast<int>(calculation.spec.signalPeriod)},
        {QStringLiteral("primary"), pointsToJson(calculation.primary)},
        {QStringLiteral("secondary"), pointsToJson(calculation.secondary)},
        {QStringLiteral("histogram"), pointsToJson(calculation.histogram)},
    };
}

QJsonArray ChartBridge::researchEventsToJson() const {
    QHash<QDate, std::int64_t> firstBarByDate;
    for (const auto& bar : bars_) {
        const auto date =
            QDateTime::fromSecsSinceEpoch(bar.timestamp, QTimeZone::UTC)
                .date();
        if (!firstBarByDate.contains(date)) {
            firstBarByDate.insert(date, bar.timestamp);
        }
    }
    QJsonArray output;
    for (const auto& event : researchEvents_) {
        if (!validateResearchEvent(event).isEmpty()) {
            continue;
        }
        const auto found = firstBarByDate.constFind(event.scheduledDate);
        if (found == firstBarByDate.cend()) {
            continue;
        }
        QString text;
        QString color;
        switch (event.type) {
        case ResearchEventType::Earnings:
            text = QStringLiteral("ER");
            color = QStringLiteral("#ab47bc");
            break;
        case ResearchEventType::Filing:
            text = QStringLiteral("SEC");
            color = QStringLiteral("#42a5f5");
            break;
        case ResearchEventType::EconomicRelease:
            text = QStringLiteral("ECO");
            color = QStringLiteral("#ff9800");
            break;
        case ResearchEventType::CentralBank:
            text = QStringLiteral("FED");
            color = QStringLiteral("#ef5350");
            break;
        case ResearchEventType::ExDividend:
        case ResearchEventType::DividendPayment:
            text = QStringLiteral("DIV");
            color = QStringLiteral("#26a69a");
            break;
        default:
            text = QStringLiteral("EV");
            color = QStringLiteral("#787b86");
            break;
        }
        output.append(QJsonObject{
            {QStringLiteral("time"), static_cast<qint64>(found.value())},
            {QStringLiteral("position"), QStringLiteral("aboveBar")},
            {QStringLiteral("shape"), QStringLiteral("circle")},
            {QStringLiteral("color"), color},
            {QStringLiteral("text"), text},
            {QStringLiteral("title"), event.title},
        });
    }
    return output;
}

QJsonArray ChartBridge::priceLevelsToJson() const {
    QJsonArray output;
    for (const auto& level : priceLevels_) {
        if (level.symbol.compare(
                symbol_.trimmed(),
                Qt::CaseInsensitive) != 0) {
            continue;
        }
        output.append(QJsonObject{
            {QStringLiteral("id"), level.id},
            {QStringLiteral("symbol"), level.symbol},
            {QStringLiteral("price"), level.price},
            {QStringLiteral("title"), level.title},
            {QStringLiteral("color"), level.color},
        });
    }
    return output;
}

} // namespace tvchart
