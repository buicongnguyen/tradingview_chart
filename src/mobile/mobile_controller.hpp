#pragma once

#include "domain/bar.hpp"

#include <QObject>
#include <QString>

class QJsonObject;

namespace tvchart {

class ChartBridge;
class MarketDataClient;

class MobileController final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString status READ status NOTIFY statusChanged)
    Q_PROPERTY(QString source READ source NOTIFY marketSummaryChanged)
    Q_PROPERTY(QString latestPrice READ latestPrice NOTIFY marketSummaryChanged)
    Q_PROPERTY(QString priceChange READ priceChange NOTIFY marketSummaryChanged)
    Q_PROPERTY(QString loadedRange READ loadedRange NOTIFY marketSummaryChanged)
    Q_PROPERTY(QString averageVolume READ averageVolume NOTIFY marketSummaryChanged)
    Q_PROPERTY(int barCount READ barCount NOTIFY marketSummaryChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(bool hasTwelveDataKey READ hasTwelveDataKey
                   NOTIFY twelveDataKeyChanged)
    Q_PROPERTY(qreal topSystemInset READ topSystemInset
                   CONSTANT)
    Q_PROPERTY(qreal bottomSystemInset READ bottomSystemInset
                   CONSTANT)

public:
    explicit MobileController(QObject* parent = nullptr);

    [[nodiscard]] QString status() const;
    [[nodiscard]] QString source() const;
    [[nodiscard]] QString latestPrice() const;
    [[nodiscard]] QString priceChange() const;
    [[nodiscard]] QString loadedRange() const;
    [[nodiscard]] QString averageVolume() const;
    [[nodiscard]] int barCount() const noexcept;
    [[nodiscard]] bool busy() const noexcept;
    [[nodiscard]] bool hasTwelveDataKey() const noexcept;
    [[nodiscard]] qreal topSystemInset() const noexcept;
    [[nodiscard]] qreal bottomSystemInset() const noexcept;

    Q_INVOKABLE void refresh(QString symbol, int timeframeIndex);
    Q_INVOKABLE void setChartStyle(int styleIndex);
    Q_INVOKABLE void setIndicator(int indicatorIndex);
    Q_INVOKABLE void setDarkTheme(bool dark);
    Q_INVOKABLE void setMarketStructureVisible(bool visible);
    Q_INVOKABLE void fitChart();
    Q_INVOKABLE void beginChartLoad();
    Q_INVOKABLE void chartLoaded();
    Q_INVOKABLE void setTwelveDataKey(const QString& apiKey);
    Q_INVOKABLE void reportChartError(const QString& message);
    Q_INVOKABLE void reportWebViewError(const QString& message);

signals:
    void statusChanged();
    void marketSummaryChanged();
    void busyChanged();
    void twelveDataKeyChanged();
    void executeChartJavaScript(const QString& script);

private:
    void showInitialDemo();
    void applySeries(
        const QString& symbol,
        Timeframe timeframe,
        const QString& source,
        Bars bars);
    void updateIndicator();
    void sendCommand(const QString& type);
    void sendCommand(const QJsonObject& command);
    void setStatus(QString status);
    void setBusy(bool busy);

    [[nodiscard]] static Timeframe timeframeFromIndex(int index) noexcept;
    [[nodiscard]] static QString timeframeLabel(Timeframe timeframe);

    ChartBridge* chartBridge_{};
    MarketDataClient* marketDataClient_{};
    Bars bars_;
    QString symbol_{QStringLiteral("AAPL")};
    QString source_{QStringLiteral("Demo · connecting")};
    QString status_{QStringLiteral("Preparing chart…")};
    QString latestPrice_{QStringLiteral("—")};
    QString priceChange_{QStringLiteral("—")};
    QString loadedRange_{QStringLiteral("—")};
    QString averageVolume_{QStringLiteral("—")};
    Timeframe timeframe_{Timeframe::FiveMinutes};
    int indicatorIndex_{1};
    int barCount_{};
    bool busy_{};
    bool chartLoaded_{};
    qreal topSystemInset_{};
    qreal bottomSystemInset_{};
};

} // namespace tvchart
