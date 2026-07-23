#include "main_window.hpp"

#include "data/csv_bar_loader.hpp"
#include "data/demo_data_source.hpp"

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

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent) {
    buildUi();
    restoreSettings();
    loadDemo();
}

void MainWindow::buildUi() {
    setWindowTitle(tr("TradingView Chart — Offline"));
    resize(1280, 800);

    chartView_ = new ChartView(this);
    setCentralWidget(chartView_);
    connect(chartView_, &ChartView::chartReady, this, &MainWindow::chartReady);
    connect(chartView_, &ChartView::chartError, this, [this](const QString& message) {
        statusBar()->showMessage(message);
    });

    auto* fileMenu = menuBar()->addMenu(tr("&File"));
    auto* openAction = fileMenu->addAction(tr("&Open OHLCV CSV…"));
    openAction->setShortcut(QKeySequence::Open);
    connect(openAction, &QAction::triggered, this, &MainWindow::openCsv);

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
                loadDemo();
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
    sourceLabel_ = new QLabel(tr("Offline"), toolbar);
    sourceLabel_->setObjectName(QStringLiteral("sourceLabel"));
    toolbar->addWidget(sourceLabel_);

    auto* watchlistDock = new QDockWidget(tr("Offline watchlist"), this);
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
            loadDemo();
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

void MainWindow::loadDemo() {
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
            symbol, activeTimeframeLabel(), std::move(bars))) {
        setStatus(tr("Offline demo"), barCount);
    }
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
            symbol, tr("CSV"), result.bars)) {
        setStatus(tr("Local CSV: %1").arg(QFileInfo(path).fileName()), barCount);
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
           "<p>An offline-first C++/Qt chart viewer.</p>"
           "<p>Charts are rendered by "
           "<a href=\"https://www.tradingview.com/\">TradingView "
           "Lightweight Charts™</a> 5.2.0 under the Apache-2.0 license.</p>"
           "<p>Lightweight Charts is a renderer and does not provide market data. "
           "Demo data is synthetic; imported data remains local.</p>"),
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

void MainWindow::setStatus(const QString& source, const std::size_t barCount) {
    sourceLabel_->setText(source);
    statusBar()->showMessage(
        tr("%1 · %2 · %3 bars")
            .arg(activeSymbol(), activeTimeframeLabel())
            .arg(static_cast<qulonglong>(barCount)));
}

void MainWindow::closeEvent(QCloseEvent* event) {
    saveSettings();
    QMainWindow::closeEvent(event);
}

} // namespace tvchart
