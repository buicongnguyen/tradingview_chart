#pragma once

#include "analysis/market_structure_analyzer.hpp"
#include "analysis/technical_indicators.hpp"
#include "domain/bar.hpp"
#include "research/research_models.hpp"

#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QString>

namespace tvchart {

struct ChartPriceLevel {
    QString id;
    QString symbol;
    double price{};
    QString title;
    QString color{QStringLiteral("#2962ff")};

    [[nodiscard]] bool operator==(const ChartPriceLevel&) const = default;
};

class ChartBridge final : public QObject {
    Q_OBJECT

public:
    explicit ChartBridge(QObject* parent = nullptr);

    bool setSeries(QString symbol, QString timeframe, QString source, Bars bars);
    void setDarkTheme(bool dark);
    void setChartStyle(QString style);
    void setPriceScaleMode(QString mode);
    void setIndicators(std::vector<IndicatorCalculation> calculations);
    void setResearchEvents(std::vector<ResearchEvent> events);
    void setMarketStructure(MarketStructureReport report);
    void setMarketStructureVisible(bool visible);
    bool setPriceLevels(std::vector<ChartPriceLevel> levels);
    void setVisibleRange(std::int64_t from, std::int64_t to);
    void setCrosshairTime(std::int64_t timestamp);
    void requestFit();

    [[nodiscard]] bool isReady() const noexcept;

    Q_INVOKABLE void webReady();
    Q_INVOKABLE void reportError(const QString& message);
    Q_INVOKABLE void reportVisibleRange(double from, double to);
    Q_INVOKABLE void reportCrosshairTime(double timestamp);

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
    void researchEventsChanged(const QJsonArray& events);
    void marketStructureChanged(const QJsonObject& structure);
    void priceLevelsChanged(const QJsonArray& levels);
    void visibleRangeChanged(qint64 from, qint64 to);
    void crosshairTimeChanged(qint64 timestamp);
    void fitRequested();
    void ready();
    void errorReported(const QString& message);
    void visibleRangeReported(qint64 from, qint64 to);
    void crosshairTimeReported(qint64 timestamp);

private:
    void publishState();
    [[nodiscard]] static QJsonArray toJson(const Bars& bars);
    [[nodiscard]] static QJsonArray toJson(
        const std::vector<IndicatorCalculation>& calculations);
    [[nodiscard]] static QJsonObject indicatorToJson(
        const IndicatorCalculation& calculation);
    [[nodiscard]] QJsonArray researchEventsToJson() const;
    [[nodiscard]] QJsonObject marketStructureToJson() const;
    [[nodiscard]] QJsonArray priceLevelsToJson() const;

    QString symbol_;
    QString timeframe_;
    QString source_;
    QString style_{QStringLiteral("candlestick")};
    QString priceScaleMode_{QStringLiteral("normal")};
    Bars bars_;
    std::vector<IndicatorCalculation> indicators_;
    std::vector<ResearchEvent> researchEvents_;
    MarketStructureReport marketStructure_;
    std::vector<ChartPriceLevel> priceLevels_;
    bool dark_{true};
    bool ready_{false};
    bool marketStructureVisible_{true};
};

} // namespace tvchart
