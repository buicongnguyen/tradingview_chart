#include "main_window.hpp"

#include "data/csv_bar_loader.hpp"
#include "data/demo_data_source.hpp"
#include "data/market_data_client.hpp"

#include <QAction>
#include <QApplication>
#include <QCloseEvent>
#include <QComboBox>
#include <QDateTime>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDockWidget>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QKeySequence>
#include <QLabel>
#include <QListWidget>
#include <QLocale>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QSettings>
#include <QStatusBar>
#include <QToolBar>
#include <QTimer>
#include <QVBoxLayout>

#include <array>
#include <cmath>
#include <exception>
#include <utility>

namespace tvchart {
namespace {

constexpr auto kOrganization = "buicongnguyen";
constexpr auto kApplication = "TradingViewChart";

[[nodiscard]] QString timeframeLabel(const Timeframe timeframe) {
    switch (timeframe) {
    case Timeframe::OneMinute:
        return QStringLiteral("1m");
    case Timeframe::FiveMinutes:
        return QStringLiteral("5m");
    case Timeframe::FifteenMinutes:
        return QStringLiteral("15m");
    case Timeframe::OneHour:
        return QStringLiteral("1h");
    case Timeframe::OneDay:
        return QStringLiteral("1D");
    }
    return QStringLiteral("1m");
}

[[nodiscard]] QString formatPrice(const double value) {
    const auto decimals = std::abs(value) < 1.0 ? 4 : 2;
    return QLocale::system().toString(value, 'f', decimals);
}

[[nodiscard]] QString formatVolume(const double value) {
    return QLocale::system().toString(std::max(0.0, value), 'f', 0);
}

} // namespace

MainWindow::MainWindow(
    QWidget* parent,
    const bool onlineDataEnabled,
    const bool settingsEnabled)
    : QMainWindow(parent),
      onlineDataEnabled_(onlineDataEnabled),
      settingsEnabled_(settingsEnabled) {
    buildUi();
    if (settingsEnabled_) {
        restoreSettings();
    }
    reloadActiveSource();
}

void MainWindow::buildUi() {
    setWindowTitle(tr("TradingView Chart"));
    resize(1280, 800);

    chartView_ = new ChartView(this);
    marketDataClient_ = new MarketDataClient(this);
    refreshTimer_ = new QTimer(this);
    refreshTimer_->setSingleShot(true);
    connect(refreshTimer_, &QTimer::timeout, this, &MainWindow::loadMarketData);
    setCentralWidget(chartView_);
    connect(chartView_, &ChartView::chartReady, this, &MainWindow::chartReady);
    connect(chartView_, &ChartView::chartError, this, [this](const QString& message) {
        statusBar()->showMessage(message);
    });

    auto* fileMenu = menuBar()->addMenu(tr("&File"));
    auto* openAction = fileMenu->addAction(tr("&Open OHLCV CSV…"));
    openAction->setShortcut(QKeySequence::Open);
    connect(openAction, &QAction::triggered, this, &MainWindow::openCsv);

    auto* refreshAction = fileMenu->addAction(tr("&Refresh market data"));
    refreshAction->setShortcut(QKeySequence::Refresh);
    refreshAction->setEnabled(onlineDataEnabled_);
    connect(refreshAction, &QAction::triggered, this, &MainWindow::loadMarketData);

    auto* demoAction = fileMenu->addAction(tr("Load &offline demo"));
    demoAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+D")));
    connect(demoAction, &QAction::triggered, this, &MainWindow::loadDemo);
    fileMenu->addSeparator();
    fileMenu->addAction(tr("E&xit"), QKeySequence::Quit, this, &QWidget::close);

    auto* viewMenu = menuBar()->addMenu(tr("&View"));
    auto* fitAction = viewMenu->addAction(tr("&Fit all data"));
    fitAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+0")));
    connect(fitAction, &QAction::triggered, chartView_->bridge(), &ChartBridge::requestFit);

    darkThemeAction_ = viewMenu->addAction(tr("&Dark theme"));
    darkThemeAction_->setCheckable(true);
    darkThemeAction_->setChecked(true);
    connect(darkThemeAction_, &QAction::toggled, this, &MainWindow::applyTheme);

    auto* helpMenu = menuBar()->addMenu(tr("&Help"));
    helpMenu->addAction(tr("&About"), this, &MainWindow::showAbout);

    auto* toolbar = addToolBar(tr("Chart"));
    toolbar->setObjectName(QStringLiteral("chartToolbar"));
    toolbar->setMovable(false);
    toolbar->addAction(openAction);
    toolbar->addAction(refreshAction);
    toolbar->addAction(demoAction);
    toolbar->addSeparator();

    toolbar->addWidget(new QLabel(tr("Interval"), toolbar));
    timeframeSelector_ = new QComboBox(toolbar);
    const std::array timeframes{
        Timeframe::OneMinute,
        Timeframe::FiveMinutes,
        Timeframe::FifteenMinutes,
        Timeframe::OneHour,
        Timeframe::OneDay,
    };
    for (const auto timeframe : timeframes) {
        timeframeSelector_->addItem(timeframeLabel(timeframe), static_cast<int>(timeframe));
    }
    timeframeSelector_->setCurrentIndex(1);
    toolbar->addWidget(timeframeSelector_);
    connect(
        timeframeSelector_,
        &QComboBox::currentIndexChanged,
        this,
        [this](int) {
            if (!restoringSettings_) {
                reloadActiveSource();
            }
        });

    toolbar->addSeparator();
    toolbar->addWidget(new QLabel(tr("Style"), toolbar));
    styleSelector_ = new QComboBox(toolbar);
    styleSelector_->addItem(tr("Candles"), QStringLiteral("candlestick"));
    styleSelector_->addItem(tr("Line"), QStringLiteral("line"));
    styleSelector_->addItem(tr("Area"), QStringLiteral("area"));
    toolbar->addWidget(styleSelector_);
    connect(styleSelector_, &QComboBox::currentIndexChanged, this, [this](int) {
        chartView_->bridge()->setChartStyle(styleSelector_->currentData().toString());
    });

    toolbar->addSeparator();
    toolbar->addWidget(new QLabel(tr("Indicator"), toolbar));
    indicatorSelector_ = new QComboBox(toolbar);
    indicatorSelector_->setObjectName(QStringLiteral("indicatorSelector"));
    indicatorSelector_->addItem(
        tr("None"),
        static_cast<int>(IndicatorKind::None));
    indicatorSelector_->addItem(
        tr("SMA (20)"),
        static_cast<int>(IndicatorKind::SimpleMovingAverage));
    indicatorSelector_->addItem(
        tr("EMA (20)"),
        static_cast<int>(IndicatorKind::ExponentialMovingAverage));
    indicatorSelector_->addItem(
        tr("VWAP (UTC session)"),
        static_cast<int>(IndicatorKind::VolumeWeightedAveragePrice));
    indicatorSelector_->addItem(
        tr("RSI (14)"),
        static_cast<int>(IndicatorKind::RelativeStrengthIndex));
    indicatorSelector_->addItem(
        tr("MACD (12, 26, 9)"),
        static_cast<int>(
            IndicatorKind::MovingAverageConvergenceDivergence));
    indicatorSelector_->setCurrentIndex(1);
    toolbar->addWidget(indicatorSelector_);
    connect(
        indicatorSelector_,
        &QComboBox::currentIndexChanged,
        this,
        [this](int) {
            updateTechnicalAnalysis();
        });

    toolbar->addSeparator();
    sourceLabel_ = new QLabel(tr("Loading…"), toolbar);
    sourceLabel_->setObjectName(QStringLiteral("sourceLabel"));
    toolbar->addWidget(sourceLabel_);

    auto* watchlistDock = new QDockWidget(tr("Watchlist"), this);
    watchlistDock->setObjectName(QStringLiteral("watchlistDock"));
    watchlistDock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    watchlist_ = new QListWidget(watchlistDock);
    watchlist_->addItems({
        QStringLiteral("AAPL"),
        QStringLiteral("MSFT"),
        QStringLiteral("NVDA"),
        QStringLiteral("TSLA"),
        QStringLiteral("SPY"),
        QStringLiteral("BTCUSD"),
    });
    watchlist_->setCurrentRow(0);
    watchlistDock->setWidget(watchlist_);
    addDockWidget(Qt::LeftDockWidgetArea, watchlistDock);
    connect(watchlist_, &QListWidget::currentRowChanged, this, [this](int) {
        if (!restoringSettings_) {
            reloadActiveSource();
        }
    });

    auto* analysisDock = new QDockWidget(tr("Calculated information"), this);
    analysisDock->setObjectName(QStringLiteral("analysisDock"));
    analysisDock->setAllowedAreas(
        Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    auto* analysisPanel = new QWidget(analysisDock);
    analysisPanel->setObjectName(QStringLiteral("analysisPanel"));
    auto* analysisLayout = new QFormLayout(analysisPanel);
    analysisLayout->setContentsMargins(10, 10, 10, 10);
    analysisLayout->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    latestValueLabel_ = new QLabel(QStringLiteral("—"), analysisPanel);
    changeValueLabel_ = new QLabel(QStringLiteral("—"), analysisPanel);
    rangeValueLabel_ = new QLabel(QStringLiteral("—"), analysisPanel);
    averageVolumeValueLabel_ = new QLabel(QStringLiteral("—"), analysisPanel);
    indicatorValueLabel_ = new QLabel(QStringLiteral("—"), analysisPanel);
    indicatorValueLabel_->setWordWrap(true);
    analysisLayout->addRow(tr("Latest close"), latestValueLabel_);
    analysisLayout->addRow(tr("Last-bar change"), changeValueLabel_);
    analysisLayout->addRow(tr("Loaded range"), rangeValueLabel_);
    analysisLayout->addRow(tr("Average volume (20)"), averageVolumeValueLabel_);
    analysisLayout->addRow(tr("Technical calculation"), indicatorValueLabel_);
    analysisDock->setWidget(analysisPanel);
    addDockWidget(Qt::RightDockWidgetArea, analysisDock);

    statusBar()->showMessage(tr("Starting local chart renderer…"));
}

void MainWindow::restoreSettings() {
    restoringSettings_ = true;
    QSettings settings(QString::fromLatin1(kOrganization), QString::fromLatin1(kApplication));
    restoreGeometry(settings.value(QStringLiteral("window/geometry")).toByteArray());
    restoreState(settings.value(QStringLiteral("window/state")).toByteArray());

    const auto symbol = settings.value(QStringLiteral("chart/symbol"), QStringLiteral("AAPL")).toString();
    const auto matchingItems = watchlist_->findItems(symbol, Qt::MatchExactly);
    if (!matchingItems.isEmpty()) {
        watchlist_->setCurrentItem(matchingItems.front());
    }

    const auto timeframeValue =
        settings.value(
                    QStringLiteral("chart/timeframe"),
                    static_cast<int>(Timeframe::FiveMinutes))
            .toInt();
    const auto timeframeIndex = timeframeSelector_->findData(timeframeValue);
    if (timeframeIndex >= 0) {
        timeframeSelector_->setCurrentIndex(timeframeIndex);
    }

    const auto style = settings.value(
                                   QStringLiteral("chart/style"),
                                   QStringLiteral("candlestick"))
                           .toString();
    const auto styleIndex = styleSelector_->findData(style);
    if (styleIndex >= 0) {
        styleSelector_->setCurrentIndex(styleIndex);
    }

    const auto indicatorValue =
        settings.value(
                    QStringLiteral("chart/indicator"),
                    static_cast<int>(IndicatorKind::SimpleMovingAverage))
            .toInt();
    const auto indicatorIndex = indicatorSelector_->findData(indicatorValue);
    if (indicatorIndex >= 0) {
        indicatorSelector_->setCurrentIndex(indicatorIndex);
    }

    const auto dark = settings.value(QStringLiteral("chart/darkTheme"), true).toBool();
    darkThemeAction_->setChecked(dark);
    applyTheme(dark);
    restoringSettings_ = false;
}

void MainWindow::saveSettings() const {
    QSettings settings(QString::fromLatin1(kOrganization), QString::fromLatin1(kApplication));
    settings.setValue(QStringLiteral("window/geometry"), saveGeometry());
    settings.setValue(QStringLiteral("window/state"), saveState());
    settings.setValue(QStringLiteral("chart/symbol"), activeSymbol());
    settings.setValue(
        QStringLiteral("chart/timeframe"),
        timeframeSelector_->currentData().toInt());
    settings.setValue(QStringLiteral("chart/style"), styleSelector_->currentData().toString());
    settings.setValue(
        QStringLiteral("chart/indicator"),
        indicatorSelector_->currentData().toInt());
    settings.setValue(QStringLiteral("chart/darkTheme"), darkThemeAction_->isChecked());
}

void MainWindow::reloadActiveSource() {
    if (onlineDataEnabled_) {
        loadMarketData();
    } else {
        loadDemo();
    }
}

void MainWindow::loadMarketData() {
    if (!onlineDataEnabled_ || !watchlist_ || !timeframeSelector_ ||
        watchlist_->currentItem() == nullptr) {
        return;
    }

    refreshTimer_->stop();
    const auto symbol = activeSymbol();
    const auto timeframe = activeTimeframe();
    const auto timeframeText = activeTimeframeLabel();
    sourceLabel_->setText(tr("Loading Yahoo…"));
    sourceLabel_->setToolTip(
        marketDataClient_->hasTwelveDataKey()
            ? tr("Yahoo Finance is primary; Twelve Data is the configured fallback.")
            : tr("Yahoo Finance is primary. Set TWELVE_DATA_API_KEY to enable fallback."));
    statusBar()->showMessage(tr("%1 · %2 · requesting market data…").arg(
        symbol, timeframeText));

    marketDataClient_->fetch(
        symbol,
        timeframe,
        [this, symbol, timeframe, timeframeText](MarketDataResult result) mutable {
            if (symbol != activeSymbol() || timeframe != activeTimeframe()) {
                return;
            }

            if (result.ok()) {
                const auto barCount = result.bars.size();
                if (applySeries(
                        symbol,
                        timeframeText,
                        result.source,
                        std::move(result.bars))) {
                    setStatus(result.source, barCount, symbol, timeframeText);
                    sourceLabel_->setToolTip(
                        result.source == QStringLiteral("Yahoo Finance")
                            ? tr("Unofficial Yahoo Finance chart endpoint.")
                            : tr("Twelve Data fallback response."));
                }
            } else {
                showDemo(tr("Offline fallback"), result.error);
            }
            scheduleRefresh(timeframe);
        });
}

void MainWindow::loadDemo() {
    marketDataClient_->cancel();
    refreshTimer_->stop();
    showDemo(tr("Offline demo"), {});
}

void MainWindow::showDemo(
    const QString& source,
    const QString& detail) {
    if (!watchlist_ || !timeframeSelector_ || watchlist_->currentItem() == nullptr) {
        return;
    }

    const auto symbol = activeSymbol();
    const auto timeframe = activeTimeframe();
    const auto now = QDateTime::currentDateTimeUtc().toSecsSinceEpoch();
    auto bars = DemoDataSource::generate(
        symbol.toStdString(), timeframe, 600, now);
    const auto barCount = bars.size();
    if (applySeries(
            symbol,
            activeTimeframeLabel(),
            source,
            std::move(bars))) {
        setStatus(source, barCount, symbol, activeTimeframeLabel());
        sourceLabel_->setToolTip(detail);
        if (!detail.isEmpty()) {
            statusBar()->showMessage(
                tr("%1 · %2 · offline fallback: %3")
                    .arg(symbol, activeTimeframeLabel(), detail),
                20'000);
        }
    }
}

bool MainWindow::applySeries(
    const QString& symbol,
    const QString& timeframe,
    const QString& source,
    Bars bars) {
    if (!chartView_->bridge()->setSeries(
            symbol,
            timeframe,
            source,
            bars)) {
        return false;
    }
    currentBars_ = std::move(bars);
    updateTechnicalAnalysis();
    return true;
}

void MainWindow::updateTechnicalAnalysis() {
    if (!chartView_ || !indicatorSelector_ || currentBars_.empty()) {
        return;
    }

    try {
        const auto statistics = calculateMarketStatistics(currentBars_);
        latestValueLabel_->setText(formatPrice(statistics.latestClose));
        const auto changePrefix =
            statistics.barChange > 0.0 ? QStringLiteral("+") : QString{};
        changeValueLabel_->setText(
            tr("%1%2 (%3%4%)")
                .arg(changePrefix)
                .arg(formatPrice(statistics.barChange))
                .arg(
                    statistics.barChangePercent > 0.0
                        ? QStringLiteral("+")
                        : QString{})
                .arg(
                    QLocale::system().toString(
                        statistics.barChangePercent,
                        'f',
                        2)));
        changeValueLabel_->setStyleSheet(
            statistics.barChange > 0.0
                ? QStringLiteral("color: #26a69a;")
                : statistics.barChange < 0.0
                    ? QStringLiteral("color: #ef5350;")
                    : QString{});
        rangeValueLabel_->setText(
            tr("%1 – %2 · close at %3%")
                .arg(formatPrice(statistics.loadedLow))
                .arg(formatPrice(statistics.loadedHigh))
                .arg(
                    QLocale::system().toString(
                        statistics.loadedRangePositionPercent,
                        'f',
                        1)));
        averageVolumeValueLabel_->setText(
            formatVolume(statistics.averageVolume20));
    } catch (const std::exception& error) {
        latestValueLabel_->setText(tr("Unavailable"));
        changeValueLabel_->setText(tr("Unavailable"));
        changeValueLabel_->setStyleSheet({});
        rangeValueLabel_->setText(tr("Unavailable"));
        averageVolumeValueLabel_->setText(tr("Unavailable"));
        indicatorValueLabel_->setToolTip(QString::fromUtf8(error.what()));
    }

    try {
        auto calculation =
            calculateIndicator(currentBars_, activeIndicator());
        chartView_->bridge()->setIndicator(calculation);
        const auto label = QString::fromLatin1(
            indicatorLabel(calculation.kind).data(),
            static_cast<qsizetype>(
                indicatorLabel(calculation.kind).size()));
        if (calculation.kind == IndicatorKind::None) {
            indicatorValueLabel_->setText(tr("None"));
        } else if (calculation.primary.empty()) {
            indicatorValueLabel_->setText(tr("%1 · warming up").arg(label));
        } else if (
            calculation.kind ==
            IndicatorKind::MovingAverageConvergenceDivergence) {
            const auto macd = calculation.primary.back().value;
            if (calculation.secondary.empty() ||
                calculation.histogram.empty()) {
                indicatorValueLabel_->setText(
                    tr("%1\nMACD %2 · signal warming up")
                        .arg(label)
                        .arg(formatPrice(macd)));
            } else {
                indicatorValueLabel_->setText(
                    tr("%1\nMACD %2 · signal %3 · histogram %4")
                        .arg(label)
                        .arg(formatPrice(macd))
                        .arg(formatPrice(
                            calculation.secondary.back().value))
                        .arg(formatPrice(
                            calculation.histogram.back().value)));
            }
        } else {
            indicatorValueLabel_->setText(
                tr("%1 · %2")
                    .arg(label)
                    .arg(formatPrice(calculation.primary.back().value)));
        }
        indicatorValueLabel_->setToolTip(
            tr("Calculated locally from the loaded OHLCV bars. "
               "This is not a forecast or trading recommendation."));
    } catch (const std::exception& error) {
        chartView_->bridge()->setIndicator({});
        indicatorValueLabel_->setText(tr("Calculation unavailable"));
        indicatorValueLabel_->setToolTip(QString::fromUtf8(error.what()));
    }
}

void MainWindow::scheduleRefresh(const Timeframe timeframe) {
    if (!onlineDataEnabled_) {
        return;
    }
    const auto interval =
        timeframe == Timeframe::OneDay ? 15 * 60 * 1'000 : 2 * 60 * 1'000;
    refreshTimer_->start(interval);
}

void MainWindow::openCsv() {
    QSettings settings(QString::fromLatin1(kOrganization), QString::fromLatin1(kApplication));
    const auto initialDirectory =
        settings.value(QStringLiteral("files/lastDirectory")).toString();
    const auto path = QFileDialog::getOpenFileName(
        this,
        tr("Open OHLCV CSV"),
        initialDirectory,
        tr("CSV files (*.csv);;All files (*.*)"));
    if (path.isEmpty()) {
        return;
    }

    marketDataClient_->cancel();
    refreshTimer_->stop();
    const auto result = CsvBarLoader::loadFile(path);
    if (!result.ok()) {
        QMessageBox::critical(
            this,
            tr("CSV import failed"),
            tr("The file was not loaded.\n\n%1").arg(result.error));
        return;
    }

    settings.setValue(QStringLiteral("files/lastDirectory"), QFileInfo(path).absolutePath());
    const auto symbol = QFileInfo(path).completeBaseName().toUpper();
    const auto barCount = result.bars.size();
    if (applySeries(
            symbol, tr("CSV"), tr("Local CSV"), result.bars)) {
        setStatus(
            tr("Local CSV: %1").arg(QFileInfo(path).fileName()),
            barCount,
            symbol,
            tr("CSV"));
        sourceLabel_->setToolTip(path);
    }
}

void MainWindow::applyTheme(const bool dark) {
    chartView_->bridge()->setDarkTheme(dark);
    if (dark) {
        qApp->setStyleSheet(QStringLiteral(
            "QMainWindow, QDialog, QMenuBar, QMenu, QToolBar, QDockWidget, "
            "QListWidget, QComboBox, QStatusBar {"
            " background: #131722; color: #d1d4dc; }"
            "QListWidget::item:selected { background: #2962ff; color: white; }"
            "QToolBar { border-bottom: 1px solid #2a2e39; spacing: 7px; }"
            "QDockWidget { border: 1px solid #2a2e39; }"
            "QWidget#analysisPanel, QWidget#analysisPanel QLabel {"
            " background: #131722; color: #d1d4dc; }"));
    } else {
        qApp->setStyleSheet({});
    }
}

void MainWindow::showAbout() {
    QDialog dialog(this);
    dialog.setWindowTitle(tr("About TradingView Chart"));
    dialog.setMinimumWidth(520);
    auto* layout = new QVBoxLayout(&dialog);
    auto* label = new QLabel(
        tr("<h2>TradingView Chart 0.1.0</h2>"
           "<p>A C++/Qt market chart viewer with online and offline sources.</p>"
           "<p>Charts are rendered by "
           "<a href=\"https://www.tradingview.com/\">TradingView "
           "Lightweight Charts™</a> 5.2.0 under the Apache-2.0 license.</p>"
           "<p>Yahoo Finance is queried first through an unofficial chart endpoint. "
           "Twelve Data is used as a fallback when TWELVE_DATA_API_KEY is set. "
           "Provider availability, terms, freshness, and display rights apply.</p>"
           "<p>SMA, EMA, UTC-session VWAP, RSI, MACD, price change, range, and "
           "average volume are calculated locally from the displayed OHLCV bars. "
           "They are descriptive calculations, not forecasts or trading advice.</p>"
           "<p>Lightweight Charts is only the renderer. Offline demo data is synthetic; "
           "imported CSV data remains local.</p>"),
        &dialog);
    label->setWordWrap(true);
    label->setOpenExternalLinks(true);
    label->setTextInteractionFlags(Qt::TextBrowserInteraction);
    layout->addWidget(label);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok, &dialog);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    layout->addWidget(buttons);
    dialog.exec();
}

QString MainWindow::activeSymbol() const {
    return watchlist_->currentItem() != nullptr
               ? watchlist_->currentItem()->text()
               : QStringLiteral("AAPL");
}

Timeframe MainWindow::activeTimeframe() const {
    return static_cast<Timeframe>(timeframeSelector_->currentData().toInt());
}

QString MainWindow::activeTimeframeLabel() const {
    return timeframeLabel(activeTimeframe());
}

IndicatorKind MainWindow::activeIndicator() const {
    return static_cast<IndicatorKind>(
        indicatorSelector_->currentData().toInt());
}

void MainWindow::setStatus(
    const QString& source,
    const std::size_t barCount,
    const QString& symbol,
    const QString& timeframe) {
    sourceLabel_->setText(source);
    statusBar()->showMessage(
        tr("%1 · %2 · %3 bars")
            .arg(symbol, timeframe)
            .arg(static_cast<qulonglong>(barCount)));
}

void MainWindow::closeEvent(QCloseEvent* event) {
    if (settingsEnabled_) {
        saveSettings();
    }
    QMainWindow::closeEvent(event);
}

} // namespace tvchart
