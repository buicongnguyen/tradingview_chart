#include "data/android_http_client.hpp"

#include <QAtomicInteger>
#include <QHash>
#include <QJniEnvironment>
#include <QJniObject>
#include <QMetaObject>
#include <QMutex>
#include <QMutexLocker>
#include <QPointer>

#include <jni.h>

#include <utility>

namespace tvchart {
namespace {

struct PendingRequest {
    QPointer<AndroidHttpClient> owner;
    AndroidHttpClient::Callback callback;
};

QMutex pendingMutex;
QHash<quint64, PendingRequest> pendingRequests;
QAtomicInteger<quint64> nextRequestToken{1};

[[nodiscard]] PendingRequest takePendingRequest(const quint64 token) {
    const QMutexLocker lock(&pendingMutex);
    return pendingRequests.take(token);
}

void completeRequest(
    const quint64 token,
    const int status,
    QByteArray payload,
    QString error) {
    auto pending = takePendingRequest(token);
    if (!pending.owner || !pending.callback) {
        return;
    }
    const auto owner = pending.owner;
    QMetaObject::invokeMethod(
        owner,
        [
            owner,
            callback = std::move(pending.callback),
            status,
            payload = std::move(payload),
            error = std::move(error)
        ]() mutable {
            if (owner) {
                callback(status, std::move(payload), std::move(error));
            }
        },
        Qt::QueuedConnection);
}

} // namespace

void deliverAndroidHttpResult(
    const quint64 token,
    const int status,
    QByteArray payload,
    QString error) {
    completeRequest(token, status, std::move(payload), std::move(error));
}

AndroidHttpClient::AndroidHttpClient(QObject* parent)
    : QObject(parent) {}

AndroidHttpClient::~AndroidHttpClient() {
    const QMutexLocker lock(&pendingMutex);
    for (auto iterator = pendingRequests.begin();
         iterator != pendingRequests.end();) {
        if (iterator->owner == this) {
            iterator = pendingRequests.erase(iterator);
        } else {
            ++iterator;
        }
    }
}

quint64 AndroidHttpClient::get(
    const QUrl& url,
    const qsizetype maximumBytes,
    Callback callback) {
    if (!callback) {
        return 0;
    }
    if (url.scheme() != QStringLiteral("https") || !url.isValid()) {
        QMetaObject::invokeMethod(
            this,
            [callback = std::move(callback)]() mutable {
                callback(0, {}, QStringLiteral("Only valid HTTPS URLs are allowed."));
            },
            Qt::QueuedConnection);
        return 0;
    }

    const auto token = nextRequestToken.fetchAndAddRelaxed(1);
    {
        const QMutexLocker lock(&pendingMutex);
        pendingRequests.insert(token, PendingRequest{this, std::move(callback)});
    }

    const auto javaUrl = QJniObject::fromString(url.toString(QUrl::FullyEncoded));
    const auto userAgent = QJniObject::fromString(
        QStringLiteral(
            "TradeChartLab/" TRADINGVIEW_CHART_VERSION " (Qt 6; Android)"));
    QJniObject::callStaticMethod<void>(
        "com/buicongnguyen/tradingviewchart/AndroidHttpClient",
        "get",
        "(JLjava/lang/String;Ljava/lang/String;I)V",
        static_cast<jlong>(token),
        javaUrl.object<jstring>(),
        userAgent.object<jstring>(),
        static_cast<jint>(maximumBytes));

    QJniEnvironment environment;
    if (environment.checkAndClearExceptions()) {
        auto pending = takePendingRequest(token);
        QMetaObject::invokeMethod(
            this,
            [callback = std::move(pending.callback)]() mutable {
                if (callback) {
                    callback(
                        0,
                        {},
                        QStringLiteral("Could not start Android HTTPS request."));
                }
            },
            Qt::QueuedConnection);
        return 0;
    }
    return token;
}

void AndroidHttpClient::cancel(const quint64 requestToken) {
    if (requestToken == 0) {
        return;
    }
    (void)takePendingRequest(requestToken);
}

} // namespace tvchart

extern "C" JNIEXPORT void JNICALL
Java_com_buicongnguyen_tradingviewchart_AndroidHttpClient_nativeResult(
    JNIEnv* environment,
    jclass,
    const jlong requestToken,
    const jint httpStatus,
    jbyteArray payload,
    jstring error) {
    QByteArray bytes;
    if (payload != nullptr) {
        const auto length = environment->GetArrayLength(payload);
        bytes.resize(length);
        if (length > 0) {
            environment->GetByteArrayRegion(
                payload,
                0,
                length,
                reinterpret_cast<jbyte*>(bytes.data()));
        }
    }
    const auto errorMessage =
        error == nullptr ? QString() : QJniObject(error).toString();
    tvchart::deliverAndroidHttpResult(
        static_cast<quint64>(requestToken),
        static_cast<int>(httpStatus),
        std::move(bytes),
        errorMessage);
}
