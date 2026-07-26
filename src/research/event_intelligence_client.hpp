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

struct EventIntelligenceResult {
    QString provider;
    QString symbol;
    std::vector<ResearchEvent> events;
    QString error;

    [[nodiscard]] bool ok() const noexcept { return error.isEmpty(); }
};

class EventIntelligenceClient final : public QObject {
    Q_OBJECT

public:
    using Callback = std::function<void(EventIntelligenceResult)>;

    explicit EventIntelligenceClient(QObject* parent = nullptr);

    void fetchSecFilings(QString symbol, QString knownCik, Callback callback);
    void fetchFredCalendar(Callback callback);
    void cancel();

    [[nodiscard]] bool hasSecUserAgent() const noexcept;
    [[nodiscard]] bool hasFredApiKey() const noexcept;

private:
    void requestSecTickerMap(
        const QString& symbol,
        std::uint64_t generation,
        Callback callback);
    void requestSecSubmissions(
        const QString& symbol,
        const QString& normalizedCik,
        std::uint64_t generation,
        Callback callback);

    QNetworkAccessManager* network_{};
    QPointer<QNetworkReply> activeReply_;
    QString secUserAgent_;
    QString fredApiKey_;
    std::uint64_t generation_{};
};

} // namespace tvchart
