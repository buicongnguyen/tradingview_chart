#pragma once

#include "research/research_models.hpp"

#include <QObject>
#include <QPointer>
#include <QString>

#include <cstdint>
#include <functional>
#include <vector>

class QNetworkAccessManager;
class QNetworkReply;

namespace tvchart {

struct AlphaVantageResearchResult {
    CompanyResearchSnapshot snapshot;
    std::vector<ResearchEvent> events;
    bool earningsCalendarUpdated{};
    QString warning;
    QString error;

    [[nodiscard]] bool ok() const noexcept { return error.isEmpty(); }
};

class AlphaVantageResearchClient final : public QObject {
    Q_OBJECT

public:
    using Callback = std::function<void(AlphaVantageResearchResult)>;

    explicit AlphaVantageResearchClient(QObject* parent = nullptr);

    void fetch(QString symbol, Callback callback);
    void cancel();
    [[nodiscard]] bool hasApiKey() const noexcept;

private:
    void requestOverview(
        const QString& symbol,
        std::uint64_t generation,
        Callback callback);
    void requestCalendar(
        const QString& symbol,
        std::uint64_t generation,
        CompanyResearchSnapshot snapshot,
        std::vector<ResearchEvent> events,
        Callback callback);

    QNetworkAccessManager* network_{};
    QPointer<QNetworkReply> activeReply_;
    QString apiKey_;
    std::uint64_t generation_{};
};

} // namespace tvchart
