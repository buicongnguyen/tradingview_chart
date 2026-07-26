#pragma once

#include <QByteArray>
#include <QString>

#include <functional>

class QObject;
class QNetworkReply;

namespace tvchart {

struct BoundedNetworkReplyResult {
    int httpStatus{};
    QByteArray payload;
    QString transportError;
    bool limitExceeded{};
};

using BoundedNetworkReplyCallback =
    std::function<void(BoundedNetworkReplyResult)>;

void consumeBoundedNetworkReply(
    QNetworkReply* reply,
    qsizetype maxBytes,
    QObject* context,
    BoundedNetworkReplyCallback callback);

} // namespace tvchart
