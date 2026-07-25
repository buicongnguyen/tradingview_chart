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
#include <QKeySequence>
#include <QLabel>
#include <QListWidget>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QSettings>
#include <QStatusBar>
#include <QToolBar>
#include <QTimer>
#include <QVBoxLayout>

#include <array>
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
                if (chartView_->bridge()->setSeries(
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
    if (chartView_->bridge()->setSeries(
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
    if (chartView_->bridge()->setSeries(
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
            "QDockWidget { border: 1px solid #2a2e39; }"));
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
