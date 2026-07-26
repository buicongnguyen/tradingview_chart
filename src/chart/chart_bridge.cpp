#include "chart/chart_bridge.hpp"

#include <QJsonObject>

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

void ChartBridge::publishState() {
    emit themeChanged(dark_);
    emit chartStyleChanged(style_);
    emit priceScaleModeChanged(priceScaleMode_);
    if (!bars_.empty()) {
        emit seriesChanged(symbol_, timeframe_, source_, toJson(bars_));
    }
    emit indicatorsChanged(toJson(indicators_));
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

} // namespace tvchart
