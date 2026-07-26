#pragma once

#include "data/market_data_parser.hpp"
#include "domain/bar.hpp"

#include <QByteArray>
#include <QObject>
#include <QString>

#ifndef Q_OS_ANDROID
#include <QPointer>
#endif

#include <cstdint>
#include <functional>

#ifdef Q_OS_ANDROID
namespace tvchart {
class AndroidHttpClient;
}
#else
class QNetworkAccessManager;
class QNetworkReply;
#endif

namespace tvchart {

struct MarketDataResult {
    Bars bars;
    Bars rawBars;
    std::vector<CorporateAction> corporateActions;
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
    void fetch(
        QString symbol,
        Timeframe timeframe,
        PriceAdjustmentMode adjustmentMode,
        Callback callback);
    void cancel();
    void setTwelveDataKey(QString apiKey);

    [[nodiscard]] bool hasTwelveDataKey() const noexcept;

private:
    void requestYahoo(
        const QString& symbol,
        Timeframe timeframe,
        PriceAdjustmentMode adjustmentMode,
        std::uint64_t generation,
        Callback callback);
    void requestTwelveData(
        const QString& symbol,
        Timeframe timeframe,
        PriceAdjustmentMode adjustmentMode,
        std::uint64_t generation,
        QString yahooError,
        Callback callback);
    void processYahooResponse(
        const QString& symbol,
        Timeframe timeframe,
        PriceAdjustmentMode adjustmentMode,
        std::uint64_t generation,
        int httpStatus,
        QString transportError,
        QByteArray payload,
        Callback callback);
    void processTwelveDataResponse(
        Timeframe timeframe,
        PriceAdjustmentMode adjustmentMode,
        std::uint64_t generation,
        QString yahooError,
        int httpStatus,
        QString transportError,
        QByteArray payload,
        Callback callback);

#ifdef Q_OS_ANDROID
    AndroidHttpClient* network_{};
    quint64 activeRequestToken_{};
#else
    QNetworkAccessManager* network_{};
    QPointer<QNetworkReply> activeReply_;
#endif
    QString twelveDataKey_;
    std::uint64_t generation_{};
};

} // namespace tvchart
