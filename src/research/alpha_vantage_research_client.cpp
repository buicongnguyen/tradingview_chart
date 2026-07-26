#include "research/alpha_vantage_research_client.hpp"

#include "network/bounded_network_reply.hpp"
#include "research/alpha_vantage_research_parser.hpp"
#include "watchlists/watchlist_workspace.hpp"

#include <QDateTime>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>
#include <QUrlQuery>

#include <algorithm>
#include <iterator>
#include <utility>

namespace tvchart {
namespace {

constexpr qsizetype kMaximumResearchPayloadBytes = 5 * 1024 * 1024;

[[nodiscard]] QNetworkRequest researchRequest(const QUrl& url) {
    QNetworkRequest request(url);
    request.setHeader(
        QNetworkRequest::UserAgentHeader,
        QStringLiteral("TradingViewChart/0.8.0 (Qt 6; personal client)"));
    request.setRawHeader("Accept", "application/json,text/csv");
    request.setTransferTimeout(15'000);
    request.setAttribute(
        QNetworkRequest::RedirectPolicyAttribute,
        QNetworkRequest::NoLessSafeRedirectPolicy);
    return request;
}

[[nodiscard]] QString responseError(
    const BoundedNetworkReplyResult& response) {
    if (response.limitExceeded) {
        return QStringLiteral(
            "Alpha Vantage response exceeded 5 MiB.");
    }
    if (!response.transportError.isEmpty()) {
        return QStringLiteral("Alpha Vantage request failed: %1")
            .arg(response.transportError);
    }
    if (response.httpStatus < 200 || response.httpStatus >= 300) {
        return QStringLiteral("Alpha Vantage returned HTTP %1.")
            .arg(response.httpStatus);
    }
    return {};
}

[[nodiscard]] QUrl alphaVantageUrl(
    const QString& function,
    const QString& symbol,
    const QString& apiKey) {
    QUrl url(QStringLiteral("https://www.alphavantage.co/query"));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("function"), function);
    query.addQueryItem(QStringLiteral("symbol"), symbol);
    if (function == QStringLiteral("EARNINGS_CALENDAR")) {
        query.addQueryItem(QStringLiteral("horizon"), QStringLiteral("12month"));
    }
    query.addQueryItem(QStringLiteral("apikey"), apiKey);
    url.setQuery(query);
    return url;
}

} // namespace

AlphaVantageResearchClient::AlphaVantageResearchClient(QObject* parent)
    : QObject(parent),
      network_(new QNetworkAccessManager(this)),
      apiKey_(qEnvironmentVariable("ALPHA_VANTAGE_API_KEY").trimmed()) {}

void AlphaVantageResearchClient::fetch(
    QString symbol,
    Callback callback) {
    cancel();
    symbol = normalizeWatchlistSymbol(std::move(symbol));
    if (!callback) {
        return;
    }
    const NamedWatchlist candidate{
        .id = QStringLiteral("research-validation"),
        .name = QStringLiteral("Research validation"),
        .entries = {{.symbol = symbol}},
    };
    if (!validateWatchlist(candidate).isEmpty()) {
        callback({
            .error = QStringLiteral("A valid research symbol is required."),
        });
        return;
    }
    if (apiKey_.isEmpty()) {
        callback({
            .error =
                QStringLiteral(
                    "ALPHA_VANTAGE_API_KEY is not set. Manual targets and "
                    "events remain available."),
        });
        return;
    }
    requestOverview(symbol, generation_, std::move(callback));
}

void AlphaVantageResearchClient::cancel() {
    ++generation_;
    if (activeReply_) {
        activeReply_->abort();
        activeReply_->deleteLater();
        activeReply_.clear();
    }
}

bool AlphaVantageResearchClient::hasApiKey() const noexcept {
    return !apiKey_.isEmpty();
}

void AlphaVantageResearchClient::requestOverview(
    const QString& symbol,
    const std::uint64_t requestGeneration,
    Callback callback) {
    auto* reply = network_->get(researchRequest(alphaVantageUrl(
        QStringLiteral("OVERVIEW"),
        symbol,
        apiKey_)));
    activeReply_ = reply;
    consumeBoundedNetworkReply(
        reply,
        kMaximumResearchPayloadBytes,
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
            auto error = responseError(response);
            if (!error.isEmpty()) {
                callback({.error = std::move(error)});
                return;
            }
            auto parsed = AlphaVantageResearchParser::parseOverview(
                response.payload,
                QDateTime::currentSecsSinceEpoch());
            if (!parsed.ok()) {
                callback({.error = std::move(parsed.error)});
                return;
            }
            if (normalizeWatchlistSymbol(parsed.snapshot.symbol) != symbol) {
                callback({
                    .error =
                        QStringLiteral(
                            "Alpha Vantage returned research for a different "
                            "symbol."),
                });
                return;
            }
            requestCalendar(
                symbol,
                requestGeneration,
                std::move(parsed.snapshot),
                std::move(parsed.corporateEvents),
                std::move(callback));
        });
}

void AlphaVantageResearchClient::requestCalendar(
    const QString& symbol,
    const std::uint64_t requestGeneration,
    CompanyResearchSnapshot snapshot,
    std::vector<ResearchEvent> events,
    Callback callback) {
    auto* reply = network_->get(researchRequest(alphaVantageUrl(
        QStringLiteral("EARNINGS_CALENDAR"),
        symbol,
        apiKey_)));
    activeReply_ = reply;
    consumeBoundedNetworkReply(
        reply,
        kMaximumResearchPayloadBytes,
        this,
        [this,
         reply,
         requestGeneration,
         snapshot = std::move(snapshot),
         events = std::move(events),
         callback = std::move(callback)](
            BoundedNetworkReplyResult response) mutable {
            if (requestGeneration != generation_) {
                return;
            }
            if (activeReply_ == reply) {
                activeReply_.clear();
            }
            auto warning = responseError(response);
            auto earningsCalendarUpdated = false;
            if (warning.isEmpty()) {
                auto parsed =
                    AlphaVantageResearchParser::parseEarningsCalendar(
                        response.payload,
                        QDateTime::currentSecsSinceEpoch());
                if (parsed.ok()) {
                    const auto expectedSymbol =
                        normalizeWatchlistSymbol(snapshot.symbol);
                    std::erase_if(
                        parsed.events,
                        [&](const ResearchEvent& event) {
                            return normalizeWatchlistSymbol(event.symbol) !=
                                   expectedSymbol;
                        });
                    events.insert(
                        events.end(),
                        std::make_move_iterator(parsed.events.begin()),
                        std::make_move_iterator(parsed.events.end()));
                    earningsCalendarUpdated = true;
                } else {
                    warning = std::move(parsed.error);
                }
            }
            callback({
                .snapshot = std::move(snapshot),
                .events = std::move(events),
                .earningsCalendarUpdated = earningsCalendarUpdated,
                .warning = std::move(warning),
            });
        });
}

} // namespace tvchart
