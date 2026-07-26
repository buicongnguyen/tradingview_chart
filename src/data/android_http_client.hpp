#pragma once

#include <QByteArray>
#include <QObject>
#include <QString>
#include <QUrl>

#include <functional>

namespace tvchart {

class AndroidHttpClient final : public QObject {
    Q_OBJECT

public:
    using Callback = std::function<void(int, QByteArray, QString)>;

    explicit AndroidHttpClient(QObject* parent = nullptr);
    ~AndroidHttpClient() override;

    [[nodiscard]] quint64 get(
        const QUrl& url,
        qsizetype maximumBytes,
        Callback callback);
    void cancel(quint64 requestToken);
};

} // namespace tvchart
