#pragma once

#include "data/market_data_parser.hpp"
#include "domain/bar.hpp"

#include <QNetworkReply>
#include <QObject>
#include <QPointer>
#include <QString>

#include <cstdint>
#include <functional>

class QNetworkAccessManager;

namespace tvchart {

struct MarketDataResult {
    Bars bars;
    QString source;
    MarketDataMetadata metadata;
    QString error;

    [[nodiscard]] bool ok() const noexcept {
        return error.isEmpty() && !bars.empty();
    }
};

class MarketDataClient final : public QObject {
    Q_OBJECT

public:
    using Callback = std::function<void(MarketDataResult)>;

    explicit MarketDataClient(QObject* parent = nullptr);

    void fetch(QString symbol, Timeframe timeframe, Callback callback);
    void cancel();

    [[nodiscard]] bool hasTwelveDataKey() const noexcept;

private:
    void requestYahoo(
        const QString& symbol,
        Timeframe timeframe,
        std::uint64_t generation,
        Callback callback);
    void requestTwelveData(
        const QString& symbol,
        Timeframe timeframe,
        std::uint64_t generation,
        QString yahooError,
        Callback callback);

    QNetworkAccessManager* network_{};
    QPointer<QNetworkReply> activeReply_;
    QString twelveDataKey_;
    std::uint64_t generation_{};
};

} // namespace tvchart
