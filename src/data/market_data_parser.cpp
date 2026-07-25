#include "data/market_data_parser.hpp"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>

#include <algorithm>
#include <cmath>
#include <optional>

namespace tvchart {
namespace {

[[nodiscard]] std::optional<double> finiteNumber(const QJsonValue& value) {
    bool ok = false;
    double number{};
    if (value.isDouble()) {
        number = value.toDouble();
        ok = true;
    } else if (value.isString()) {
        number = value.toString().toDouble(&ok);
    }
    if (!ok || !std::isfinite(number)) {
        return std::nullopt;
    }
    return number;
}

[[nodiscard]] MarketDataParseResult jsonError(
    const QJsonParseError& parseError,
    const QString& provider) {
    return {
        .error = QStringLiteral("%1 returned invalid JSON: %2")
                     .arg(provider, parseError.errorString()),
    };
}

[[nodiscard]] MarketDataParseResult finish(Bars bars, const QString& provider) {
    std::ranges::sort(bars, {}, &Bar::timestamp);
    const auto duplicate =
        std::ranges::unique(bars, {}, &Bar::timestamp);
    bars.erase(duplicate.begin(), duplicate.end());

    if (bars.empty()) {
        return {.error = QStringLiteral("%1 returned no usable OHLCV bars.").arg(provider)};
    }
    if (const auto error = validateBars(bars)) {
        return {
            .error = QStringLiteral("%1 returned invalid bars: %2")
                         .arg(provider, QString::fromStdString(*error)),
        };
    }
    return {.bars = std::move(bars)};
}

[[nodiscard]] std::optional<Bar> yahooBar(
    const QJsonArray& timestamps,
    const QJsonArray& opens,
    const QJsonArray& highs,
    const QJsonArray& lows,
    const QJsonArray& closes,
    const QJsonArray& volumes,
    const qsizetype index) {
    const auto timestampValue = timestamps.at(index);
    if (!timestampValue.isDouble()) {
        return std::nullopt;
    }
    const auto open = finiteNumber(opens.at(index));
    const auto high = finiteNumber(highs.at(index));
    const auto low = finiteNumber(lows.at(index));
    const auto close = finiteNumber(closes.at(index));
    if (!open || !high || !low || !close) {
        return std::nullopt;
    }

    double volume{};
    if (index < volumes.size() && !volumes.at(index).isNull()) {
        const auto parsedVolume = finiteNumber(volumes.at(index));
        if (!parsedVolume) {
            return std::nullopt;
        }
        volume = *parsedVolume;
    }

    Bar bar{
        .timestamp = static_cast<std::int64_t>(timestampValue.toDouble()),
        .open = *open,
        .high = *high,
        .low = *low,
        .close = *close,
        .volume = volume,
    };
    if (validateBar(bar)) {
        return std::nullopt;
    }
    return bar;
}

[[nodiscard]] std::optional<std::int64_t> twelveTimestamp(const QString& text) {
    auto isoText = text.trimmed();
    isoText.replace(u' ', u'T');
    if (!isoText.endsWith(u'Z')) {
        isoText.append(u'Z');
    }
    const auto timestamp = QDateTime::fromString(isoText, Qt::ISODate);
    if (!timestamp.isValid()) {
        return std::nullopt;
    }
    return timestamp.toSecsSinceEpoch();
}

} // namespace

MarketDataParseResult MarketDataParser::parseYahoo(const QByteArray& payload) {
    QJsonParseError parseError;
    const auto document = QJsonDocument::fromJson(payload, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        return jsonError(parseError, QStringLiteral("Yahoo Finance"));
    }

    const auto chart = document.object().value(QStringLiteral("chart")).toObject();
    const auto apiError = chart.value(QStringLiteral("error"));
    if (!apiError.isNull() && apiError.isObject()) {
        const auto errorObject = apiError.toObject();
        return {
            .error = QStringLiteral("Yahoo Finance error: %1")
                         .arg(errorObject.value(QStringLiteral("description")).toString(
                             errorObject.value(QStringLiteral("code")).toString())),
        };
    }

    const auto results = chart.value(QStringLiteral("result")).toArray();
    if (results.isEmpty()) {
        return {.error = QStringLiteral("Yahoo Finance returned no chart result.")};
    }
    const auto result = results.at(0).toObject();
    const auto timestamps = result.value(QStringLiteral("timestamp")).toArray();
    const auto quotes =
        result.value(QStringLiteral("indicators"))
            .toObject()
            .value(QStringLiteral("quote"))
            .toArray();
    if (timestamps.isEmpty() || quotes.isEmpty()) {
        return {.error = QStringLiteral("Yahoo Finance returned no quote arrays.")};
    }

    const auto quote = quotes.at(0).toObject();
    const auto opens = quote.value(QStringLiteral("open")).toArray();
    const auto highs = quote.value(QStringLiteral("high")).toArray();
    const auto lows = quote.value(QStringLiteral("low")).toArray();
    const auto closes = quote.value(QStringLiteral("close")).toArray();
    const auto volumes = quote.value(QStringLiteral("volume")).toArray();
    const auto count =
        std::min({timestamps.size(), opens.size(), highs.size(), lows.size(), closes.size()});

    Bars bars;
    bars.reserve(static_cast<std::size_t>(count));
    for (qsizetype index = 0; index < count; ++index) {
        if (auto bar = yahooBar(
                timestamps, opens, highs, lows, closes, volumes, index)) {
            bars.push_back(*bar);
        }
    }
    return finish(std::move(bars), QStringLiteral("Yahoo Finance"));
}

MarketDataParseResult MarketDataParser::parseTwelveData(const QByteArray& payload) {
    QJsonParseError parseError;
    const auto document = QJsonDocument::fromJson(payload, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        return jsonError(parseError, QStringLiteral("Twelve Data"));
    }

    const auto root = document.object();
    if (root.value(QStringLiteral("status")).toString() == QStringLiteral("error")) {
        return {
            .error = QStringLiteral("Twelve Data error: %1")
                         .arg(root.value(QStringLiteral("message")).toString(
                             QStringLiteral("unknown provider error"))),
        };
    }

    const auto values = root.value(QStringLiteral("values")).toArray();
    Bars bars;
    bars.reserve(static_cast<std::size_t>(values.size()));
    for (const auto& value : values) {
        const auto object = value.toObject();
        const auto timestamp =
            twelveTimestamp(object.value(QStringLiteral("datetime")).toString());
        const auto open = finiteNumber(object.value(QStringLiteral("open")));
        const auto high = finiteNumber(object.value(QStringLiteral("high")));
        const auto low = finiteNumber(object.value(QStringLiteral("low")));
        const auto close = finiteNumber(object.value(QStringLiteral("close")));
        if (!timestamp || !open || !high || !low || !close) {
            continue;
        }

        double volume{};
        const auto volumeValue = object.value(QStringLiteral("volume"));
        if (!volumeValue.isUndefined() && !volumeValue.isNull()) {
            const auto parsedVolume = finiteNumber(volumeValue);
            if (!parsedVolume) {
                continue;
            }
            volume = *parsedVolume;
        }

        Bar bar{
            .timestamp = *timestamp,
            .open = *open,
            .high = *high,
            .low = *low,
            .close = *close,
            .volume = volume,
        };
        if (!validateBar(bar)) {
            bars.push_back(bar);
        }
    }
    return finish(std::move(bars), QStringLiteral("Twelve Data"));
}

} // namespace tvchart
