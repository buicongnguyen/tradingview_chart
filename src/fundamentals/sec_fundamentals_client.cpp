#include "fundamentals/sec_fundamentals_client.hpp"

#include "fundamentals/sec_companyfacts_parser.hpp"
#include "network/bounded_network_reply.hpp"
#include "research/event_intelligence_parser.hpp"
#include "watchlists/watchlist_workspace.hpp"

#include <QDateTime>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>

#include <utility>

namespace tvchart {
namespace {

constexpr auto kMaximumTickerPayload = qsizetype{12 * 1024 * 1024};
constexpr auto kMaximumCompanyFactsPayload =
    qsizetype{32 * 1024 * 1024};

[[nodiscard]] QNetworkRequest secRequest(
    const QUrl& url,
    const QString& userAgent) {
    QNetworkRequest request(url);
    request.setHeader(
        QNetworkRequest::UserAgentHeader,
        userAgent);
    request.setRawHeader("Accept", "application/json");
    request.setTransferTimeout(25'000);
    request.setAttribute(
        QNetworkRequest::RedirectPolicyAttribute,
        QNetworkRequest::NoLessSafeRedirectPolicy);
    return request;
}

[[nodiscard]] QString responseError(
    const BoundedNetworkReplyResult& response,
    const qsizetype maximumBytes) {
    if (response.limitExceeded) {
        return QStringLiteral(
                   "SEC response exceeded the %1 MiB safety limit.")
            .arg(maximumBytes / (1024 * 1024));
    }
    if (!response.transportError.isEmpty()) {
        return QStringLiteral("SEC request failed: %1")
            .arg(response.transportError);
    }
    if (response.httpStatus < 200 ||
        response.httpStatus >= 300) {
        return QStringLiteral("SEC returned HTTP %1.")
            .arg(response.httpStatus);
    }
    return {};
}

} // namespace

SecFundamentalsClient::SecFundamentalsClient(QObject* parent)
    : QObject(parent),
      network_(new QNetworkAccessManager(this)),
      secUserAgent_(
          qEnvironmentVariable("SEC_USER_AGENT").trimmed()) {}

void SecFundamentalsClient::fetch(
    QString symbol,
    Callback callback) {
    cancel();
    if (!callback) {
        return;
    }
    symbol = normalizeWatchlistSymbol(std::move(symbol));
    if (symbol.isEmpty()) {
        callback({
            .error = QStringLiteral(
                "A valid SEC ticker is required."),
        });
        return;
    }
    if (!hasSecUserAgent()) {
        callback({
            .error = QStringLiteral(
                "SEC_USER_AGENT is required and must identify the "
                "application with a contact email."),
        });
        return;
    }
    requestTickerMap(
        symbol,
        generation_,
        std::move(callback));
}

void SecFundamentalsClient::cancel() {
    ++generation_;
    if (activeReply_) {
        activeReply_->abort();
        activeReply_->deleteLater();
        activeReply_.clear();
    }
}

bool SecFundamentalsClient::hasSecUserAgent() const noexcept {
    return secUserAgent_.size() <= 200 &&
           secUserAgent_.contains(u'@') &&
           !secUserAgent_.contains(u'\r') &&
           !secUserAgent_.contains(u'\n');
}

void SecFundamentalsClient::requestTickerMap(
    const QString& symbol,
    const std::uint64_t requestGeneration,
    Callback callback) {
    auto* reply = network_->get(secRequest(
        QUrl(QStringLiteral(
            "https://www.sec.gov/files/company_tickers.json")),
        secUserAgent_));
    activeReply_ = reply;
    consumeBoundedNetworkReply(
        reply,
        kMaximumTickerPayload,
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
            if (auto error = responseError(
                    response,
                    kMaximumTickerPayload);
                !error.isEmpty()) {
                callback({.error = std::move(error)});
                return;
            }
            auto lookup =
                EventIntelligenceParser::parseSecTickerMap(
                    response.payload,
                    symbol);
            if (!lookup.ok()) {
                callback({.error = std::move(lookup.error)});
                return;
            }
            requestCompanyFacts(
                symbol,
                lookup.cik,
                requestGeneration,
                std::move(callback));
        });
}

void SecFundamentalsClient::requestCompanyFacts(
    const QString& symbol,
    const QString& cik,
    const std::uint64_t requestGeneration,
    Callback callback) {
    auto* reply = network_->get(secRequest(
        QUrl(QStringLiteral(
                 "https://data.sec.gov/api/xbrl/companyfacts/CIK%1.json")
                 .arg(cik)),
        secUserAgent_));
    activeReply_ = reply;
    consumeBoundedNetworkReply(
        reply,
        kMaximumCompanyFactsPayload,
        this,
        [this,
         reply,
         symbol,
         cik,
         requestGeneration,
         callback = std::move(callback)](
            BoundedNetworkReplyResult response) mutable {
            if (requestGeneration != generation_) {
                return;
            }
            if (activeReply_ == reply) {
                activeReply_.clear();
            }
            if (auto error = responseError(
                    response,
                    kMaximumCompanyFactsPayload);
                !error.isEmpty()) {
                callback({.error = std::move(error)});
                return;
            }
            auto parsed = SecCompanyFactsParser::parse(
                response.payload,
                symbol,
                cik,
                QDateTime::currentSecsSinceEpoch());
            callback({
                .company = std::move(parsed.company),
                .rejectedFacts = parsed.rejectedFacts,
                .error = std::move(parsed.error),
            });
        });
}

} // namespace tvchart
