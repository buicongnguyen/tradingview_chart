#include "mobile/mobile_controller.hpp"

#include "analysis/technical_indicators.hpp"
#include "chart/chart_bridge.hpp"
#include "data/demo_data_source.hpp"
#include "data/market_data_client.hpp"

#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJniObject>
#include <QLocale>

#include <algorithm>
#include <array>
#include <cmath>
#include <utility>

namespace tvchart {
namespace {

[[nodiscard]] QString formatPrice(const double value) {
    const auto decimals = std::abs(value) < 1.0 ? 4 : 2;
    return QLocale().toString(value, 'f', decimals);
}

[[nodiscard]] QString formatVolume(const double value) {
    const auto locale = QLocale();
    if (value >= 1'000'000'000.0) {
        return locale.toString(value / 1'000'000'000.0, 'f', 2) +
               QStringLiteral("B");
    }
    if (value >= 1'000'000.0) {
        return locale.toString(value / 1'000'000.0, 'f', 2) +
               QStringLiteral("M");
    }
    if (value >= 1'000.0) {
        return locale.toString(value / 1'000.0, 'f', 1) + QStringLiteral("K");
    }
    return locale.toString(value, 'f', 0);
}

[[nodiscard]] IndicatorKind indicatorFromIndex(const int index) noexcept {
    constexpr std::array kinds{
        IndicatorKind::None,
        IndicatorKind::SimpleMovingAverage,
        IndicatorKind::ExponentialMovingAverage,
        IndicatorKind::VolumeWeightedAveragePrice,
        IndicatorKind::RelativeStrengthIndex,
        IndicatorKind::MovingAverageConvergenceDivergence,
        IndicatorKind::RollingHigh,
        IndicatorKind::RollingLow,
        IndicatorKind::VolumeSimpleMovingAverage,
    };
    if (index < 0 || static_cast<std::size_t>(index) >= kinds.size()) {
        return IndicatorKind::None;
    }
    return kinds[static_cast<std::size_t>(index)];
}

} // namespace

MobileController::MobileController(QObject* parent)
    : QObject(parent),
      chartBridge_(new ChartBridge(this)),
      marketDataClient_(new MarketDataClient(this)) {
    topSystemInset_ = QJniObject::callStaticMethod<jfloat>(
        "com/buicongnguyen/tradingviewchart/AndroidUiMetrics",
        "statusBarHeightDp",
        "()F");
    bottomSystemInset_ = QJniObject::callStaticMethod<jfloat>(
        "com/buicongnguyen/tradingviewchart/AndroidUiMetrics",
        "navigationBarHeightDp",
        "()F");
    connect(
        chartBridge_,
        &ChartBridge::seriesChanged,
        this,
        [this](
            const QString& symbol,
            const QString& timeframe,
            const QString& source,
            const QJsonArray& bars) {
            sendCommand(QJsonObject{
                {QStringLiteral("type"), QStringLiteral("series")},
                {QStringLiteral("symbol"), symbol},
                {QStringLiteral("timeframe"), timeframe},
                {QStringLiteral("source"), source},
                {QStringLiteral("bars"), bars},
            });
        });
    connect(
        chartBridge_,
        &ChartBridge::themeChanged,
        this,
        [this](const bool dark) {
            sendCommand(QJsonObject{
                {QStringLiteral("type"), QStringLiteral("theme")},
                {QStringLiteral("dark"), dark},
            });
        });
    connect(
        chartBridge_,
        &ChartBridge::chartStyleChanged,
        this,
        [this](const QString& style) {
            sendCommand(QJsonObject{
                {QStringLiteral("type"), QStringLiteral("style")},
                {QStringLiteral("style"), style},
            });
        });
    connect(
        chartBridge_,
        &ChartBridge::priceScaleModeChanged,
        this,
        [this](const QString& mode) {
            sendCommand(QJsonObject{
                {QStringLiteral("type"), QStringLiteral("priceScale")},
                {QStringLiteral("mode"), mode},
            });
        });
    connect(
        chartBridge_,
        &ChartBridge::indicatorsChanged,
        this,
        [this](const QJsonArray& calculations) {
            sendCommand(QJsonObject{
                {QStringLiteral("type"), QStringLiteral("indicators")},
                {QStringLiteral("calculations"), calculations},
            });
        });
    connect(chartBridge_, &ChartBridge::fitRequested, this, [this]() {
        sendCommand(QStringLiteral("fit"));
    });
    connect(
        chartBridge_,
        &ChartBridge::errorReported,
        this,
        &MobileController::reportChartError);

    showInitialDemo();
}

QString MobileController::status() const {
    return status_;
}

QString MobileController::source() const {
    return source_;
}

QString MobileController::latestPrice() const {
    return latestPrice_;
}

QString MobileController::priceChange() const {
    return priceChange_;
}

QString MobileController::loadedRange() const {
    return loadedRange_;
}

QString MobileController::averageVolume() const {
    return averageVolume_;
}

int MobileController::barCount() const noexcept {
    return barCount_;
}

bool MobileController::busy() const noexcept {
    return busy_;
}

bool MobileController::hasTwelveDataKey() const noexcept {
    return marketDataClient_->hasTwelveDataKey();
}

qreal MobileController::topSystemInset() const noexcept {
    return topSystemInset_;
}

qreal MobileController::bottomSystemInset() const noexcept {
    return bottomSystemInset_;
}

void MobileController::refresh(QString symbol, const int timeframeIndex) {
    symbol = symbol.trimmed().toUpper();
    if (symbol.isEmpty()) {
        setStatus(QStringLiteral("Enter a symbol before refreshing."));
        return;
    }
    if (symbol.size() > 32 ||
        std::any_of(symbol.cbegin(), symbol.cend(), [](const QChar character) {
            return character.isSpace() || !character.isPrint();
        })) {
        setStatus(QStringLiteral("The symbol must be 1–32 characters without spaces."));
        return;
    }

    symbol_ = std::move(symbol);
    timeframe_ = timeframeFromIndex(timeframeIndex);
    setBusy(true);
    setStatus(
        QStringLiteral("Loading %1 %2 from Yahoo Finance…")
            .arg(symbol_, timeframeLabel(timeframe_)));

    marketDataClient_->fetch(
        symbol_,
        timeframe_,
        [this, requestedSymbol = symbol_, requestedTimeframe = timeframe_](
            MarketDataResult result) mutable {
            setBusy(false);
            if (!result.ok()) {
                setStatus(
                    QStringLiteral("Live data failed; displaying the last data. %1")
                        .arg(result.error));
                return;
            }
            applySeries(
                requestedSymbol,
                requestedTimeframe,
                result.source,
                std::move(result.bars));
            const auto delayed = result.metadata.exchangeDelayMinutes.value_or(0);
            setStatus(
                delayed > 0
                    ? QStringLiteral(
                          "%1 bars loaded from %2 · exchange delay about %3 min")
                          .arg(barCount_)
                          .arg(source_)
                          .arg(delayed)
                    : QStringLiteral("%1 bars loaded from %2")
                          .arg(barCount_)
                          .arg(source_));
        });
}

void MobileController::setChartStyle(const int styleIndex) {
    static const std::array styles{
        QStringLiteral("candlestick"),
        QStringLiteral("line"),
        QStringLiteral("area"),
    };
    const auto index =
        std::clamp(styleIndex, 0, static_cast<int>(styles.size()) - 1);
    chartBridge_->setChartStyle(styles[static_cast<std::size_t>(index)]);
}

void MobileController::setIndicator(const int indicatorIndex) {
    indicatorIndex_ = indicatorIndex;
    updateIndicator();
}

void MobileController::setDarkTheme(const bool dark) {
    chartBridge_->setDarkTheme(dark);
}

void MobileController::fitChart() {
    chartBridge_->requestFit();
}

void MobileController::chartLoaded() {
    if (chartLoaded_) {
        return;
    }
    chartLoaded_ = true;
    chartBridge_->webReady();
    setStatus(QStringLiteral("Chart ready · fetching real market data…"));
}

void MobileController::setTwelveDataKey(const QString& apiKey) {
    marketDataClient_->setTwelveDataKey(apiKey);
    emit twelveDataKeyChanged();
    setStatus(
        hasTwelveDataKey()
            ? QStringLiteral(
                  "Twelve Data fallback enabled for this app session. The key "
                  "is not stored.")
            : QStringLiteral("Twelve Data fallback disabled."));
}

void MobileController::reportChartError(const QString& message) {
    setStatus(QStringLiteral("Chart error: %1").arg(message.left(300)));
}

void MobileController::reportWebViewError(const QString& message) {
    setStatus(QStringLiteral("Android WebView failed: %1").arg(message.left(300)));
}

void MobileController::showInitialDemo() {
    applySeries(
        symbol_,
        timeframe_,
        QStringLiteral("Demo · waiting for network"),
        DemoDataSource::generate(
            symbol_.toStdString(),
            timeframe_,
            300,
            QDateTime::currentSecsSinceEpoch()));
}

void MobileController::applySeries(
    const QString& symbol,
    const Timeframe timeframe,
    const QString& source,
    Bars bars) {
    bars_ = std::move(bars);
    symbol_ = symbol;
    timeframe_ = timeframe;
    source_ = source;
    barCount_ = static_cast<int>(bars_.size());

    const auto statistics = calculateMarketStatistics(bars_);
    latestPrice_ = formatPrice(statistics.latestClose);
    priceChange_ =
        QStringLiteral("%1%2 (%3%4%)")
            .arg(statistics.barChange >= 0.0 ? QStringLiteral("+") : QString())
            .arg(formatPrice(statistics.barChange))
            .arg(
                statistics.barChangePercent >= 0.0 ? QStringLiteral("+") : QString())
            .arg(QLocale().toString(statistics.barChangePercent, 'f', 2));
    loadedRange_ =
        QStringLiteral("%1 – %2")
            .arg(
                formatPrice(statistics.loadedLow),
                formatPrice(statistics.loadedHigh));
    averageVolume_ = formatVolume(statistics.averageVolume20);

    chartBridge_->setSeries(
        symbol_,
        timeframeLabel(timeframe_),
        source_,
        bars_);
    updateIndicator();
    emit marketSummaryChanged();
}

void MobileController::updateIndicator() {
    const auto kind = indicatorFromIndex(indicatorIndex_);
    if (kind == IndicatorKind::None || bars_.empty()) {
        chartBridge_->setIndicators({});
        return;
    }
    chartBridge_->setIndicators(
        calculateIndicators(bars_, {defaultIndicatorSpec(kind)}));
}

void MobileController::sendCommand(const QString& type) {
    sendCommand(QJsonObject{{QStringLiteral("type"), type}});
}

void MobileController::sendCommand(const QJsonObject& command) {
    if (!chartLoaded_) {
        return;
    }
    const auto payload =
        QJsonDocument(command).toJson(QJsonDocument::Compact).toBase64();
    const auto script =
        QStringLiteral(
            "(() => { try {"
            "const b=Uint8Array.from(atob('%1'),c=>c.charCodeAt(0));"
            "window.mobileChart.receive(JSON.parse(new TextDecoder().decode(b)));"
            "return '';"
            "} catch (e) { return String(e?.message ?? e); } })();")
            .arg(QString::fromLatin1(payload));
    emit executeChartJavaScript(script);
}

void MobileController::setStatus(QString status) {
    if (status_ == status) {
        return;
    }
    status_ = std::move(status);
    emit statusChanged();
}

void MobileController::setBusy(const bool busy) {
    if (busy_ == busy) {
        return;
    }
    busy_ = busy;
    emit busyChanged();
}

Timeframe MobileController::timeframeFromIndex(const int index) noexcept {
    switch (index) {
    case 0:
        return Timeframe::OneMinute;
    case 1:
        return Timeframe::FiveMinutes;
    case 2:
        return Timeframe::FifteenMinutes;
    case 3:
        return Timeframe::OneHour;
    case 4:
        return Timeframe::OneDay;
    default:
        return Timeframe::FiveMinutes;
    }
}

QString MobileController::timeframeLabel(const Timeframe timeframe) {
    switch (timeframe) {
    case Timeframe::OneMinute:
        return QStringLiteral("1 minute");
    case Timeframe::FiveMinutes:
        return QStringLiteral("5 minutes");
    case Timeframe::FifteenMinutes:
        return QStringLiteral("15 minutes");
    case Timeframe::OneHour:
        return QStringLiteral("1 hour");
    case Timeframe::OneDay:
        return QStringLiteral("1 day");
    }
    return QStringLiteral("5 minutes");
}

} // namespace tvchart
