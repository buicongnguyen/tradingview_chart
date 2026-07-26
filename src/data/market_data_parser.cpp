#include "data/market_data_parser.hpp"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QTimeZone>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <optional>
#include <ranges>
#include <utility>
#include <vector>

namespace tvchart {
namespace {

constexpr auto kMaximumUnixTimestamp = std::int64_t{253'402'300'799};
constexpr auto kMaximumCorporateActions = std::size_t{10'000};

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

[[nodiscard]] std::optional<std::int64_t> unixTimestamp(const QJsonValue& value) {
    const auto number = finiteNumber(value);
    if (!number || *number < 1.0 ||
        *number > static_cast<double>(kMaximumUnixTimestamp) ||
        std::trunc(*number) != *number) {
        return std::nullopt;
    }
    return static_cast<std::int64_t>(*number);
}

[[nodiscard]] MarketDataParseResult jsonError(
    const QJsonParseError& parseError,
    const QString& provider) {
    return {
        .error = QStringLiteral("%1 returned invalid JSON: %2")
                     .arg(provider, parseError.errorString()),
    };
}

[[nodiscard]] MarketDataParseResult finish(
    MarketDataParseResult parsed,
    const QString& provider) {
    std::stable_sort(
        parsed.bars.begin(),
        parsed.bars.end(),
        [](const Bar& left, const Bar& right) {
            return left.timestamp < right.timestamp;
        });

    Bars normalized;
    normalized.reserve(parsed.bars.size());
    for (const auto& bar : parsed.bars) {
        if (!normalized.empty() &&
            normalized.back().timestamp == bar.timestamp) {
            if (normalized.back() != bar) {
                return {
                    .error =
                        QStringLiteral(
                            "%1 returned conflicting bars for timestamp %2.")
                            .arg(provider)
                            .arg(bar.timestamp),
                };
            }
            ++parsed.duplicateRows;
            continue;
        }
        normalized.push_back(bar);
    }
    parsed.bars = std::move(normalized);

    if (parsed.bars.empty()) {
        return {.error = QStringLiteral("%1 returned no usable OHLCV bars.").arg(provider)};
    }
    if (const auto error = validateBars(parsed.bars)) {
        return {
            .error = QStringLiteral("%1 returned invalid bars: %2")
                         .arg(provider, QString::fromStdString(*error)),
        };
    }
    return parsed;
}

[[nodiscard]] std::optional<Bar> yahooBar(
    const QJsonArray& timestamps,
    const QJsonArray& opens,
    const QJsonArray& highs,
    const QJsonArray& lows,
    const QJsonArray& closes,
    const QJsonArray& volumes,
    const qsizetype index) {
    const auto timestamp = unixTimestamp(timestamps.at(index));
    if (!timestamp) {
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
        .timestamp = *timestamp,
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

[[nodiscard]] std::optional<std::int64_t> twelveTimestamp(
    const QString& text,
    const QTimeZone& defaultTimeZone) {
    auto isoText = text.trimmed();
    isoText.replace(u' ', u'T');
    auto dateTime = QDateTime::fromString(isoText, Qt::ISODateWithMs);
    if (!dateTime.isValid()) {
        dateTime = QDateTime::fromString(isoText, Qt::ISODate);
    }
    if (!dateTime.isValid()) {
        return std::nullopt;
    }
    if (dateTime.timeSpec() == Qt::LocalTime) {
        dateTime = QDateTime(dateTime.date(), dateTime.time(), defaultTimeZone);
    }
    const auto timestamp = dateTime.toSecsSinceEpoch();
    if (timestamp <= 0 || timestamp > kMaximumUnixTimestamp) {
        return std::nullopt;
    }
    return timestamp;
}

[[nodiscard]] std::optional<std::pair<double, double>> splitRatio(
    const QJsonObject& object) {
    const auto numerator = finiteNumber(
        object.value(QStringLiteral("numerator")));
    const auto denominator = finiteNumber(
        object.value(QStringLiteral("denominator")));
    if (numerator && denominator) {
        return std::pair{*numerator, *denominator};
    }
    const auto pieces =
        object.value(QStringLiteral("splitRatio"))
            .toString()
            .split(u':');
    if (pieces.size() != 2) {
        return std::nullopt;
    }
    bool numeratorOk = false;
    bool denominatorOk = false;
    const auto parsedNumerator =
        pieces.at(0).trimmed().toDouble(&numeratorOk);
    const auto parsedDenominator =
        pieces.at(1).trimmed().toDouble(&denominatorOk);
    if (!numeratorOk || !denominatorOk) {
        return std::nullopt;
    }
    return std::pair{parsedNumerator, parsedDenominator};
}

[[nodiscard]] QString parseYahooActions(
    const QJsonObject& events,
    std::vector<CorporateAction>& actions) {
    const auto append =
        [&](const QJsonObject& collection,
            const CorporateActionType type) -> QString {
        for (auto iterator = collection.begin();
             iterator != collection.end();
             ++iterator) {
            if (actions.size() >= kMaximumCorporateActions) {
                return QStringLiteral(
                    "Yahoo Finance returned too many corporate actions.");
            }
            if (!iterator.value().isObject()) {
                continue;
            }
            const auto object = iterator.value().toObject();
            const auto timestamp = unixTimestamp(
                object.value(QStringLiteral("date")));
            if (!timestamp) {
                continue;
            }
            CorporateAction action{
                .type = type,
                .timestamp = *timestamp,
                .currency =
                    object.value(QStringLiteral("currency")).toString(),
                .provider = QStringLiteral("Yahoo Finance"),
            };
            if (type == CorporateActionType::CashDividend) {
                const auto amount = finiteNumber(
                    object.value(QStringLiteral("amount")));
                if (!amount) {
                    continue;
                }
                action.amount = *amount;
            } else {
                const auto ratio = splitRatio(object);
                if (!ratio) {
                    continue;
                }
                action.numerator = ratio->first;
                action.denominator = ratio->second;
            }
            if (validateCorporateAction(action).isEmpty()) {
                actions.push_back(std::move(action));
            }
        }
        return {};
    };
    if (const auto error = append(
            events.value(QStringLiteral("dividends")).toObject(),
            CorporateActionType::CashDividend);
        !error.isEmpty()) {
        return error;
    }
    if (const auto error = append(
            events.value(QStringLiteral("splits")).toObject(),
            CorporateActionType::StockSplit);
        !error.isEmpty()) {
        return error;
    }
    std::ranges::sort(
        actions,
        [](const CorporateAction& left, const CorporateAction& right) {
            if (left.timestamp != right.timestamp) {
                return left.timestamp < right.timestamp;
            }
            return left.type < right.type;
        });
    actions.erase(
        std::unique(actions.begin(), actions.end()),
        actions.end());
    return {};
}

} // namespace

MarketDataParseResult MarketDataParser::parseYahoo(const QByteArray& payload) {
    QJsonParseError parseError;
    const auto document = QJsonDocument::fromJson(payload, &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        return jsonError(parseError, QStringLiteral("Yahoo Finance"));
    }
    if (!document.isObject()) {
        return {
            .error = QStringLiteral(
                "Yahoo Finance returned an unexpected top-level JSON value."),
        };
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
    const auto meta = result.value(QStringLiteral("meta")).toObject();
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
    const auto adjustedCollections =
        result.value(QStringLiteral("indicators"))
            .toObject()
            .value(QStringLiteral("adjclose"))
            .toArray();
    const auto adjusted =
        adjustedCollections.isEmpty()
            ? QJsonArray{}
            : adjustedCollections.at(0)
                  .toObject()
                  .value(QStringLiteral("adjclose"))
                  .toArray();

    MarketDataParseResult parsed{
        .inputRows = static_cast<std::size_t>(timestamps.size()),
    };
    parsed.bars.reserve(static_cast<std::size_t>(timestamps.size()));
    parsed.adjustedCloses.reserve(
        static_cast<std::size_t>(timestamps.size()));
    for (qsizetype index = 0; index < timestamps.size(); ++index) {
        if (auto bar = yahooBar(
                timestamps, opens, highs, lows, closes, volumes, index)) {
            parsed.bars.push_back(*bar);
            if (index < adjusted.size()) {
                const auto adjustedClose =
                    finiteNumber(adjusted.at(index));
                if (adjustedClose && *adjustedClose > 0.0) {
                    parsed.adjustedCloses.push_back({
                        .timestamp = bar->timestamp,
                        .adjustedClose = *adjustedClose,
                    });
                }
            }
        } else {
            ++parsed.rejectedRows;
        }
    }
    if (const auto error = parseYahooActions(
            result.value(QStringLiteral("events")).toObject(),
            parsed.corporateActions);
        !error.isEmpty()) {
        return {.error = error};
    }
    parsed = finish(std::move(parsed), QStringLiteral("Yahoo Finance"));
    if (!parsed.ok()) {
        return parsed;
    }
    parsed.metadata.exchange =
        meta.value(QStringLiteral("fullExchangeName")).toString(
            meta.value(QStringLiteral("exchangeName")).toString());
    parsed.metadata.currency =
        meta.value(QStringLiteral("currency")).toString();
    parsed.metadata.timezone =
        meta.value(QStringLiteral("exchangeTimezoneName")).toString();
    parsed.metadata.instrumentType =
        meta.value(QStringLiteral("instrumentType")).toString();
    parsed.metadata.interval =
        meta.value(QStringLiteral("dataGranularity")).toString();
    const auto delay = finiteNumber(
        meta.value(QStringLiteral("exchangeDataDelayedBy")));
    if (delay && *delay >= 0.0 && *delay <= 24.0 * 60.0 &&
        std::trunc(*delay) == *delay) {
        parsed.metadata.exchangeDelayMinutes = static_cast<int>(*delay);
    }
    return parsed;
}

MarketDataParseResult MarketDataParser::parseTwelveData(const QByteArray& payload) {
    QJsonParseError parseError;
    const auto document = QJsonDocument::fromJson(payload, &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        return jsonError(parseError, QStringLiteral("Twelve Data"));
    }
    if (!document.isObject()) {
        return {
            .error = QStringLiteral(
                "Twelve Data returned an unexpected top-level JSON value."),
        };
    }

    const auto root = document.object();
    if (root.value(QStringLiteral("status")).toString() == QStringLiteral("error")) {
        return {
            .error = QStringLiteral("Twelve Data error: %1")
                         .arg(root.value(QStringLiteral("message")).toString(
                             QStringLiteral("unknown provider error"))),
        };
    }

    const auto meta = root.value(QStringLiteral("meta")).toObject();
    auto timeZone = QTimeZone::utc();
    auto timeZoneId =
        meta.value(QStringLiteral("timezone")).toString().trimmed();
    if (timeZoneId.isEmpty()) {
        timeZoneId =
            meta.value(QStringLiteral("exchange_timezone"))
                .toString()
                .trimmed();
    }
    if (!timeZoneId.isEmpty()) {
        timeZone = QTimeZone(timeZoneId.toUtf8());
        if (!timeZone.isValid()) {
            return {
                .error = QStringLiteral("Twelve Data returned an unsupported timezone: %1")
                             .arg(timeZoneId),
            };
        }
    }

    const auto values = root.value(QStringLiteral("values")).toArray();
    MarketDataParseResult parsed{
        .inputRows = static_cast<std::size_t>(values.size()),
    };
    parsed.bars.reserve(static_cast<std::size_t>(values.size()));
    for (const auto& value : values) {
        const auto object = value.toObject();
        const auto timestamp =
            twelveTimestamp(
                object.value(QStringLiteral("datetime")).toString(),
                timeZone);
        const auto open = finiteNumber(object.value(QStringLiteral("open")));
        const auto high = finiteNumber(object.value(QStringLiteral("high")));
        const auto low = finiteNumber(object.value(QStringLiteral("low")));
        const auto close = finiteNumber(object.value(QStringLiteral("close")));
        if (!timestamp || !open || !high || !low || !close) {
            ++parsed.rejectedRows;
            continue;
        }

        double volume{};
        const auto volumeValue = object.value(QStringLiteral("volume"));
        if (!volumeValue.isUndefined() && !volumeValue.isNull()) {
            const auto parsedVolume = finiteNumber(volumeValue);
            if (!parsedVolume) {
                ++parsed.rejectedRows;
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
            parsed.bars.push_back(bar);
        } else {
            ++parsed.rejectedRows;
        }
    }
    parsed = finish(std::move(parsed), QStringLiteral("Twelve Data"));
    if (!parsed.ok()) {
        return parsed;
    }
    parsed.metadata.exchange =
        meta.value(QStringLiteral("exchange")).toString();
    parsed.metadata.currency =
        meta.value(QStringLiteral("currency")).toString();
    parsed.metadata.timezone =
        meta.value(QStringLiteral("exchange_timezone")).toString(
            meta.value(QStringLiteral("timezone")).toString());
    parsed.metadata.instrumentType =
        meta.value(QStringLiteral("type")).toString();
    parsed.metadata.interval =
        meta.value(QStringLiteral("interval")).toString();
    return parsed;
}

} // namespace tvchart
