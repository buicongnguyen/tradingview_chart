#include "data/market_data_client.hpp"

#include "data/market_data_parser.hpp"

#include <QDateTime>
#ifndef Q_OS_ANDROID
#include "network/bounded_network_reply.hpp"

#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#else
#include "data/android_http_client.hpp"
#endif
#include <QUrl>
#include <QUrlQuery>

#include <utility>

namespace tvchart {
namespace {

constexpr qsizetype kMaximumPayloadBytes = 10 * 1024 * 1024;

struct YahooQuery {
    QString interval;
    QString range;
};

[[nodiscard]] YahooQuery yahooQuery(const Timeframe timeframe) {
    switch (timeframe) {
    case Timeframe::OneMinute:
        return {QStringLiteral("1m"), QStringLiteral("5d")};
    case Timeframe::FiveMinutes:
        return {QStringLiteral("5m"), QStringLiteral("1mo")};
    case Timeframe::FifteenMinutes:
        return {QStringLiteral("15m"), QStringLiteral("1mo")};
    case Timeframe::OneHour:
        return {QStringLiteral("60m"), QStringLiteral("6mo")};
    case Timeframe::OneDay:
        return {QStringLiteral("1d"), QStringLiteral("1y")};
    }
    return {QStringLiteral("5m"), QStringLiteral("5d")};
}

[[nodiscard]] QString twelveDataInterval(const Timeframe timeframe) {
    switch (timeframe) {
    case Timeframe::OneMinute:
        return QStringLiteral("1min");
    case Timeframe::FiveMinutes:
        return QStringLiteral("5min");
    case Timeframe::FifteenMinutes:
        return QStringLiteral("15min");
    case Timeframe::OneHour:
        return QStringLiteral("1h");
    case Timeframe::OneDay:
        return QStringLiteral("1day");
    }
    return QStringLiteral("5min");
}

[[nodiscard]] QString yahooSymbol(QString symbol) {
    if (symbol.compare(QStringLiteral("BTCUSD"), Qt::CaseInsensitive) == 0) {
        return QStringLiteral("BTC-USD");
    }
    return symbol.trimmed().toUpper();
}

[[nodiscard]] QString twelveDataSymbol(QString symbol) {
    if (symbol.compare(QStringLiteral("BTCUSD"), Qt::CaseInsensitive) == 0) {
        return QStringLiteral("BTC/USD");
    }
    return symbol.trimmed().toUpper();
}

#ifndef Q_OS_ANDROID
[[nodiscard]] QNetworkRequest marketDataRequest(const QUrl& url) {
    QNetworkRequest request(url);
    request.setHeader(
        QNetworkRequest::UserAgentHeader,
        QStringLiteral("TradingViewChart/0.5.0 (Qt 6; personal client)"));
    request.setRawHeader("Accept", "application/json");
    request.setTransferTimeout(15'000);
    request.setAttribute(
        QNetworkRequest::RedirectPolicyAttribute,
        QNetworkRequest::NoLessSafeRedirectPolicy);
    return request;
}

#endif

[[nodiscard]] QString responseError(
    const int httpStatus,
    const QString& transportError,
    const QString& provider) {
    if (!transportError.isEmpty()) {
        return QStringLiteral("%1 request failed: %2")
            .arg(provider, transportError);
    }
    if (httpStatus < 200 || httpStatus >= 300) {
        return QStringLiteral("%1 returned HTTP %2.")
            .arg(provider)
            .arg(httpStatus);
    }
    return {};
}

} // namespace

MarketDataClient::MarketDataClient(QObject* parent)
    : QObject(parent),
#ifdef Q_OS_ANDROID
      network_(new AndroidHttpClient(this)),
#else
      network_(new QNetworkAccessManager(this)),
#endif
      twelveDataKey_(qEnvironmentVariable("TWELVE_DATA_API_KEY").trimmed()) {}

void MarketDataClient::fetch(
    QString symbol,
    const Timeframe timeframe,
    Callback callback) {
    cancel();
    const auto requestGeneration = generation_;
    requestYahoo(symbol.trimmed(), timeframe, requestGeneration, std::move(callback));
}

void MarketDataClient::cancel() {
    ++generation_;
#ifdef Q_OS_ANDROID
    network_->cancel(activeRequestToken_);
    activeRequestToken_ = 0;
#else
    if (activeReply_) {
        activeReply_->abort();
        activeReply_->deleteLater();
        activeReply_.clear();
    }
#endif
}

void MarketDataClient::setTwelveDataKey(QString apiKey) {
    twelveDataKey_ = apiKey.trimmed().left(256);
}

bool MarketDataClient::hasTwelveDataKey() const noexcept {
    return !twelveDataKey_.isEmpty();
}

void MarketDataClient::requestYahoo(
    const QString& symbol,
    const Timeframe timeframe,
    const std::uint64_t requestGeneration,
    Callback callback) {
    const auto querySettings = yahooQuery(timeframe);
    const auto encodedSymbol =
        QString::fromLatin1(QUrl::toPercentEncoding(yahooSymbol(symbol)));
    QUrl url(QStringLiteral("https://query1.finance.yahoo.com/v8/finance/chart/") +
             encodedSymbol);
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("interval"), querySettings.interval);
    query.addQueryItem(QStringLiteral("range"), querySettings.range);
    query.addQueryItem(QStringLiteral("includePrePost"), QStringLiteral("false"));
    query.addQueryItem(QStringLiteral("events"), QStringLiteral("div,splits"));
    url.setQuery(query);

#ifdef Q_OS_ANDROID
    activeRequestToken_ = network_->get(
        url,
        kMaximumPayloadBytes,
        [
            this,
            symbol,
            timeframe,
            requestGeneration,
            callback = std::move(callback)
        ](
            const int httpStatus,
            QByteArray payload,
            QString transportError) mutable {
            activeRequestToken_ = 0;
            processYahooResponse(
                symbol,
                timeframe,
                requestGeneration,
                httpStatus,
                std::move(transportError),
                std::move(payload),
                std::move(callback));
        });
#else
    auto* reply = network_->get(marketDataRequest(url));
    activeReply_ = reply;
    consumeBoundedNetworkReply(
        reply,
        kMaximumPayloadBytes,
        this,
        [this,
         reply,
         symbol,
         timeframe,
         requestGeneration,
         callback = std::move(callback)](
            BoundedNetworkReplyResult response) mutable {
            if (requestGeneration != generation_) {
                return;
            }
            if (activeReply_ == reply) {
                activeReply_.clear();
            }

            if (response.limitExceeded) {
                response.transportError =
                    QStringLiteral("response exceeded 10 MiB.");
            }
            processYahooResponse(
                symbol,
                timeframe,
                requestGeneration,
                response.httpStatus,
                std::move(response.transportError),
                std::move(response.payload),
                std::move(callback));
        });
#endif
}

void MarketDataClient::requestTwelveData(
    const QString& symbol,
    const Timeframe timeframe,
    const std::uint64_t requestGeneration,
    QString yahooError,
    Callback callback) {
    QUrl url(QStringLiteral("https://api.twelvedata.com/time_series"));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("symbol"), twelveDataSymbol(symbol));
    query.addQueryItem(QStringLiteral("interval"), twelveDataInterval(timeframe));
    query.addQueryItem(QStringLiteral("outputsize"), QStringLiteral("600"));
    query.addQueryItem(QStringLiteral("timezone"), QStringLiteral("UTC"));
    query.addQueryItem(QStringLiteral("format"), QStringLiteral("JSON"));
    query.addQueryItem(QStringLiteral("apikey"), twelveDataKey_);
    url.setQuery(query);

#ifdef Q_OS_ANDROID
    activeRequestToken_ = network_->get(
        url,
        kMaximumPayloadBytes,
        [
            this,
            requestGeneration,
            yahooError = std::move(yahooError),
            callback = std::move(callback)
        ](
            const int httpStatus,
            QByteArray payload,
            QString transportError) mutable {
            activeRequestToken_ = 0;
            processTwelveDataResponse(
                requestGeneration,
                std::move(yahooError),
                httpStatus,
                std::move(transportError),
                std::move(payload),
                std::move(callback));
        });
#else
    auto* reply = network_->get(marketDataRequest(url));
    activeReply_ = reply;
    consumeBoundedNetworkReply(
        reply,
        kMaximumPayloadBytes,
        this,
        [this,
         reply,
         requestGeneration,
         yahooError = std::move(yahooError),
         callback = std::move(callback)](
            BoundedNetworkReplyResult response) mutable {
            if (requestGeneration != generation_) {
                return;
            }
            if (activeReply_ == reply) {
                activeReply_.clear();
            }

            if (response.limitExceeded) {
                response.transportError =
                    QStringLiteral("response exceeded 10 MiB.");
            }
            processTwelveDataResponse(
                requestGeneration,
                std::move(yahooError),
                response.httpStatus,
                std::move(response.transportError),
                std::move(response.payload),
                std::move(callback));
        });
#endif
}

