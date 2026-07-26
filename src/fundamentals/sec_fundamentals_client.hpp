#pragma once

#include "fundamentals/fundamental_models.hpp"

#include <QObject>
#include <QPointer>
#include <QString>

#include <cstddef>
#include <cstdint>
#include <functional>

class QNetworkAccessManager;
class QNetworkReply;

namespace tvchart {

struct SecFundamentalsResult {
    FundamentalCompany company;
    std::size_t rejectedFacts{};
    QString error;

    [[nodiscard]] bool ok() const noexcept {
        return error.isEmpty() && !company.facts.empty();
    }
};

class SecFundamentalsClient final : public QObject {
    Q_OBJECT

public:
    using Callback = std::function<void(SecFundamentalsResult)>;

    explicit SecFundamentalsClient(QObject* parent = nullptr);

    void fetch(QString symbol, Callback callback);
    void cancel();

    [[nodiscard]] bool hasSecUserAgent() const noexcept;

private:
    void requestTickerMap(
        const QString& symbol,
        std::uint64_t generation,
        Callback callback);
    void requestCompanyFacts(
        const QString& symbol,
        const QString& cik,
        std::uint64_t generation,
        Callback callback);

    QNetworkAccessManager* network_{};
    QPointer<QNetworkReply> activeReply_;
    QString secUserAgent_;
    std::uint64_t generation_{};
};

} // namespace tvchart
