#pragma once

#include "domain/bar.hpp"

#include <QJsonArray>
#include <QObject>
#include <QString>

namespace tvchart {

class ChartBridge final : public QObject {
    Q_OBJECT

public:
    explicit ChartBridge(QObject* parent = nullptr);

    bool setSeries(QString symbol, QString timeframe, Bars bars);
    void setDarkTheme(bool dark);
    void setChartStyle(QString style);
    void requestFit();

    [[nodiscard]] bool isReady() const noexcept;

    Q_INVOKABLE void webReady();
    Q_INVOKABLE void reportError(const QString& message);

signals:
    void seriesChanged(const QString& symbol, const QString& timeframe, const QJsonArray& bars);
    void themeChanged(bool dark);
    void chartStyleChanged(const QString& style);
    void fitRequested();
    void ready();
    void errorReported(const QString& message);

private:
    void publishState();
    [[nodiscard]] static QJsonArray toJson(const Bars& bars);

    QString symbol_;
    QString timeframe_;
    QString style_{QStringLiteral("candlestick")};
    Bars bars_;
    bool dark_{true};
    bool ready_{false};
};

} // namespace tvchart
