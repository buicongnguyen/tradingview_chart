#pragma once

#include "analysis/technical_indicators.hpp"
#include "domain/bar.hpp"

#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QString>

namespace tvchart {

class ChartBridge final : public QObject {
    Q_OBJECT

public:
    explicit ChartBridge(QObject* parent = nullptr);

    bool setSeries(QString symbol, QString timeframe, QString source, Bars bars);
    void setDarkTheme(bool dark);
    void setChartStyle(QString style);
    void setPriceScaleMode(QString mode);
    void setIndicators(std::vector<IndicatorCalculation> calculations);
    void requestFit();

    [[nodiscard]] bool isReady() const noexcept;

    Q_INVOKABLE void webReady();
    Q_INVOKABLE void reportError(const QString& message);

signals:
    void seriesChanged(
        const QString& symbol,
        const QString& timeframe,
        const QString& source,
        const QJsonArray& bars);
    void themeChanged(bool dark);
    void chartStyleChanged(const QString& style);
    void priceScaleModeChanged(const QString& mode);
    void indicatorsChanged(const QJsonArray& calculations);
    void fitRequested();
    void ready();
    void errorReported(const QString& message);

private:
    void publishState();
    [[nodiscard]] static QJsonArray toJson(const Bars& bars);
    [[nodiscard]] static QJsonArray toJson(
        const std::vector<IndicatorCalculation>& calculations);
    [[nodiscard]] static QJsonObject indicatorToJson(
        const IndicatorCalculation& calculation);

    QString symbol_;
    QString timeframe_;
    QString source_;
    QString style_{QStringLiteral("candlestick")};
    QString priceScaleMode_{QStringLiteral("normal")};
    Bars bars_;
    std::vector<IndicatorCalculation> indicators_;
    bool dark_{true};
    bool ready_{false};
};

} // namespace tvchart
