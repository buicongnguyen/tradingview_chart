#include "network/bounded_network_reply.hpp"

#include <QNetworkReply>
#include <QNetworkRequest>
#include <QObject>

#include <algorithm>
#include <memory>
#include <utility>

namespace tvchart {
namespace {

struct ReplyState {
    QByteArray payload;
    bool limitExceeded{};
    bool completed{};
};

void readAvailable(
    QNetworkReply* reply,
    const qsizetype maxBytes,
    ReplyState& state) {
    constexpr auto chunkSize = qsizetype{64 * 1024};
    while (!state.limitExceeded && reply->bytesAvailable() > 0) {
        const auto remaining = maxBytes - state.payload.size();
        if (remaining <= 0) {
            state.limitExceeded = true;
            reply->abort();
            return;
        }
        const auto requested =
            std::min({reply->bytesAvailable(), remaining + 1, chunkSize});
        const auto chunk = reply->read(requested);
        if (chunk.isEmpty()) {
            return;
        }
        if (chunk.size() > remaining) {
            state.payload.append(chunk.constData(), remaining);
            state.limitExceeded = true;
            reply->abort();
            return;
        }
        state.payload.append(chunk);
    }
}

} // namespace

void consumeBoundedNetworkReply(
    QNetworkReply* reply,
    const qsizetype maxBytes,
    QObject* context,
    BoundedNetworkReplyCallback callback) {
    Q_ASSERT(reply);
    Q_ASSERT(context);
    Q_ASSERT(maxBytes > 0);

    auto state = std::make_shared<ReplyState>();
    state->payload.reserve(std::min(maxBytes, qsizetype{256 * 1024}));

    QObject::connect(
        reply,
        &QNetworkReply::readyRead,
        context,
        [reply, maxBytes, state] {
            readAvailable(reply, maxBytes, *state);
        });
    QObject::connect(
        reply,
        &QNetworkReply::finished,
        context,
        [reply,
         maxBytes,
         state,
         callback = std::move(callback)]() mutable {
            if (state->completed) {
                return;
            }
            state->completed = true;
            readAvailable(reply, maxBytes, *state);

            BoundedNetworkReplyResult result{
                .httpStatus =
                    reply->attribute(
                             QNetworkRequest::HttpStatusCodeAttribute)
                        .toInt(),
                .payload = std::move(state->payload),
                .limitExceeded = state->limitExceeded,
            };
            if (!result.limitExceeded &&
                reply->error() != QNetworkReply::NoError) {
                result.transportError = reply->errorString();
            }
            reply->deleteLater();
            callback(std::move(result));
        });
}

} // namespace tvchart
