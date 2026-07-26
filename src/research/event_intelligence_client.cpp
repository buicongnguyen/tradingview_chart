#include "research/event_intelligence_client.hpp"

#include "network/bounded_network_reply.hpp"
#include "research/event_intelligence_parser.hpp"
#include "watchlists/watchlist_workspace.hpp"

#include <QDate>
#include <QDateTime>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QUrl>
#include <QUrlQuery>

#include <utility>

namespace tvchart {
namespace {

constexpr qsizetype kMaximumSecPayloadBytes = 12 * 1024 * 1024;
constexpr qsizetype kMaximumFredPayloadBytes = 5 * 1024 * 1024;

[[nodiscard]] QNetworkRequest providerRequest(
    const QUrl& url,
    const QString& userAgent) {
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::UserAgentHeader, userAgent);
    request.setRawHeader("Accept", "application/json");
    request.setTransferTimeout(20'000);
    request.setAttribute(
        QNetworkRequest::RedirectPolicyAttribute,
        QNetworkRequest::NoLessSafeRedirectPolicy);
    return request;
}

[[nodiscard]] QString responseError(
    const BoundedNetworkReplyResult& response,
    const QString& provider,
    const qsizetype maximumBytes) {
    if (response.limitExceeded) {
        return QStringLiteral("%1 response exceeded the %2 MiB safety limit.")
            .arg(provider)
            .arg(maximumBytes / (1024 * 1024));
    }
    if (!response.transportError.isEmpty()) {
        return QStringLiteral("%1 request failed: %2")
            .arg(provider, response.transportError);
    }
    if (response.httpStatus < 200 || response.httpStatus >= 300) {
        return QStringLiteral("%1 returned HTTP %2.")
            .arg(provider)
            .arg(response.httpStatus);
    }
    return {};
}

[[nodiscard]] QString normalizeCik(QString cik) {
    cik = cik.trimmed();
    static const QRegularExpression pattern(QStringLiteral("^\\d{1,10}$"));
    if (!pattern.match(cik).hasMatch()) {
        return {};
    }
    while (cik.size() > 1 && cik.startsWith(u'0')) {
        cik.remove(0, 1);
    }
    return cik.rightJustified(10, u'0');
}

} // namespace

EventIntelligenceClient::EventIntelligenceClient(QObject* parent)
    : QObject(parent),
      network_(new QNetworkAccessManager(this)),
      secUserAgent_(qEnvironmentVariable("SEC_USER_AGENT").trimmed()),
      fredApiKey_(qEnvironmentVariable("FRED_API_KEY").trimmed()) {}

void EventIntelligenceClient::fetchSecFilings(
    QString symbol,
    QString knownCik,
    Callback callback) {
    cancel();
    if (!callback) {
        return;
    }
    symbol = normalizeWatchlistSymbol(std::move(symbol));
    if (symbol.isEmpty()) {
        callback({
            .provider = QStringLiteral("SEC EDGAR"),
            .error = QStringLiteral("A valid SEC ticker is required."),
        });
        return;
    }
    if (!hasSecUserAgent()) {
        callback({
            .provider = QStringLiteral("SEC EDGAR"),
            .symbol = symbol,
            .error =
                QStringLiteral(
                    "SEC_USER_AGENT is required and must identify the app "
                    "with a contact email, for example "
                    "\"TradeChartLab your@email.example\"."),
        });
        return;
    }
    knownCik = normalizeCik(std::move(knownCik));
    if (knownCik.isEmpty()) {
        requestSecTickerMap(symbol, generation_, std::move(callback));
    } else {
        requestSecSubmissions(
            symbol,
            knownCik,
            generation_,
            std::move(callback));
    }
}

void EventIntelligenceClient::fetchFredCalendar(Callback callback) {
    cancel();
    if (!callback) {
        return;
    }
    static const QRegularExpression apiKeyPattern(
        QStringLiteral("^[a-z0-9]{32}$"));
    if (!apiKeyPattern.match(fredApiKey_).hasMatch()) {
        callback({
            .provider = QStringLiteral("FRED"),
            .error =
                QStringLiteral(
                    "FRED_API_KEY is missing or invalid. The key is read "
                    "from the process environment and is not saved."),
        });
        return;
    }
    const auto firstDate = QDate::currentDate().addDays(-7);
    const auto lastDate = QDate::currentDate().addDays(90);
    QUrl url(
        QStringLiteral(
            "https://api.stlouisfed.org/fred/releases/dates"));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("api_key"), fredApiKey_);
    query.addQueryItem(QStringLiteral("file_type"), QStringLiteral("json"));
    query.addQueryItem(
        QStringLiteral("realtime_start"),
        firstDate.toString(Qt::ISODate));
    query.addQueryItem(
        QStringLiteral("realtime_end"),
        lastDate.toString(Qt::ISODate));
    query.addQueryItem(QStringLiteral("limit"), QStringLiteral("1000"));
    query.addQueryItem(QStringLiteral("sort_order"), QStringLiteral("asc"));
    query.addQueryItem(
        QStringLiteral("include_release_dates_with_no_data"),
        QStringLiteral("true"));
    url.setQuery(query);

