#include "data/market_data_client.hpp"

#include "data/market_data_parser.hpp"

#include <QNetworkAccessManager>
#include <QNetworkRequest>
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

[[nodiscard]] QNetworkRequest marketDataRequest(const QUrl& url) {
    QNetworkRequest request(url);
    request.setHeader(
        QNetworkRequest::UserAgentHeader,
        QStringLiteral("TradingViewChart/0.2 (Qt 6; personal desktop client)"));
    request.setRawHeader("Accept", "application/json");
    request.setTransferTimeout(15'000);
    request.setAttribute(
        QNetworkRequest::RedirectPolicyAttribute,
        QNetworkRequest::NoLessSafeRedirectPolicy);
    return request;
}

[[nodiscard]] QString replyError(QNetworkReply* reply, const QString& provider) {
    if (reply->error() != QNetworkReply::NoError) {
        return QStringLiteral("%1 request failed: %2").arg(provider, reply->errorString());
    }
    const auto status =
        reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (status < 200 || status >= 300) {
        return QStringLiteral("%1 returned HTTP %2.").arg(provider).arg(status);
    }
    return {};
}

} // namespace

MarketDataClient::MarketDataClient(QObject* parent)
    : QObject(parent),
      network_(new QNetworkAccessManager(this)),
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
    if (activeReply_) {
        activeReply_->abort();
        activeReply_->deleteLater();
        activeReply_.clear();
    }
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

    auto* reply = network_->get(marketDataRequest(url));
    activeReply_ = reply;
    connect(
        reply,
        &QNetworkReply::finished,
        this,
        [this,
         reply,
         symbol,
         timeframe,
         requestGeneration,
         callback = std::move(callback)]() mutable {
            if (requestGeneration != generation_) {
                reply->deleteLater();
                return;
            }
            if (activeReply_ == reply) {
                activeReply_.clear();
            }

            auto error = replyError(reply, QStringLiteral("Yahoo Finance"));
            const auto payload = reply->readAll();
            reply->deleteLater();
            if (error.isEmpty() && payload.size() > kMaximumPayloadBytes) {
                error = QStringLiteral("Yahoo Finance response exceeded 10 MiB.");
            }
            if (error.isEmpty()) {
                auto parsed = MarketDataParser::parseYahoo(payload);
                if (parsed.ok()) {
                    callback({
                        .bars = std::move(parsed.bars),
                        .source = QStringLiteral("Yahoo Finance"),
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
                             " Twelve Data fallback is disabled because "
                             "TWELVE_DATA_API_KEY is not set."),
            });
        });
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

    auto* reply = network_->get(marketDataRequest(url));
    activeReply_ = reply;
    connect(
        reply,
        &QNetworkReply::finished,
        this,
        [this,
         reply,
         requestGeneration,
         yahooError = std::move(yahooError),
         callback = std::move(callback)]() mutable {
            if (requestGeneration != generation_) {
                reply->deleteLater();
                return;
            }
            if (activeReply_ == reply) {
                activeReply_.clear();
            }

            auto twelveError = replyError(reply, QStringLiteral("Twelve Data"));
            const auto payload = reply->readAll();
            reply->deleteLater();
            if (twelveError.isEmpty() && payload.size() > kMaximumPayloadBytes) {
                twelveError = QStringLiteral("Twelve Data response exceeded 10 MiB.");
            }
            if (twelveError.isEmpty()) {
                auto parsed = MarketDataParser::parseTwelveData(payload);
                if (parsed.ok()) {
                    callback({
                        .bars = std::move(parsed.bars),
                        .source = QStringLiteral("Twelve Data"),
                    });
                    return;
                }
                twelveError = std::move(parsed.error);
            }

            callback({
                .error = QStringLiteral("%1 Fallback also failed: %2")
                             .arg(yahooError, twelveError),
            });
        });
}

} // namespace tvchart