void MarketDataClient::processYahooResponse(
    const QString& symbol,
    const Timeframe timeframe,
    const std::uint64_t requestGeneration,
    const int httpStatus,
    QString transportError,
    QByteArray payload,
    Callback callback) {
    if (requestGeneration != generation_) {
        return;
    }

    auto error = responseError(
        httpStatus,
        transportError,
        QStringLiteral("Yahoo Finance"));
    if (error.isEmpty() && payload.size() > kMaximumPayloadBytes) {
        error = QStringLiteral("Yahoo Finance response exceeded 10 MiB.");
    }
    if (error.isEmpty()) {
        auto parsed = MarketDataParser::parseYahoo(payload);
        if (parsed.ok()) {
            parsed.metadata.retrievedAtUtc = QDateTime::currentSecsSinceEpoch();
            callback({
                .bars = std::move(parsed.bars),
                .source = QStringLiteral("Yahoo Finance"),
                .metadata = std::move(parsed.metadata),
            });
            return;
        }
        error = std::move(parsed.error);
    }

    if (hasTwelveDataKey()) {
        requestTwelveData(
            symbol,
            timeframe,
            requestGeneration,
            std::move(error),
            std::move(callback));
        return;
    }
    callback({
        .error = error +
                 QStringLiteral(
                     " Twelve Data fallback is disabled because no API key is "
                     "configured."),
    });
}

void MarketDataClient::processTwelveDataResponse(
    const std::uint64_t requestGeneration,
    QString yahooError,
    const int httpStatus,
    QString transportError,
    QByteArray payload,
    Callback callback) {
    if (requestGeneration != generation_) {
        return;
    }

    auto twelveError = responseError(
        httpStatus,
        transportError,
        QStringLiteral("Twelve Data"));
    if (twelveError.isEmpty() && payload.size() > kMaximumPayloadBytes) {
        twelveError = QStringLiteral("Twelve Data response exceeded 10 MiB.");
    }
    if (twelveError.isEmpty()) {
        auto parsed = MarketDataParser::parseTwelveData(payload);
        if (parsed.ok()) {
            parsed.metadata.retrievedAtUtc = QDateTime::currentSecsSinceEpoch();
            callback({
                .bars = std::move(parsed.bars),
                .source = QStringLiteral("Twelve Data"),
                .metadata = std::move(parsed.metadata),
            });
            return;
        }
        twelveError = std::move(parsed.error);
    }

    callback({
        .error =
            QStringLiteral("%1 Fallback also failed: %2")
                .arg(yahooError, twelveError),
    });
}

} // namespace tvchart