    const auto requestGeneration = generation_;
    auto* reply = network_->get(providerRequest(
        url,
        QStringLiteral("TradeChartLab/1.1.0 (Qt; personal client)")));
    activeReply_ = reply;
    consumeBoundedNetworkReply(
        reply,
        kMaximumFredPayloadBytes,
        this,
        [this,
         reply,
         requestGeneration,
         firstDate,
         lastDate,
         callback = std::move(callback)](
            BoundedNetworkReplyResult response) mutable {
            if (requestGeneration != generation_) {
                return;
            }
            if (activeReply_ == reply) {
                activeReply_.clear();
            }
            auto error = responseError(
                response,
                QStringLiteral("FRED"),
                kMaximumFredPayloadBytes);
            if (!error.isEmpty()) {
                callback({
                    .provider = QStringLiteral("FRED"),
                    .error = std::move(error),
                });
                return;
            }
            auto parsed = EventIntelligenceParser::parseFredReleaseDates(
                response.payload,
                QDateTime::currentSecsSinceEpoch(),
                firstDate,
                lastDate);
            callback({
                .provider = QStringLiteral("FRED"),
                .events = std::move(parsed.events),
                .error = std::move(parsed.error),
            });
        });
}

void EventIntelligenceClient::cancel() {
    ++generation_;
    if (activeReply_) {
        activeReply_->abort();
        activeReply_->deleteLater();
        activeReply_.clear();
    }
}

bool EventIntelligenceClient::hasSecUserAgent() const noexcept {
    return secUserAgent_.size() <= 200 &&
           secUserAgent_.contains(u'@') &&
           !secUserAgent_.contains(u'\r') &&
           !secUserAgent_.contains(u'\n');
}

bool EventIntelligenceClient::hasFredApiKey() const noexcept {
    static const QRegularExpression apiKeyPattern(
        QStringLiteral("^[a-z0-9]{32}$"));
    return apiKeyPattern.match(fredApiKey_).hasMatch();
}

void EventIntelligenceClient::requestSecTickerMap(
    const QString& symbol,
    const std::uint64_t requestGeneration,
    Callback callback) {
    auto* reply = network_->get(providerRequest(
        QUrl(QStringLiteral("https://www.sec.gov/files/company_tickers.json")),
        secUserAgent_));
    activeReply_ = reply;
    consumeBoundedNetworkReply(
        reply,
        kMaximumSecPayloadBytes,
        this,
        [this,
         reply,
         symbol,
         requestGeneration,
         callback = std::move(callback)](
            BoundedNetworkReplyResult response) mutable {
            if (requestGeneration != generation_) {
                return;
            }
            if (activeReply_ == reply) {
                activeReply_.clear();
            }
            auto error = responseError(
                response,
                QStringLiteral("SEC"),
                kMaximumSecPayloadBytes);
            if (!error.isEmpty()) {
                callback({
                    .provider = QStringLiteral("SEC EDGAR"),
                    .symbol = symbol,
                    .error = std::move(error),
                });
                return;
            }
            auto lookup = EventIntelligenceParser::parseSecTickerMap(
                response.payload,
                symbol);
            if (!lookup.ok()) {
                callback({
                    .provider = QStringLiteral("SEC EDGAR"),
                    .symbol = symbol,
                    .error = std::move(lookup.error),
                });
                return;
            }
            requestSecSubmissions(
                symbol,
                lookup.cik,
                requestGeneration,
                std::move(callback));
        });
}

void EventIntelligenceClient::requestSecSubmissions(
    const QString& symbol,
    const QString& normalizedCik,
    const std::uint64_t requestGeneration,
    Callback callback) {
    const QUrl url(
        QStringLiteral("https://data.sec.gov/submissions/CIK%1.json")
            .arg(normalizedCik));
    auto* reply = network_->get(providerRequest(url, secUserAgent_));
    activeReply_ = reply;
    consumeBoundedNetworkReply(
        reply,
        kMaximumSecPayloadBytes,
        this,
        [this,
         reply,
         symbol,
         normalizedCik,
         requestGeneration,
         callback = std::move(callback)](
            BoundedNetworkReplyResult response) mutable {
            if (requestGeneration != generation_) {
                return;
            }
            if (activeReply_ == reply) {
                activeReply_.clear();
            }
            auto error = responseError(
                response,
                QStringLiteral("SEC"),
                kMaximumSecPayloadBytes);
            if (!error.isEmpty()) {
                callback({
                    .provider = QStringLiteral("SEC EDGAR"),
                    .symbol = symbol,
                    .error = std::move(error),
                });
                return;
            }
            auto parsed = EventIntelligenceParser::parseSecSubmissions(
                response.payload,
                symbol,
                normalizedCik,
                QDateTime::currentSecsSinceEpoch());
            callback({
                .provider = QStringLiteral("SEC EDGAR"),
                .symbol = symbol,
                .events = std::move(parsed.events),
                .error = std::move(parsed.error),
            });
        });
}

} // namespace tvchart
