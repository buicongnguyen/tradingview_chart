#include "main_window.hpp"

#include "data/csv_bar_loader.hpp"
#include "data/demo_data_source.hpp"
#include "data/historical_data_store.hpp"
#include "data/market_data_client.hpp"
#include "research/alpha_vantage_research_client.hpp"
#include "research/margin_risk.hpp"
#include "strategy/strategy_lab_widget.hpp"

#include <QAbstractItemView>
#include <QAction>
#include <QApplication>
#include <QCloseEvent>
#include <QComboBox>
#include <QDateEdit>
#include <QDateTime>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QDockWidget>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QLocale>
#include <QMenuBar>
#include <QMessageBox>
#include <QPushButton>
#include <QSaveFile>
#include <QSettings>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QStatusBar>
#include <QStandardPaths>
#include <QStringList>
#include <QTabWidget>
#include <QTableWidget>
#include <QToolBar>
#include <QTimeZone>
#include <QTimer>
#include <QUuid>
#include <QVBoxLayout>

#include <algorithm>
#include <array>
#include <cmath>
#include <exception>
#include <iterator>
#include <ranges>
#include <utility>

namespace tvchart {
namespace {

constexpr auto kOrganization = "buicongnguyen";
constexpr auto kApplication = "TradingViewChart";
constexpr auto kMaximumWatchlistFileBytes = qint64{2 * 1024 * 1024};
constexpr auto kMaximumResearchFileBytes = qint64{2 * 1024 * 1024};

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

[[nodiscard]] QString indicatorName(const IndicatorKind kind) {
    switch (kind) {
    case IndicatorKind::None:
        return QStringLiteral("None");
    case IndicatorKind::SimpleMovingAverage:
        return QStringLiteral("Simple moving average");
    case IndicatorKind::ExponentialMovingAverage:
        return QStringLiteral("Exponential moving average");
    case IndicatorKind::VolumeWeightedAveragePrice:
        return QStringLiteral("VWAP (UTC session)");
    case IndicatorKind::RelativeStrengthIndex:
        return QStringLiteral("Relative strength index");
    case IndicatorKind::MovingAverageConvergenceDivergence:
        return QStringLiteral("MACD");
    case IndicatorKind::RollingHigh:
        return QStringLiteral("Rolling high");
    case IndicatorKind::RollingLow:
        return QStringLiteral("Rolling low");
    case IndicatorKind::VolumeSimpleMovingAverage:
        return QStringLiteral("Volume moving average");
    }
    return QStringLiteral("Unknown");
}

[[nodiscard]] std::optional<IndicatorKind> indicatorKindFromId(
    const QString& id) {
    constexpr std::array kinds{
        IndicatorKind::SimpleMovingAverage,
        IndicatorKind::ExponentialMovingAverage,
        IndicatorKind::VolumeWeightedAveragePrice,
        IndicatorKind::RelativeStrengthIndex,
        IndicatorKind::MovingAverageConvergenceDivergence,
        IndicatorKind::RollingHigh,
        IndicatorKind::RollingLow,
        IndicatorKind::VolumeSimpleMovingAverage,
    };
    for (const auto kind : kinds) {
        const auto value = indicatorId(kind);
        if (id == QString::fromLatin1(
                      value.data(),
                      static_cast<qsizetype>(value.size()))) {
            return kind;
        }
    }
    return std::nullopt;
}

[[nodiscard]] QString utcTimestamp(const std::int64_t timestamp) {
    if (timestamp <= 0) {
        return QStringLiteral("—");
    }
    return QDateTime::fromSecsSinceEpoch(timestamp, QTimeZone::UTC)
        .toString(QStringLiteral("yyyy-MM-dd HH:mm:ss 'UTC'"));
}

[[nodiscard]] QString ageText(
    const std::int64_t timestamp,
    const std::int64_t now) {
    if (timestamp <= 0) {
        return QStringLiteral("unknown age");
    }
    const auto seconds = std::max<std::int64_t>(0, now - timestamp);
    if (seconds < 60) {
        return QStringLiteral("%1s ago").arg(seconds);
    }
    if (seconds < 60 * 60) {
        return QStringLiteral("%1m ago").arg(seconds / 60);
    }
    if (seconds < 24 * 60 * 60) {
        return QStringLiteral("%1h %2m ago")
            .arg(seconds / (60 * 60))
            .arg((seconds / 60) % 60);
    }
    return QStringLiteral("%1d %2h ago")
        .arg(seconds / (24 * 60 * 60))
        .arg((seconds / (60 * 60)) % 24);
}

[[nodiscard]] QStringList nonEmptyParts(
    std::initializer_list<QString> values) {
    QStringList result;
    for (auto value : values) {
        value = value.trimmed();
        if (!value.isEmpty()) {
            result.push_back(std::move(value));
        }
    }
    return result;
}

[[nodiscard]] QString confidenceLabel(
    const ResearchConfidence confidence) {
    switch (confidence) {
    case ResearchConfidence::Confirmed:
        return QStringLiteral("Confirmed");
    case ResearchConfidence::Estimated:
        return QStringLiteral("Estimated");
    case ResearchConfidence::Unknown:
        return QStringLiteral("Unknown");
    }
    return QStringLiteral("Unknown");
}

[[nodiscard]] QString optionalNumber(
    const std::optional<double>& value,
    const QString& suffix = {}) {
    return value ? QStringLiteral("%1%2").arg(formatPrice(*value), suffix)
                 : QStringLiteral("—");
}

[[nodiscard]] QString compactAmount(const std::optional<double>& value) {
    if (!value) {
        return QStringLiteral("—");
    }
    const auto absolute = std::abs(*value);
    if (absolute >= 1'000'000'000'000.0) {
        return QStringLiteral("%1T").arg(
            QLocale::system().toString(*value / 1'000'000'000'000.0, 'f', 2));
    }
    if (absolute >= 1'000'000'000.0) {
        return QStringLiteral("%1B").arg(
            QLocale::system().toString(*value / 1'000'000'000.0, 'f', 2));
    }
    if (absolute >= 1'000'000.0) {
        return QStringLiteral("%1M").arg(
            QLocale::system().toString(*value / 1'000'000.0, 'f', 2));
    }
    return formatPrice(*value);
}

} // namespace

MainWindow::MainWindow(
    QWidget* parent,
    const bool onlineDataEnabled,
    const bool settingsEnabled)
    : QMainWindow(parent),
      onlineDataEnabled_(onlineDataEnabled),
      settingsEnabled_(settingsEnabled),
      restoringSettings_(true) {
    buildUi();
    if (settingsEnabled_) {
        restoreSettings();
    } else {
        refreshWatchlistSelector();
        refreshWatchlistEntries(QStringLiteral("AAPL"));
        applyTheme(true);
    }
    restoringSettings_ = false;
    updateTechnicalAnalysis();
    refreshResearchDisplay();
    recalculateMarginRisk();
    reloadActiveSource();
}

MainWindow::~MainWindow() = default;

void MainWindow::buildUi() {
    setWindowTitle(tr("TradingView Chart"));
    resize(1500, 900);

    chartView_ = new ChartView(this);
    marketDataClient_ = new MarketDataClient(this);
    researchClient_ = new AlphaVantageResearchClient(this);
    const auto historyPath =
        settingsEnabled_
            ? QDir(
                  QStandardPaths::writableLocation(
                      QStandardPaths::AppDataLocation))
                  .filePath(QStringLiteral("history.sqlite"))
            : QStringLiteral(":memory:");
    historyStore_ =
        std::make_unique<HistoricalDataStore>(historyPath);
    const auto historyAvailable = historyStore_->open();
    refreshTimer_ = new QTimer(this);
    refreshTimer_->setSingleShot(true);
    connect(refreshTimer_, &QTimer::timeout, this, &MainWindow::loadMarketData);
    statusAgeTimer_ = new QTimer(this);
    statusAgeTimer_->setInterval(30'000);
    connect(
        statusAgeTimer_,
        &QTimer::timeout,
        this,
        &MainWindow::refreshDataStatusDisplay);
    statusAgeTimer_->start();

    setCentralWidget(chartView_);
    connect(chartView_, &ChartView::chartReady, this, &MainWindow::chartReady);
    connect(
        chartView_,
        &ChartView::chartError,
        this,
        [this](const QString& message) {
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
    auto* importWatchlistAction =
        fileMenu->addAction(tr("Import watchlist CSV…"));
    connect(
        importWatchlistAction,
        &QAction::triggered,
        this,
        &MainWindow::importWatchlist);
    auto* exportWatchlistAction =
        fileMenu->addAction(tr("Export active watchlist CSV…"));
    connect(
        exportWatchlistAction,
        &QAction::triggered,
        this,
        &MainWindow::exportWatchlist);
    fileMenu->addSeparator();
    fileMenu->addAction(tr("E&xit"), QKeySequence::Quit, this, &QWidget::close);

    auto* viewMenu = menuBar()->addMenu(tr("&View"));
    auto* fitAction = viewMenu->addAction(tr("&Fit all data"));
    fitAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+0")));
    connect(
        fitAction,
        &QAction::triggered,
        chartView_->bridge(),
        &ChartBridge::requestFit);

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
        timeframeSelector_->addItem(
            timeframeLabel(timeframe),
            static_cast<int>(timeframe));
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
                saveSettingsNow();
            }
        });

    toolbar->addSeparator();
    toolbar->addWidget(new QLabel(tr("Style"), toolbar));
    styleSelector_ = new QComboBox(toolbar);
    styleSelector_->addItem(tr("Candles"), QStringLiteral("candlestick"));
    styleSelector_->addItem(tr("Line"), QStringLiteral("line"));
    styleSelector_->addItem(tr("Area"), QStringLiteral("area"));
    toolbar->addWidget(styleSelector_);
    connect(
        styleSelector_,
        &QComboBox::currentIndexChanged,
        this,
        [this](int) {
            chartView_->bridge()->setChartStyle(
                styleSelector_->currentData().toString());
            saveSettingsNow();
        });

    toolbar->addSeparator();
    toolbar->addWidget(new QLabel(tr("Scale"), toolbar));
    scaleSelector_ = new QComboBox(toolbar);
    scaleSelector_->setObjectName(QStringLiteral("scaleSelector"));
    scaleSelector_->addItem(tr("Linear"), QStringLiteral("normal"));
    scaleSelector_->addItem(tr("Log"), QStringLiteral("logarithmic"));
    scaleSelector_->addItem(tr("Percent"), QStringLiteral("percentage"));
    toolbar->addWidget(scaleSelector_);
    connect(
        scaleSelector_,
        &QComboBox::currentIndexChanged,
        this,
        [this](int) {
            chartView_->bridge()->setPriceScaleMode(
                scaleSelector_->currentData().toString());
            saveSettingsNow();
        });

    toolbar->addSeparator();
    sourceLabel_ = new QLabel(tr("Loading…"), toolbar);
    sourceLabel_->setObjectName(QStringLiteral("sourceLabel"));
    toolbar->addWidget(sourceLabel_);

    buildWatchlistDock();
    buildAnalysisDock();
    buildIndicatorDock();
    buildDataStatusDock();
    buildResearchDock();
    buildMarginRiskDock();
    buildStrategyLabDock();
    statusBar()->showMessage(tr("Starting local chart renderer…"));
    if (!historyAvailable) {
        statusBar()->showMessage(
            tr("Historical cache is unavailable: %1")
                .arg(historyStore_->lastError()),
            15'000);
    }
}

void MainWindow::buildWatchlistDock() {
    auto* dock = new QDockWidget(tr("Watchlists"), this);
    dock->setObjectName(QStringLiteral("watchlistDock"));
    dock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);

    auto* panel = new QWidget(dock);
    auto* layout = new QVBoxLayout(panel);
    layout->setContentsMargins(6, 6, 6, 6);

    namedWatchlistSelector_ = new QComboBox(panel);
    namedWatchlistSelector_->setObjectName(
        QStringLiteral("namedWatchlistSelector"));
    layout->addWidget(namedWatchlistSelector_);
    connect(
        namedWatchlistSelector_,
        &QComboBox::currentIndexChanged,
        this,
        &MainWindow::selectNamedWatchlist);

    auto* listActions = new QHBoxLayout;
    auto* newList = new QPushButton(tr("New"), panel);
    auto* renameList = new QPushButton(tr("Rename"), panel);
    auto* deleteList = new QPushButton(tr("Delete"), panel);
    listActions->addWidget(newList);
    listActions->addWidget(renameList);
    listActions->addWidget(deleteList);
    layout->addLayout(listActions);
    connect(newList, &QPushButton::clicked, this, &MainWindow::createNamedWatchlist);
    connect(renameList, &QPushButton::clicked, this, &MainWindow::renameNamedWatchlist);
    connect(deleteList, &QPushButton::clicked, this, &MainWindow::deleteNamedWatchlist);

    auto* sortRow = new QHBoxLayout;
    sortRow->addWidget(new QLabel(tr("Sort"), panel));
    watchlistSortSelector_ = new QComboBox(panel);
    watchlistSortSelector_->addItem(
        tr("Manual"),
        static_cast<int>(WatchlistSort::Manual));
    watchlistSortSelector_->addItem(
        tr("Ticker"),
        static_cast<int>(WatchlistSort::Symbol));
    sortRow->addWidget(watchlistSortSelector_);
    layout->addLayout(sortRow);
    connect(
        watchlistSortSelector_,
        &QComboBox::currentIndexChanged,
        this,
        &MainWindow::updateWatchlistSort);

    watchlist_ = new QListWidget(panel);
    watchlist_->setObjectName(QStringLiteral("watchlist"));
    watchlist_->setSelectionMode(QAbstractItemView::SingleSelection);
    layout->addWidget(watchlist_, 1);
    connect(
        watchlist_,
        &QListWidget::currentRowChanged,
        this,
        [this](int) {
            const auto entryIndex = activeWatchlistEntryIndex();
            const auto* list = activeWatchlist();
            watchlistNoteInput_->setText(
                list && entryIndex
                    ? list->entries[*entryIndex].note
                    : QString{});
            refreshResearchDisplay();
            recalculateMarginRisk();
            if (!restoringSettings_ && watchlist_->currentItem()) {
                reloadActiveSource();
                saveSettingsNow();
            }
        });

    auto* moveRow = new QHBoxLayout;
    auto* moveUp = new QPushButton(tr("Move up"), panel);
    auto* moveDown = new QPushButton(tr("Move down"), panel);
    moveRow->addWidget(moveUp);
    moveRow->addWidget(moveDown);
    layout->addLayout(moveRow);
    connect(moveUp, &QPushButton::clicked, this, [this] {
        moveWatchlistSymbol(-1);
    });
    connect(moveDown, &QPushButton::clicked, this, [this] {
        moveWatchlistSymbol(1);
    });

    auto* symbolRow = new QHBoxLayout;
    watchlistSymbolInput_ = new QLineEdit(panel);
    watchlistSymbolInput_->setPlaceholderText(tr("Ticker, e.g. AAPL"));
    auto* addSymbol = new QPushButton(tr("Add"), panel);
    auto* removeSymbol = new QPushButton(tr("Remove"), panel);
    symbolRow->addWidget(watchlistSymbolInput_, 1);
    symbolRow->addWidget(addSymbol);
    symbolRow->addWidget(removeSymbol);
    layout->addLayout(symbolRow);
    connect(addSymbol, &QPushButton::clicked, this, &MainWindow::addWatchlistSymbol);
    connect(
        watchlistSymbolInput_,
        &QLineEdit::returnPressed,
        this,
        &MainWindow::addWatchlistSymbol);
    connect(removeSymbol, &QPushButton::clicked, this, &MainWindow::removeWatchlistSymbol);

    layout->addWidget(new QLabel(tr("Selected-symbol note"), panel));
    watchlistNoteInput_ = new QLineEdit(panel);
    watchlistNoteInput_->setMaxLength(512);
    watchlistNoteInput_->setPlaceholderText(
        tr("Optional local note (not sent to providers)"));
    layout->addWidget(watchlistNoteInput_);
    connect(
        watchlistNoteInput_,
        &QLineEdit::editingFinished,
        this,
        &MainWindow::updateWatchlistNote);

    dock->setWidget(panel);
    addDockWidget(Qt::LeftDockWidgetArea, dock);
}

void MainWindow::buildAnalysisDock() {
    auto* dock = new QDockWidget(tr("Calculated information"), this);
    dock->setObjectName(QStringLiteral("analysisDock"));
    dock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    auto* panel = new QWidget(dock);
    panel->setObjectName(QStringLiteral("analysisPanel"));
    auto* layout = new QFormLayout(panel);
    layout->setContentsMargins(10, 10, 10, 10);
    layout->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);

    latestValueLabel_ = new QLabel(QStringLiteral("—"), panel);
    changeValueLabel_ = new QLabel(QStringLiteral("—"), panel);
    rangeValueLabel_ = new QLabel(QStringLiteral("—"), panel);
    averageVolumeValueLabel_ = new QLabel(QStringLiteral("—"), panel);
    indicatorValueLabel_ = new QLabel(QStringLiteral("—"), panel);
    indicatorValueLabel_->setWordWrap(true);
    layout->addRow(tr("Latest close"), latestValueLabel_);
    layout->addRow(tr("Last-bar change"), changeValueLabel_);
    layout->addRow(tr("Loaded range"), rangeValueLabel_);
    layout->addRow(tr("Average volume (20)"), averageVolumeValueLabel_);
    layout->addRow(tr("Technical calculations"), indicatorValueLabel_);

    dock->setWidget(panel);
    addDockWidget(Qt::RightDockWidgetArea, dock);
}

void MainWindow::buildIndicatorDock() {
    auto* dock = new QDockWidget(tr("Indicators"), this);
    dock->setObjectName(QStringLiteral("indicatorDock"));
    dock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);

    auto* panel = new QWidget(dock);
    auto* layout = new QVBoxLayout(panel);
    layout->setContentsMargins(6, 6, 6, 6);
    auto* explanation = new QLabel(
        tr("Enable several calculations at once. Periods apply to finalized "
           "bars currently loaded in the chart."),
        panel);
    explanation->setWordWrap(true);
    layout->addWidget(explanation);

    constexpr std::array kinds{
        IndicatorKind::SimpleMovingAverage,
        IndicatorKind::ExponentialMovingAverage,
        IndicatorKind::VolumeWeightedAveragePrice,
        IndicatorKind::RelativeStrengthIndex,
        IndicatorKind::MovingAverageConvergenceDivergence,
        IndicatorKind::RollingHigh,
        IndicatorKind::RollingLow,
        IndicatorKind::VolumeSimpleMovingAverage,
    };
    indicatorTable_ = new QTableWidget(
        static_cast<int>(kinds.size()),
        5,
        panel);
    indicatorTable_->setObjectName(QStringLiteral("indicatorTable"));
    indicatorTable_->setHorizontalHeaderLabels({
        tr("On"),
        tr("Indicator"),
        tr("Period / Fast"),
        tr("Slow"),
        tr("Signal"),
    });
    indicatorTable_->verticalHeader()->setVisible(false);
    indicatorTable_->setSelectionMode(QAbstractItemView::NoSelection);
    indicatorTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    indicatorTable_->horizontalHeader()->setSectionResizeMode(
        QHeaderView::ResizeToContents);
    indicatorTable_->horizontalHeader()->setSectionResizeMode(
        1,
        QHeaderView::Stretch);

    indicatorControls_.reserve(kinds.size());
    for (std::size_t row = 0; row < kinds.size(); ++row) {
        const auto kind = kinds[row];
        const auto spec = defaultIndicatorSpec(kind);
        auto* enabled = new QTableWidgetItem;
        enabled->setFlags(Qt::ItemIsEnabled | Qt::ItemIsUserCheckable);
        enabled->setCheckState(
            kind == IndicatorKind::SimpleMovingAverage
                ? Qt::Checked
                : Qt::Unchecked);
        indicatorTable_->setItem(static_cast<int>(row), 0, enabled);

        auto* name = new QTableWidgetItem(indicatorName(kind));
        name->setFlags(Qt::ItemIsEnabled);
        indicatorTable_->setItem(static_cast<int>(row), 1, name);

        IndicatorControl control{
            .kind = kind,
            .enabledItem = enabled,
        };
        const auto createSpin = [panel](const int minimum, const int maximum, const int value) {
            auto* spin = new QSpinBox(panel);
            spin->setRange(minimum, maximum);
            spin->setValue(value);
            spin->setKeyboardTracking(false);
            return spin;
        };
        if (kind == IndicatorKind::MovingAverageConvergenceDivergence) {
            control.periodOrFast = createSpin(1, 499, static_cast<int>(spec.fastPeriod));
            control.slow = createSpin(2, 500, static_cast<int>(spec.slowPeriod));
            control.signal = createSpin(1, 500, static_cast<int>(spec.signalPeriod));
            indicatorTable_->setCellWidget(
                static_cast<int>(row), 2, control.periodOrFast);
            indicatorTable_->setCellWidget(
                static_cast<int>(row), 3, control.slow);
            indicatorTable_->setCellWidget(
                static_cast<int>(row), 4, control.signal);
            connect(
                control.periodOrFast,
                &QSpinBox::valueChanged,
                this,
                [this, slow = control.slow](const int fast) {
                    slow->setMinimum(fast + 1);
                    handleIndicatorConfigurationChanged();
                });
            connect(
                control.slow,
                &QSpinBox::valueChanged,
                this,
                [this](int) { handleIndicatorConfigurationChanged(); });
            connect(
                control.signal,
                &QSpinBox::valueChanged,
                this,
                [this](int) { handleIndicatorConfigurationChanged(); });
        } else if (kind != IndicatorKind::VolumeWeightedAveragePrice) {
            control.periodOrFast = createSpin(
                1,
                500,
                static_cast<int>(spec.period));
            indicatorTable_->setCellWidget(
                static_cast<int>(row), 2, control.periodOrFast);
            connect(
                control.periodOrFast,
                &QSpinBox::valueChanged,
                this,
                [this](int) { handleIndicatorConfigurationChanged(); });
            indicatorTable_->setItem(
                static_cast<int>(row),
                3,
                new QTableWidgetItem(QStringLiteral("—")));
            indicatorTable_->setItem(
                static_cast<int>(row),
                4,
                new QTableWidgetItem(QStringLiteral("—")));
        } else {
            for (auto column = 2; column <= 4; ++column) {
                auto* unused = new QTableWidgetItem(QStringLiteral("—"));
                unused->setFlags(Qt::ItemIsEnabled);
                indicatorTable_->setItem(
                    static_cast<int>(row),
                    column,
                    unused);
            }
        }
        indicatorControls_.push_back(control);
    }
    connect(
        indicatorTable_,
        &QTableWidget::itemChanged,
        this,
        [this](QTableWidgetItem*) {
            handleIndicatorConfigurationChanged();
        });
    layout->addWidget(indicatorTable_, 1);

    auto* reset = new QPushButton(tr("Reset indicator defaults"), panel);
    connect(reset, &QPushButton::clicked, this, [this] {
        const auto wasRestoring = restoringSettings_;
        restoringSettings_ = true;
        for (auto& control : indicatorControls_) {
            const auto defaults = defaultIndicatorSpec(control.kind);
            control.enabledItem->setCheckState(
                control.kind == IndicatorKind::SimpleMovingAverage
                    ? Qt::Checked
                    : Qt::Unchecked);
            if (control.periodOrFast) {
                control.periodOrFast->setValue(
                    control.kind ==
                            IndicatorKind::MovingAverageConvergenceDivergence
                        ? static_cast<int>(defaults.fastPeriod)
                        : static_cast<int>(defaults.period));
            }
            if (control.slow) {
                control.slow->setValue(static_cast<int>(defaults.slowPeriod));
            }
            if (control.signal) {
                control.signal->setValue(static_cast<int>(defaults.signalPeriod));
            }
        }
        restoringSettings_ = wasRestoring;
        handleIndicatorConfigurationChanged();
    });
    layout->addWidget(reset);

    dock->setWidget(panel);
    addDockWidget(Qt::RightDockWidgetArea, dock);
}

void MainWindow::buildDataStatusDock() {
    auto* dock = new QDockWidget(tr("Data status"), this);
    dock->setObjectName(QStringLiteral("dataStatusDock"));
    dock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    auto* panel = new QWidget(dock);
    panel->setObjectName(QStringLiteral("dataStatusPanel"));
    auto* layout = new QFormLayout(panel);
    layout->setContentsMargins(10, 10, 10, 10);
    layout->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);

    dataLifecycleLabel_ = new QLabel(tr("Starting"), panel);
    dataDeliveryLabel_ = new QLabel(QStringLiteral("—"), panel);
    dataLastBarLabel_ = new QLabel(QStringLiteral("—"), panel);
    dataRetrievedLabel_ = new QLabel(QStringLiteral("—"), panel);
    dataMarketLabel_ = new QLabel(QStringLiteral("—"), panel);
    dataBarCountLabel_ = new QLabel(QStringLiteral("0"), panel);
    dataDetailLabel_ = new QLabel(QStringLiteral("—"), panel);
    dataDetailLabel_->setWordWrap(true);
    layout->addRow(tr("State"), dataLifecycleLabel_);
    layout->addRow(tr("Delivery"), dataDeliveryLabel_);
    layout->addRow(tr("Last candle"), dataLastBarLabel_);
    layout->addRow(tr("Retrieved"), dataRetrievedLabel_);
    layout->addRow(tr("Market"), dataMarketLabel_);
    layout->addRow(tr("Bars"), dataBarCountLabel_);
    layout->addRow(tr("Detail"), dataDetailLabel_);

    dock->setWidget(panel);
    addDockWidget(Qt::RightDockWidgetArea, dock);
}

void MainWindow::buildResearchDock() {
    auto* dock = new QDockWidget(tr("Research"), this);
    dock->setObjectName(QStringLiteral("researchDock"));
    dock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);

    auto* tabs = new QTabWidget(dock);
    tabs->setObjectName(QStringLiteral("researchTabs"));

    auto* summaryPanel = new QWidget(tabs);
    summaryPanel->setObjectName(QStringLiteral("researchPanel"));
    auto* summaryLayout = new QFormLayout(summaryPanel);
    summaryLayout->setContentsMargins(10, 10, 10, 10);
    summaryLayout->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    researchProviderLabel_ = new QLabel(tr("Not loaded"), summaryPanel);
    researchCompanyLabel_ = new QLabel(QStringLiteral("—"), summaryPanel);
    researchFundamentalsLabel_ = new QLabel(QStringLiteral("—"), summaryPanel);
    researchProviderTargetLabel_ = new QLabel(QStringLiteral("—"), summaryPanel);
    researchRatingsLabel_ = new QLabel(QStringLiteral("—"), summaryPanel);
    researchManualConsensusLabel_ = new QLabel(QStringLiteral("—"), summaryPanel);
    researchNextEventLabel_ = new QLabel(QStringLiteral("—"), summaryPanel);
    for (auto* label : {
             researchProviderLabel_,
             researchCompanyLabel_,
             researchFundamentalsLabel_,
             researchProviderTargetLabel_,
             researchRatingsLabel_,
             researchManualConsensusLabel_,
             researchNextEventLabel_,
         }) {
        label->setWordWrap(true);
    }
    researchProviderTargetLabel_->setToolTip(
        tr("Provider consensus is shown separately from organization-level "
           "targets so it is never double counted."));
    summaryLayout->addRow(tr("Provider / as of"), researchProviderLabel_);
    summaryLayout->addRow(tr("Company"), researchCompanyLabel_);
    summaryLayout->addRow(tr("Fundamentals"), researchFundamentalsLabel_);
    summaryLayout->addRow(
        tr("Provider aggregate target"),
        researchProviderTargetLabel_);
    summaryLayout->addRow(tr("Provider ratings"), researchRatingsLabel_);
    summaryLayout->addRow(
        tr("Organization targets"),
        researchManualConsensusLabel_);
    summaryLayout->addRow(tr("Next event"), researchNextEventLabel_);
    researchRefreshButton_ =
        new QPushButton(tr("Refresh Alpha Vantage research"), summaryPanel);
    researchRefreshButton_->setObjectName(
        QStringLiteral("researchRefreshButton"));
    researchRefreshButton_->setEnabled(onlineDataEnabled_);
    researchRefreshButton_->setToolTip(
        tr("Uses ALPHA_VANTAGE_API_KEY. Refresh is manual to preserve free-plan "
           "request quotas."));
    summaryLayout->addRow(researchRefreshButton_);
    connect(
        researchRefreshButton_,
        &QPushButton::clicked,
        this,
        &MainWindow::refreshResearchFromProvider);
    tabs->addTab(summaryPanel, tr("Summary"));

    auto* targetPanel = new QWidget(tabs);
    targetPanel->setObjectName(QStringLiteral("targetEstimatePanel"));
    auto* targetLayout = new QVBoxLayout(targetPanel);
    targetLayout->setContentsMargins(6, 6, 6, 6);
    auto* targetExplanation = new QLabel(
        tr("Locally record published organization targets with date and "
           "provenance. Values are descriptive inputs, not forecasts generated "
           "by this application."),
        targetPanel);
    targetExplanation->setWordWrap(true);
    targetLayout->addWidget(targetExplanation);
    targetEstimateTable_ = new QTableWidget(0, 5, targetPanel);
    targetEstimateTable_->setObjectName(QStringLiteral("targetEstimateTable"));
    targetEstimateTable_->setHorizontalHeaderLabels(
        {tr("Organization"),
         tr("Target"),
         tr("Currency"),
         tr("Published"),
         tr("Rating")});
    targetEstimateTable_->setSelectionBehavior(
        QAbstractItemView::SelectRows);
    targetEstimateTable_->setSelectionMode(
        QAbstractItemView::SingleSelection);
    targetEstimateTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    targetEstimateTable_->horizontalHeader()->setSectionResizeMode(
        QHeaderView::ResizeToContents);
    targetEstimateTable_->horizontalHeader()->setStretchLastSection(true);
    targetLayout->addWidget(targetEstimateTable_, 1);
    auto* targetActions = new QHBoxLayout;
    auto* addTarget = new QPushButton(tr("Add"), targetPanel);
    auto* removeTarget = new QPushButton(tr("Remove"), targetPanel);
    auto* importTargets = new QPushButton(tr("Import CSV"), targetPanel);
    auto* exportTargets = new QPushButton(tr("Export CSV"), targetPanel);
    targetActions->addWidget(addTarget);
    targetActions->addWidget(removeTarget);
    targetActions->addWidget(importTargets);
    targetActions->addWidget(exportTargets);
    targetLayout->addLayout(targetActions);
    connect(addTarget, &QPushButton::clicked, this, &MainWindow::addTargetEstimate);
    connect(
        removeTarget,
        &QPushButton::clicked,
        this,
        &MainWindow::removeTargetEstimate);
    connect(
        importTargets,
        &QPushButton::clicked,
        this,
        &MainWindow::importTargetEstimates);
    connect(
        exportTargets,
        &QPushButton::clicked,
        this,
        &MainWindow::exportTargetEstimates);
    tabs->addTab(targetPanel, tr("Targets"));

    auto* eventPanel = new QWidget(tabs);
    eventPanel->setObjectName(QStringLiteral("researchEventPanel"));
    auto* eventLayout = new QVBoxLayout(eventPanel);
    eventLayout->setContentsMargins(6, 6, 6, 6);
    auto* eventExplanation = new QLabel(
        tr("Dates retain their source and confidence. Earnings-calendar dates "
           "are estimates unless the source explicitly confirms them."),
        eventPanel);
    eventExplanation->setWordWrap(true);
    eventLayout->addWidget(eventExplanation);
    researchEventTable_ = new QTableWidget(0, 5, eventPanel);
    researchEventTable_->setObjectName(QStringLiteral("researchEventTable"));
    researchEventTable_->setHorizontalHeaderLabels(
        {tr("Date"), tr("Type"), tr("Confidence"), tr("Event"), tr("Source")});
    researchEventTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
    researchEventTable_->setSelectionMode(QAbstractItemView::SingleSelection);
    researchEventTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    researchEventTable_->horizontalHeader()->setSectionResizeMode(
        QHeaderView::ResizeToContents);
    researchEventTable_->horizontalHeader()->setStretchLastSection(true);
    eventLayout->addWidget(researchEventTable_, 1);
    auto* eventActions = new QHBoxLayout;
    auto* addEvent = new QPushButton(tr("Add event"), eventPanel);
    auto* removeEvent = new QPushButton(tr("Remove"), eventPanel);
    eventActions->addWidget(addEvent);
    eventActions->addWidget(removeEvent);
    eventActions->addStretch();
    eventLayout->addLayout(eventActions);
    connect(addEvent, &QPushButton::clicked, this, &MainWindow::addResearchEvent);
    connect(
        removeEvent,
        &QPushButton::clicked,
        this,
        &MainWindow::removeResearchEvent);
    tabs->addTab(eventPanel, tr("Calendar"));

    dock->setWidget(tabs);
    addDockWidget(Qt::RightDockWidgetArea, dock);
    if (auto* analysisDock =
            findChild<QDockWidget*>(QStringLiteral("analysisDock"))) {
        tabifyDockWidget(analysisDock, dock);
    }
}

void MainWindow::buildMarginRiskDock() {
    auto* dock = new QDockWidget(tr("Margin risk scenario"), this);
    dock->setObjectName(QStringLiteral("marginRiskDock"));
    dock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    auto* panel = new QWidget(dock);
    panel->setObjectName(QStringLiteral("marginRiskPanel"));
    auto* layout = new QVBoxLayout(panel);
    layout->setContentsMargins(10, 10, 10, 10);

    auto* explanation = new QLabel(
        tr("Long-only uniform-maintenance scenario. This does not connect to a "
           "broker and cannot predict a broker's margin-call date or house "
           "requirement."),
        panel);
    explanation->setWordWrap(true);
    layout->addWidget(explanation);

    auto* inputs = new QFormLayout;
    const auto configureMoney = [](QDoubleSpinBox* input) {
        input->setDecimals(2);
        input->setRange(0.0, 1'000'000'000'000.0);
        input->setSingleStep(1'000.0);
        input->setPrefix(QStringLiteral("$ "));
    };
    marginLongValueInput_ = new QDoubleSpinBox(panel);
    marginLongValueInput_->setObjectName(
        QStringLiteral("marginLongValueInput"));
    configureMoney(marginLongValueInput_);
    marginLongValueInput_->setValue(100'000.0);
    marginDebitInput_ = new QDoubleSpinBox(panel);
    marginDebitInput_->setObjectName(QStringLiteral("marginDebitInput"));
    configureMoney(marginDebitInput_);
    marginDebitInput_->setValue(50'000.0);
    marginOtherEquityInput_ = new QDoubleSpinBox(panel);
    marginOtherEquityInput_->setObjectName(
        QStringLiteral("marginOtherEquityInput"));
    configureMoney(marginOtherEquityInput_);
    marginMaintenanceInput_ = new QDoubleSpinBox(panel);
    marginMaintenanceInput_->setObjectName(
        QStringLiteral("marginMaintenanceInput"));
    marginMaintenanceInput_->setDecimals(2);
    marginMaintenanceInput_->setRange(0.01, 99.99);
    marginMaintenanceInput_->setSuffix(QStringLiteral(" %"));
    marginMaintenanceInput_->setValue(25.0);
    marginStressInput_ = new QDoubleSpinBox(panel);
    marginStressInput_->setObjectName(QStringLiteral("marginStressInput"));
    marginStressInput_->setDecimals(2);
    marginStressInput_->setRange(-99.99, 100.0);
    marginStressInput_->setSuffix(QStringLiteral(" %"));
    marginStressInput_->setValue(-20.0);
    inputs->addRow(tr("Long market value"), marginLongValueInput_);
    inputs->addRow(tr("Margin debit"), marginDebitInput_);
    inputs->addRow(tr("Other account equity"), marginOtherEquityInput_);
    inputs->addRow(tr("Maintenance assumption"), marginMaintenanceInput_);
    inputs->addRow(tr("Price stress"), marginStressInput_);
    layout->addLayout(inputs);

    auto* results = new QFormLayout;
    marginInstrumentLabel_ = new QLabel(QStringLiteral("—"), panel);
    marginCurrentLabel_ = new QLabel(QStringLiteral("—"), panel);
    marginStressResultLabel_ = new QLabel(QStringLiteral("—"), panel);
    marginThresholdLabel_ = new QLabel(QStringLiteral("—"), panel);
    for (auto* label : {
             marginInstrumentLabel_,
             marginCurrentLabel_,
             marginStressResultLabel_,
             marginThresholdLabel_,
         }) {
        label->setWordWrap(true);
    }
    results->addRow(tr("Chart context"), marginInstrumentLabel_);
    results->addRow(tr("Current"), marginCurrentLabel_);
    results->addRow(tr("Stressed"), marginStressResultLabel_);
    results->addRow(tr("Call threshold"), marginThresholdLabel_);
    layout->addLayout(results);
    layout->addStretch();

    const auto recalculate = [this](double) {
        recalculateMarginRisk();
        saveSettingsNow();
    };
    for (auto* input : {
             marginLongValueInput_,
             marginDebitInput_,
             marginOtherEquityInput_,
             marginMaintenanceInput_,
             marginStressInput_,
         }) {
        connect(
            input,
            qOverload<double>(&QDoubleSpinBox::valueChanged),
            this,
            recalculate);
    }

    dock->setWidget(panel);
    addDockWidget(Qt::RightDockWidgetArea, dock);
    if (auto* dataDock =
            findChild<QDockWidget*>(QStringLiteral("dataStatusDock"))) {
        tabifyDockWidget(dataDock, dock);
    }
}

void MainWindow::buildStrategyLabDock() {
    auto* dock = new QDockWidget(tr("Strategy Lab"), this);
    dock->setObjectName(QStringLiteral("strategyLabDock"));
    dock->setAllowedAreas(
        Qt::BottomDockWidgetArea |
        Qt::LeftDockWidgetArea |
        Qt::RightDockWidgetArea);
    strategyLab_ = new StrategyLabWidget(historyStore_.get(), dock);
    strategyLab_->setObjectName(QStringLiteral("strategyLabWidget"));
    dock->setWidget(strategyLab_);
    addDockWidget(Qt::BottomDockWidgetArea, dock);

    connect(
        strategyLab_,
        &StrategyLabWidget::replayBarsRequested,
        this,
        &MainWindow::showReplaySeries);
    connect(
        strategyLab_,
        &StrategyLabWidget::restoreFullSeriesRequested,
        this,
        &MainWindow::restoreFullSeries);
    connect(
        strategyLab_,
        &StrategyLabWidget::statusMessage,
        this,
        [this](const QString& message) {
            statusBar()->showMessage(message, 12'000);
        });
    connect(
        dock,
        &QDockWidget::visibilityChanged,
        this,
        [this](const bool visible) {
            if (!visible && strategyLab_) {
                strategyLab_->restoreChartIfReplaying();
            }
        });
}

void MainWindow::restoreSettings() {
    QSettings settings(
        QString::fromLatin1(kOrganization),
        QString::fromLatin1(kApplication));
    restoreGeometry(settings.value(QStringLiteral("window/geometry")).toByteArray());
    restoreState(settings.value(QStringLiteral("window/state")).toByteArray());

    const auto serializedWatchlists =
        settings.value(QStringLiteral("watchlists/json")).toByteArray();
    if (!serializedWatchlists.isEmpty()) {
        const auto loaded = deserializeWatchlists(serializedWatchlists);
        if (loaded.ok()) {
            watchlists_ = loaded.collection;
        } else {
            statusBar()->showMessage(
                tr("Saved watchlists were invalid; defaults restored: %1")
                    .arg(loaded.error),
                12'000);
        }
    }
    const auto serializedResearch =
        settings.value(QStringLiteral("research/workspace")).toByteArray();
    if (!serializedResearch.isEmpty()) {
        const auto loaded = deserializeResearchWorkspace(serializedResearch);
        if (loaded.ok()) {
            researchWorkspace_ = loaded.workspace;
        } else {
            statusBar()->showMessage(
                tr("Saved research data was invalid and was not loaded: %1")
                    .arg(loaded.error),
                12'000);
        }
    }
    refreshWatchlistSelector();
    refreshWatchlistEntries(
        settings.value(
                    QStringLiteral("chart/symbol"),
                    QStringLiteral("AAPL"))
            .toString());

    const auto timeframeValue =
        settings.value(
                    QStringLiteral("chart/timeframe"),
                    static_cast<int>(Timeframe::FiveMinutes))
            .toInt();
    if (const auto index = timeframeSelector_->findData(timeframeValue);
        index >= 0) {
        timeframeSelector_->setCurrentIndex(index);
    }

    const auto style = settings.value(
                                   QStringLiteral("chart/style"),
                                   QStringLiteral("candlestick"))
                           .toString();
    if (const auto index = styleSelector_->findData(style); index >= 0) {
        styleSelector_->setCurrentIndex(index);
    }
    const auto scale = settings.value(
                                   QStringLiteral("chart/priceScale"),
                                   QStringLiteral("normal"))
                           .toString();
    if (const auto index = scaleSelector_->findData(scale); index >= 0) {
        scaleSelector_->setCurrentIndex(index);
    }
    restoreIndicatorSettings(settings);
    marginLongValueInput_->setValue(
        settings.value(QStringLiteral("margin/longMarketValue"), 100'000.0)
            .toDouble());
    marginDebitInput_->setValue(
        settings.value(QStringLiteral("margin/debit"), 50'000.0).toDouble());
    marginOtherEquityInput_->setValue(
        settings.value(QStringLiteral("margin/otherEquity"), 0.0).toDouble());
    marginMaintenanceInput_->setValue(
        settings.value(QStringLiteral("margin/maintenancePercent"), 25.0)
            .toDouble());
    marginStressInput_->setValue(
        settings.value(QStringLiteral("margin/stressPercent"), -20.0)
            .toDouble());

    const auto dark =
        settings.value(QStringLiteral("chart/darkTheme"), true).toBool();
    darkThemeAction_->setChecked(dark);
    applyTheme(dark);
    chartView_->bridge()->setChartStyle(styleSelector_->currentData().toString());
    chartView_->bridge()->setPriceScaleMode(
        scaleSelector_->currentData().toString());
    if (strategyLab_) {
        strategyLab_->restoreSettings(settings);
    }
}

void MainWindow::restoreIndicatorSettings(QSettings& settings) {
    auto applied = false;
    const auto payload =
        settings.value(QStringLiteral("chart/indicators")).toByteArray();
    if (!payload.isEmpty()) {
        const auto document = QJsonDocument::fromJson(payload);
        if (document.isArray()) {
            for (const auto& value : document.array()) {
                const auto object = value.toObject();
                const auto kind =
                    indicatorKindFromId(object.value(QStringLiteral("kind")).toString());
                if (!kind) {
                    continue;
                }
                const auto control = std::ranges::find(
                    indicatorControls_,
                    *kind,
                    &IndicatorControl::kind);
                if (control == indicatorControls_.end()) {
                    continue;
                }
                control->enabledItem->setCheckState(
                    object.value(QStringLiteral("enabled")).toBool()
                        ? Qt::Checked
                        : Qt::Unchecked);
                if (control->periodOrFast) {
                    const auto fallback = control->periodOrFast->value();
                    control->periodOrFast->setValue(
                        object.value(QStringLiteral("periodOrFast"))
                            .toInt(fallback));
                }
                if (control->slow) {
                    control->slow->setValue(
                        object.value(QStringLiteral("slow"))
                            .toInt(control->slow->value()));
                }
                if (control->signal) {
                    control->signal->setValue(
                        object.value(QStringLiteral("signal"))
                            .toInt(control->signal->value()));
                }
                applied = true;
            }
        }
    }

    if (!applied) {
        const auto legacyKind = static_cast<IndicatorKind>(
            settings.value(
                        QStringLiteral("chart/indicator"),
                        static_cast<int>(IndicatorKind::SimpleMovingAverage))
                .toInt());
        for (auto& control : indicatorControls_) {
            control.enabledItem->setCheckState(
                control.kind == legacyKind ? Qt::Checked : Qt::Unchecked);
        }
    }
}

void MainWindow::saveSettings() const {
    QSettings settings(
        QString::fromLatin1(kOrganization),
        QString::fromLatin1(kApplication));
    settings.setValue(QStringLiteral("window/geometry"), saveGeometry());
    settings.setValue(QStringLiteral("window/state"), saveState());
    settings.setValue(QStringLiteral("chart/symbol"), activeSymbol());
    settings.setValue(
        QStringLiteral("chart/timeframe"),
        timeframeSelector_->currentData().toInt());
    settings.setValue(
        QStringLiteral("chart/style"),
        styleSelector_->currentData().toString());
    settings.setValue(
        QStringLiteral("chart/priceScale"),
        scaleSelector_->currentData().toString());
    settings.setValue(
        QStringLiteral("chart/darkTheme"),
        darkThemeAction_->isChecked());
    settings.setValue(
        QStringLiteral("watchlists/json"),
        serializeWatchlists(watchlists_));
    settings.setValue(
        QStringLiteral("research/workspace"),
        serializeResearchWorkspace(researchWorkspace_));
    settings.setValue(
        QStringLiteral("margin/longMarketValue"),
        marginLongValueInput_->value());
    settings.setValue(
        QStringLiteral("margin/debit"),
        marginDebitInput_->value());
    settings.setValue(
        QStringLiteral("margin/otherEquity"),
        marginOtherEquityInput_->value());
    settings.setValue(
        QStringLiteral("margin/maintenancePercent"),
        marginMaintenanceInput_->value());
    settings.setValue(
        QStringLiteral("margin/stressPercent"),
        marginStressInput_->value());

    QJsonArray indicators;
    for (const auto& control : indicatorControls_) {
        const auto kindId = indicatorId(control.kind);
        indicators.append(QJsonObject{
            {
                QStringLiteral("kind"),
                QString::fromLatin1(
                    kindId.data(),
                    static_cast<qsizetype>(kindId.size())),
            },
            {
                QStringLiteral("enabled"),
                control.enabledItem->checkState() == Qt::Checked,
            },
            {
                QStringLiteral("periodOrFast"),
                control.periodOrFast ? control.periodOrFast->value() : 0,
            },
            {
                QStringLiteral("slow"),
                control.slow ? control.slow->value() : 0,
            },
            {
                QStringLiteral("signal"),
                control.signal ? control.signal->value() : 0,
            },
        });
    }
    settings.setValue(
        QStringLiteral("chart/indicators"),
        QJsonDocument(indicators).toJson(QJsonDocument::Compact));
    if (strategyLab_) {
        strategyLab_->saveSettings(settings);
    }
}

void MainWindow::saveSettingsNow() const {
    if (settingsEnabled_ && !restoringSettings_) {
        saveSettings();
    }
}

void MainWindow::reloadActiveSource() {
    if (!watchlist_ || !watchlist_->currentItem()) {
        return;
    }
    if (onlineDataEnabled_) {
        loadMarketData();
    } else {
        loadDemo();
    }
}

void MainWindow::loadMarketData() {
    if (!onlineDataEnabled_ || !watchlist_ || !timeframeSelector_ ||
        !watchlist_->currentItem()) {
        return;
    }

    refreshTimer_->stop();
    const auto symbol = activeSymbol();
    const auto timeframe = activeTimeframe();
    const auto timeframeText = activeTimeframeLabel();
    showLoadingDataStatus(symbol, timeframeText);
    sourceLabel_->setToolTip(
        marketDataClient_->hasTwelveDataKey()
            ? tr("Yahoo Finance is primary; Twelve Data is the configured fallback.")
            : tr("Yahoo Finance is primary. Set TWELVE_DATA_API_KEY to enable fallback."));
    statusBar()->showMessage(
        tr("%1 · %2 · requesting market data…").arg(symbol, timeframeText));

    marketDataClient_->fetch(
        symbol,
        timeframe,
        [this, symbol, timeframe, timeframeText](MarketDataResult result) mutable {
            if (symbol != activeSymbol() || timeframe != activeTimeframe()) {
                return;
            }

            if (result.ok()) {
                const auto barCount = result.bars.size();
                const auto source = result.source;
                const auto metadata = result.metadata;
                if (applySeries(
                        symbol,
                        timeframeText,
                        source,
                        std::move(result.bars),
                        metadata,
                        tr("Ready"),
                        source == QStringLiteral("Yahoo Finance")
                            ? tr("Unofficial Yahoo Finance chart endpoint.")
                            : tr("Twelve Data fallback response."))) {
                    setStatus(source, barCount, symbol, timeframeText);
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
    if (!watchlist_ || !timeframeSelector_ || !watchlist_->currentItem()) {
        return;
    }

    const auto symbol = activeSymbol();
    const auto timeframe = activeTimeframe();
    const auto now = QDateTime::currentSecsSinceEpoch();
    auto bars = DemoDataSource::generate(
        symbol.toStdString(),
        timeframe,
        600,
        now);
    const auto barCount = bars.size();
    MarketDataMetadata metadata{
        .deliveryMode = DataDeliveryMode::Synthetic,
        .exchange = tr("Synthetic"),
        .timezone = QStringLiteral("UTC"),
        .instrumentType = tr("Demo series"),
        .interval = activeTimeframeLabel(),
        .retrievedAtUtc = now,
    };
    if (applySeries(
            symbol,
            activeTimeframeLabel(),
            source,
            std::move(bars),
            metadata,
            source == tr("Offline fallback")
                ? tr("Provider unavailable; showing synthetic fallback")
                : tr("Synthetic demo"),
            detail)) {
        setStatus(source, barCount, symbol, activeTimeframeLabel());
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
    Bars bars,
    MarketDataMetadata metadata,
    const QString& lifecycle,
    const QString& detail) {
    if (!chartView_->bridge()->setSeries(
            symbol,
            timeframe,
            source,
            bars)) {
        return false;
    }
    currentSeriesSymbol_ = normalizeWatchlistSymbol(symbol);
    currentBars_ = std::move(bars);
    updateTechnicalAnalysis();
    updateDataStatus(source, metadata, currentBars_, lifecycle, detail);
    if (strategyLab_) {
        strategyLab_->setCurrentSeries(
            currentSeriesSymbol_,
            activeTimeframe(),
            source,
            currentBars_,
            metadata);
    }
    refreshResearchDisplay();
    recalculateMarginRisk();
    return true;
}

void MainWindow::showReplaySeries(Bars bars) {
    if (bars.empty() || currentBars_.empty()) {
        return;
    }
    if (!chartView_->bridge()->setSeries(
            currentSeriesSymbol_,
            activeTimeframeLabel(),
            tr("Replay · %1").arg(dataStatus_.source),
            bars)) {
        return;
    }
    try {
        chartView_->bridge()->setIndicators(
            calculateIndicators(bars, activeIndicatorSpecs()));
    } catch (const std::exception& error) {
        chartView_->bridge()->setIndicators({});
        statusBar()->showMessage(
            tr("Replay indicator calculation failed: %1")
                .arg(QString::fromUtf8(error.what())),
            8'000);
    }
    statusBar()->showMessage(
        tr("Replay · %1 · %2 of %3 bars")
            .arg(currentSeriesSymbol_)
            .arg(bars.size())
            .arg(currentBars_.size()));
}

void MainWindow::restoreFullSeries() {
    if (currentBars_.empty()) {
        return;
    }
    chartView_->bridge()->setSeries(
        currentSeriesSymbol_,
        activeTimeframeLabel(),
        dataStatus_.source,
        currentBars_);
    updateTechnicalAnalysis();
    statusBar()->showMessage(tr("Full series restored."), 5'000);
}

void MainWindow::updateTechnicalAnalysis() {
    if (!chartView_ || currentBars_.empty()) {
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
        auto calculations =
            calculateIndicators(currentBars_, activeIndicatorSpecs());
        QStringList summaries;
        for (const auto& calculation : calculations) {
            const auto label = QString::fromStdString(calculation.label);
            if (calculation.primary.empty()) {
                summaries.push_back(tr("%1 · warming up").arg(label));
                continue;
            }
            if (calculation.kind ==
                IndicatorKind::MovingAverageConvergenceDivergence) {
                if (calculation.secondary.empty() ||
                    calculation.histogram.empty()) {
                    summaries.push_back(
                        tr("%1 · %2 · signal warming")
                            .arg(label)
                            .arg(formatPrice(
                                calculation.primary.back().value)));
                } else {
                    summaries.push_back(
                        tr("%1 · %2 / %3 / H %4")
                            .arg(label)
                            .arg(formatPrice(
                                calculation.primary.back().value))
                            .arg(formatPrice(
                                calculation.secondary.back().value))
                            .arg(formatPrice(
                                calculation.histogram.back().value)));
                }
            } else if (
                calculation.kind ==
                IndicatorKind::VolumeSimpleMovingAverage) {
                summaries.push_back(
                    tr("%1 · %2")
                        .arg(label)
                        .arg(formatVolume(
                            calculation.primary.back().value)));
            } else {
                summaries.push_back(
                    tr("%1 · %2")
                        .arg(label)
                        .arg(formatPrice(
                            calculation.primary.back().value)));
            }
        }
        indicatorValueLabel_->setText(
            summaries.isEmpty()
                ? tr("None enabled")
                : summaries.join(u'\n'));
        indicatorValueLabel_->setToolTip(
            tr("Calculated locally from the loaded OHLCV bars. "
               "These values are not forecasts or trading recommendations."));
        chartView_->bridge()->setIndicators(std::move(calculations));
    } catch (const std::exception& error) {
        chartView_->bridge()->setIndicators({});
        indicatorValueLabel_->setText(tr("Calculation unavailable"));
        indicatorValueLabel_->setToolTip(QString::fromUtf8(error.what()));
    }
}

void MainWindow::handleIndicatorConfigurationChanged() {
    if (restoringSettings_) {
        return;
    }
    updateTechnicalAnalysis();
    saveSettingsNow();
}

std::vector<IndicatorSpec> MainWindow::activeIndicatorSpecs() const {
    std::vector<IndicatorSpec> specs;
    for (const auto& control : indicatorControls_) {
        if (control.enabledItem->checkState() != Qt::Checked) {
            continue;
        }
        auto spec = defaultIndicatorSpec(control.kind);
        if (control.kind ==
            IndicatorKind::MovingAverageConvergenceDivergence) {
            spec.fastPeriod =
                static_cast<std::uint32_t>(control.periodOrFast->value());
            spec.slowPeriod =
                static_cast<std::uint32_t>(control.slow->value());
            spec.signalPeriod =
                static_cast<std::uint32_t>(control.signal->value());
        } else if (control.periodOrFast) {
            spec.period =
                static_cast<std::uint32_t>(control.periodOrFast->value());
        }
        specs.push_back(spec);
    }
    return specs;
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
    QSettings settings(
        QString::fromLatin1(kOrganization),
        QString::fromLatin1(kApplication));
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

    if (settingsEnabled_) {
        settings.setValue(
            QStringLiteral("files/lastDirectory"),
            QFileInfo(path).absolutePath());
    }
    const auto symbol = QFileInfo(path).completeBaseName().toUpper();
    const auto barCount = result.bars.size();
    const MarketDataMetadata metadata{
        .deliveryMode = DataDeliveryMode::LocalFile,
        .exchange = tr("Local file"),
        .timezone = QStringLiteral("UTC"),
        .instrumentType = tr("Imported OHLCV"),
        .interval = tr("CSV"),
        .retrievedAtUtc = QDateTime::currentSecsSinceEpoch(),
    };
    if (applySeries(
            symbol,
            tr("CSV"),
            tr("Local CSV"),
            result.bars,
            metadata,
            tr("Ready"),
            path)) {
        setStatus(
            tr("Local CSV: %1").arg(QFileInfo(path).fileName()),
            barCount,
            symbol,
            tr("CSV"));
    }
}

NamedWatchlist* MainWindow::activeWatchlist() noexcept {
    const auto found = std::ranges::find(
        watchlists_.lists,
        watchlists_.activeListId,
        &NamedWatchlist::id);
    return found == watchlists_.lists.end() ? nullptr : &*found;
}

const NamedWatchlist* MainWindow::activeWatchlist() const noexcept {
    const auto found = std::ranges::find(
        watchlists_.lists,
        watchlists_.activeListId,
        &NamedWatchlist::id);
    return found == watchlists_.lists.end() ? nullptr : &*found;
}

std::optional<std::size_t>
MainWindow::activeWatchlistEntryIndex() const noexcept {
    if (!watchlist_ || !watchlist_->currentItem()) {
        return std::nullopt;
    }
    bool ok = false;
    const auto index =
        watchlist_->currentItem()->data(Qt::UserRole).toULongLong(&ok);
    const auto* list = activeWatchlist();
    if (!ok || !list || index >= list->entries.size()) {
        return std::nullopt;
    }
    return static_cast<std::size_t>(index);
}

void MainWindow::refreshWatchlistSelector() {
    const QSignalBlocker blocker(namedWatchlistSelector_);
    namedWatchlistSelector_->clear();
    for (const auto& watchlist : watchlists_.lists) {
        namedWatchlistSelector_->addItem(watchlist.name, watchlist.id);
    }
    auto index =
        namedWatchlistSelector_->findData(watchlists_.activeListId);
    if (index < 0 && namedWatchlistSelector_->count() > 0) {
        index = 0;
        watchlists_.activeListId =
            namedWatchlistSelector_->itemData(0).toString();
    }
    namedWatchlistSelector_->setCurrentIndex(index);
}

void MainWindow::refreshWatchlistEntries(const QString& preferredSymbol) {
    const QSignalBlocker listBlocker(watchlist_);
    const QSignalBlocker sortBlocker(watchlistSortSelector_);
    watchlist_->clear();
    const auto* list = activeWatchlist();
    if (!list) {
        watchlistNoteInput_->clear();
        refreshStrategyWatchlist();
        refreshResearchDisplay();
        recalculateMarginRisk();
        return;
    }
    const auto sortIndex = watchlistSortSelector_->findData(
        static_cast<int>(list->sort));
    watchlistSortSelector_->setCurrentIndex(sortIndex);

    auto selectedRow = -1;
    const auto normalizedPreferred =
        normalizeWatchlistSymbol(preferredSymbol);
    for (const auto entryIndex : watchlistDisplayOrder(*list)) {
        const auto& entry = list->entries[entryIndex];
        auto* item = new QListWidgetItem(entry.symbol, watchlist_);
        item->setData(
            Qt::UserRole,
            static_cast<qulonglong>(entryIndex));
        if (!entry.note.isEmpty()) {
            item->setToolTip(entry.note);
        }
        if (entry.symbol == normalizedPreferred) {
            selectedRow = watchlist_->row(item);
        }
    }
    if (selectedRow < 0 && watchlist_->count() > 0) {
        selectedRow = 0;
    }
    watchlist_->setCurrentRow(selectedRow);
    const auto entryIndex = activeWatchlistEntryIndex();
    watchlistNoteInput_->setText(
        entryIndex ? list->entries[*entryIndex].note : QString{});
    refreshStrategyWatchlist();
    refreshResearchDisplay();
    recalculateMarginRisk();
}

void MainWindow::refreshStrategyWatchlist() {
    if (!strategyLab_) {
        return;
    }
    QStringList symbols;
    if (const auto* list = activeWatchlist()) {
        symbols.reserve(static_cast<qsizetype>(list->entries.size()));
        for (const auto& entry : list->entries) {
            symbols.push_back(entry.symbol);
        }
    }
    strategyLab_->setWatchlistSymbols(std::move(symbols));
}

void MainWindow::selectNamedWatchlist(const int index) {
    if (index < 0) {
        return;
    }
    watchlists_.activeListId =
        namedWatchlistSelector_->itemData(index).toString();
    refreshWatchlistEntries();
    if (!restoringSettings_ && watchlist_->currentItem()) {
        reloadActiveSource();
        saveSettingsNow();
    }
}

void MainWindow::createNamedWatchlist() {
    bool accepted = false;
    const auto name = QInputDialog::getText(
                          this,
                          tr("New watchlist"),
                          tr("Watchlist name"),
                          QLineEdit::Normal,
                          {},
                          &accepted)
                          .trimmed();
    if (!accepted || name.isEmpty()) {
        return;
    }
    NamedWatchlist watchlist{
        .id = QUuid::createUuid().toString(QUuid::WithoutBraces),
        .name = name.left(64),
    };
    if (watchlist_->currentItem()) {
        watchlist.entries.push_back({.symbol = activeSymbol()});
    }
    watchlists_.lists.push_back(std::move(watchlist));
    watchlists_.activeListId = watchlists_.lists.back().id;
    refreshWatchlistSelector();
    refreshWatchlistEntries();
    saveSettingsNow();
    reloadActiveSource();
}

void MainWindow::renameNamedWatchlist() {
    auto* watchlist = activeWatchlist();
    if (!watchlist) {
        return;
    }
    bool accepted = false;
    const auto name = QInputDialog::getText(
                          this,
                          tr("Rename watchlist"),
                          tr("Watchlist name"),
                          QLineEdit::Normal,
                          watchlist->name,
                          &accepted)
                          .trimmed();
    if (!accepted || name.isEmpty()) {
        return;
    }
    watchlist->name = name.left(64);
    refreshWatchlistSelector();
    saveSettingsNow();
}

void MainWindow::deleteNamedWatchlist() {
    if (watchlists_.lists.size() <= 1) {
        QMessageBox::information(
            this,
            tr("Delete watchlist"),
            tr("At least one watchlist must remain."));
        return;
    }
    const auto* watchlist = activeWatchlist();
    if (!watchlist ||
        QMessageBox::question(
            this,
            tr("Delete watchlist"),
            tr("Delete “%1”? This does not delete chart data.")
                .arg(watchlist->name)) != QMessageBox::Yes) {
        return;
    }
    const auto id = watchlist->id;
    std::erase_if(watchlists_.lists, [&id](const auto& candidate) {
        return candidate.id == id;
    });
    watchlists_.activeListId = watchlists_.lists.front().id;
    refreshWatchlistSelector();
    refreshWatchlistEntries();
    saveSettingsNow();
    reloadActiveSource();
}

void MainWindow::addWatchlistSymbol() {
    auto* watchlist = activeWatchlist();
    if (!watchlist) {
        return;
    }
    const auto symbol =
        normalizeWatchlistSymbol(watchlistSymbolInput_->text());
    NamedWatchlist candidate = *watchlist;
    candidate.entries.push_back({.symbol = symbol});
    if (const auto error = validateWatchlist(candidate); !error.isEmpty()) {
        QMessageBox::warning(this, tr("Add symbol"), error);
        return;
    }
    watchlist->entries.push_back({.symbol = symbol});
    watchlistSymbolInput_->clear();
    refreshWatchlistEntries(symbol);
    saveSettingsNow();
    reloadActiveSource();
}

void MainWindow::removeWatchlistSymbol() {
    auto* watchlist = activeWatchlist();
    const auto entryIndex = activeWatchlistEntryIndex();
    if (!watchlist || !entryIndex) {
        return;
    }
    watchlist->entries.erase(
        watchlist->entries.begin() +
        static_cast<std::ptrdiff_t>(*entryIndex));
    refreshWatchlistEntries();
    saveSettingsNow();
    if (watchlist_->currentItem()) {
        reloadActiveSource();
    } else {
        marketDataClient_->cancel();
        refreshTimer_->stop();
        sourceLabel_->setText(tr("No symbol selected"));
    }
}

void MainWindow::moveWatchlistSymbol(const int direction) {
    auto* watchlist = activeWatchlist();
    const auto entryIndex = activeWatchlistEntryIndex();
    if (!watchlist || !entryIndex || watchlist->sort != WatchlistSort::Manual) {
        return;
    }
    const auto current = static_cast<std::ptrdiff_t>(*entryIndex);
    const auto target = current + direction;
    if (target < 0 ||
        target >= static_cast<std::ptrdiff_t>(watchlist->entries.size())) {
        return;
    }
    const auto symbol = watchlist->entries[*entryIndex].symbol;
    std::swap(
        watchlist->entries[static_cast<std::size_t>(current)],
        watchlist->entries[static_cast<std::size_t>(target)]);
    refreshWatchlistEntries(symbol);
    saveSettingsNow();
}

void MainWindow::updateWatchlistNote() {
    auto* watchlist = activeWatchlist();
    const auto entryIndex = activeWatchlistEntryIndex();
    if (!watchlist || !entryIndex) {
        return;
    }
    watchlist->entries[*entryIndex].note =
        watchlistNoteInput_->text().trimmed().left(512);
    if (watchlist_->currentItem()) {
        watchlist_->currentItem()->setToolTip(
            watchlist->entries[*entryIndex].note);
    }
    saveSettingsNow();
}

void MainWindow::updateWatchlistSort(const int index) {
    auto* watchlist = activeWatchlist();
    if (!watchlist || index < 0) {
        return;
    }
    const auto symbol = activeSymbol();
    watchlist->sort = static_cast<WatchlistSort>(
        watchlistSortSelector_->itemData(index).toInt());
    refreshWatchlistEntries(symbol);
    saveSettingsNow();
}

void MainWindow::importWatchlist() {
    auto* watchlist = activeWatchlist();
    if (!watchlist) {
        return;
    }
    const auto path = QFileDialog::getOpenFileName(
        this,
        tr("Import watchlist CSV"),
        {},
        tr("CSV files (*.csv);;All files (*.*)"));
    if (path.isEmpty()) {
        return;
    }
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly) || file.size() > kMaximumWatchlistFileBytes) {
        QMessageBox::critical(
            this,
            tr("Watchlist import failed"),
            tr("The file could not be read or exceeds 2 MiB."));
        return;
    }
    const auto imported = importWatchlistCsv(file.readAll());
    if (!imported.ok()) {
        QMessageBox::critical(
            this,
            tr("Watchlist import failed"),
            imported.error);
        return;
    }

    auto added = std::size_t{};
    for (const auto& entry : imported.entries) {
        if (watchlist->entries.size() >= 500) {
            break;
        }
        if (std::ranges::find(
                watchlist->entries,
                entry.symbol,
                &WatchlistEntry::symbol) == watchlist->entries.end()) {
            watchlist->entries.push_back(entry);
            ++added;
        }
    }
    refreshWatchlistEntries(
        added > 0 ? imported.entries.front().symbol : activeSymbol());
    saveSettingsNow();
    if (added > 0) {
        reloadActiveSource();
    }
    statusBar()->showMessage(
        tr("Imported %1 symbol(s); %2 CSV line(s) were rejected.")
            .arg(static_cast<qulonglong>(added))
            .arg(static_cast<qulonglong>(imported.rejectedLines.size())),
        8'000);
}

void MainWindow::exportWatchlist() {
    const auto* watchlist = activeWatchlist();
    if (!watchlist) {
        return;
    }
    const auto path = QFileDialog::getSaveFileName(
        this,
        tr("Export active watchlist"),
        watchlist->name + QStringLiteral(".csv"),
        tr("CSV files (*.csv)"));
    if (path.isEmpty()) {
        return;
    }
    QSaveFile file(path);
    const auto output = exportWatchlistCsv(*watchlist);
    if (!file.open(QIODevice::WriteOnly) ||
        file.write(output) != output.size() ||
        !file.commit()) {
        QMessageBox::critical(
            this,
            tr("Watchlist export failed"),
            tr("The destination file could not be written."));
        return;
    }
    statusBar()->showMessage(tr("Watchlist exported."), 5'000);
}

void MainWindow::refreshResearchFromProvider() {
    if (!onlineDataEnabled_ || !researchClient_ || !researchRefreshButton_) {
        return;
    }
    const auto symbol = activeSymbol();
    researchRefreshButton_->setEnabled(false);
    researchRefreshButton_->setText(tr("Refreshing %1…").arg(symbol));
    researchProviderLabel_->setText(tr("Requesting Alpha Vantage…"));
    researchClient_->fetch(
        symbol,
        [this, symbol](AlphaVantageResearchResult result) mutable {
            if (researchRefreshButton_) {
                researchRefreshButton_->setEnabled(onlineDataEnabled_);
                researchRefreshButton_->setText(
                    tr("Refresh Alpha Vantage research"));
            }
            if (symbol != activeSymbol()) {
                return;
            }
            if (!result.ok()) {
                refreshResearchDisplay();
                statusBar()->showMessage(result.error, 15'000);
                QMessageBox::warning(
                    this,
                    tr("Research refresh failed"),
                    result.error);
                return;
            }
            mergeResearchResult(std::move(result));
        });
}

void MainWindow::mergeResearchResult(AlphaVantageResearchResult result) {
    const auto symbol = normalizeWatchlistSymbol(result.snapshot.symbol);
    const auto provider = result.snapshot.provider;
    std::erase_if(
        researchWorkspace_.companySnapshots,
        [&](const CompanyResearchSnapshot& snapshot) {
            return normalizeWatchlistSymbol(snapshot.symbol) == symbol &&
                   snapshot.provider.compare(provider, Qt::CaseInsensitive) == 0;
        });
    if (researchWorkspace_.companySnapshots.size() >=
        ResearchWorkspace::maximumCompanySnapshots) {
        const auto oldest = std::ranges::min_element(
            researchWorkspace_.companySnapshots,
            {},
            &CompanyResearchSnapshot::asOfUtc);
        researchWorkspace_.companySnapshots.erase(oldest);
    }
    researchWorkspace_.companySnapshots.push_back(std::move(result.snapshot));

    std::erase_if(
        researchWorkspace_.events,
        [&](const ResearchEvent& event) {
            return normalizeWatchlistSymbol(event.symbol) == symbol &&
                   event.source.compare(provider, Qt::CaseInsensitive) == 0 &&
                   event.id.startsWith(QStringLiteral("alpha-vantage-")) &&
                   (event.type == ResearchEventType::ExDividend ||
                    event.type == ResearchEventType::DividendPayment ||
                    (result.earningsCalendarUpdated &&
                     event.type == ResearchEventType::Earnings));
        });
    const auto eventCapacity =
        ResearchWorkspace::maximumEvents - researchWorkspace_.events.size();
    if (result.events.size() > eventCapacity) {
        result.events.resize(eventCapacity);
        if (!result.warning.isEmpty()) {
            result.warning += QStringLiteral(" ");
        }
        result.warning +=
            tr("Some provider events were omitted because the local event "
               "workspace reached its safety limit.");
    }
    researchWorkspace_.events.insert(
        researchWorkspace_.events.end(),
        std::make_move_iterator(result.events.begin()),
        std::make_move_iterator(result.events.end()));
    refreshResearchDisplay();
    saveSettingsNow();

    const auto message = result.warning.isEmpty()
                             ? tr("%1 research refreshed from %2.")
                                   .arg(symbol, provider)
                             : tr("%1 overview refreshed; calendar warning: %2")
                                   .arg(symbol, result.warning);
    statusBar()->showMessage(message, 15'000);
}

void MainWindow::refreshResearchDisplay() {
    if (!researchProviderLabel_ || !targetEstimateTable_ ||
        !researchEventTable_) {
        return;
    }
    const auto symbol = activeSymbol();
    const CompanyResearchSnapshot* snapshot = nullptr;
    for (const auto& candidate : researchWorkspace_.companySnapshots) {
        if (normalizeWatchlistSymbol(candidate.symbol) == symbol &&
            (!snapshot || candidate.asOfUtc > snapshot->asOfUtc)) {
            snapshot = &candidate;
        }
    }

    if (!snapshot) {
        researchProviderLabel_->setText(
            researchClient_ && researchClient_->hasApiKey()
                ? tr("Not loaded · use Refresh")
                : tr("Not loaded · set ALPHA_VANTAGE_API_KEY for provider data"));
        researchCompanyLabel_->setText(QStringLiteral("—"));
        researchFundamentalsLabel_->setText(QStringLiteral("—"));
        researchProviderTargetLabel_->setText(QStringLiteral("—"));
        researchRatingsLabel_->setText(QStringLiteral("—"));
    } else {
        researchProviderLabel_->setText(
            tr("%1 · %2")
                .arg(snapshot->provider, utcTimestamp(snapshot->asOfUtc)));
        const auto identity = nonEmptyParts(
            {snapshot->name, snapshot->exchange, snapshot->sector});
        researchCompanyLabel_->setText(
            identity.isEmpty() ? symbol : identity.join(QStringLiteral(" · ")));
        researchFundamentalsLabel_->setText(
            tr("MCap %1 · EPS %2 · P/E %3 · Fwd P/E %4 · Beta %5 · "
               "52w %6–%7")
                .arg(
                    compactAmount(snapshot->marketCapitalization),
                    optionalNumber(snapshot->eps),
                    optionalNumber(snapshot->peRatio),
                    optionalNumber(snapshot->forwardPe),
                    optionalNumber(snapshot->beta),
                    optionalNumber(snapshot->week52Low),
                    optionalNumber(snapshot->week52High)));
        if (snapshot->analystTargetPrice) {
            auto targetText =
                tr("%1 %2 · aggregate")
                    .arg(
                        formatPrice(*snapshot->analystTargetPrice),
                        snapshot->currency);
            if (!currentBars_.empty() && currentSeriesSymbol_ == symbol &&
                currentBars_.back().close > 0.0 &&
                !snapshot->currency.isEmpty() &&
                snapshot->currency.compare(
                    dataStatus_.metadata.currency,
                    Qt::CaseInsensitive) == 0) {
                const auto upside =
                    (*snapshot->analystTargetPrice / currentBars_.back().close -
                     1.0) *
                    100.0;
                targetText += tr(" · %1% vs loaded close")
                                  .arg(QLocale::system().toString(
                                      upside,
                                      'f',
                                      1));
            }
            researchProviderTargetLabel_->setText(targetText);
        } else {
            researchProviderTargetLabel_->setText(QStringLiteral("—"));
        }
        const auto& ratings = snapshot->ratings;
        researchRatingsLabel_->setText(
            ratings.total() > 0
                ? tr("Strong buy %1 · Buy %2 · Hold %3 · Sell %4 · "
                     "Strong sell %5 (n=%6)")
                      .arg(ratings.strongBuy)
                      .arg(ratings.buy)
                      .arg(ratings.hold)
                      .arg(ratings.sell)
                      .arg(ratings.strongSell)
                      .arg(ratings.total())
                : QStringLiteral("—"));
    }

    std::vector<const AnalystTargetEstimate*> visibleTargets;
    QStringList currencies;
    for (const auto& estimate : researchWorkspace_.targetEstimates) {
        if (estimate.scope == TargetEstimateScope::Organization &&
            normalizeWatchlistSymbol(estimate.symbol) == symbol &&
            validateTargetEstimate(estimate).isEmpty()) {
            visibleTargets.push_back(&estimate);
            if (!currencies.contains(
                    estimate.currency,
                    Qt::CaseInsensitive)) {
                currencies.push_back(estimate.currency.toUpper());
            }
        }
    }
    std::ranges::sort(
        visibleTargets,
        [](const auto* left, const auto* right) {
            if (left->publishedDate != right->publishedDate) {
                return left->publishedDate > right->publishedDate;
            }
            return left->organization.compare(
                       right->organization,
                       Qt::CaseInsensitive) < 0;
        });
    targetEstimateTable_->setRowCount(
        static_cast<int>(visibleTargets.size()));
    for (int row = 0; row < static_cast<int>(visibleTargets.size()); ++row) {
        const auto& estimate = *visibleTargets[static_cast<std::size_t>(row)];
        const std::array values{
            estimate.organization,
            formatPrice(estimate.targetPrice),
            estimate.currency,
            estimate.publishedDate.toString(Qt::ISODate),
            estimate.rating,
        };
        for (int column = 0; column < static_cast<int>(values.size());
             ++column) {
            auto* item = new QTableWidgetItem(
                values[static_cast<std::size_t>(column)]);
            if (column == 0) {
                item->setData(Qt::UserRole, estimate.id);
            }
            if (!estimate.sourceUrl.isEmpty()) {
                item->setToolTip(estimate.sourceUrl);
            }
            targetEstimateTable_->setItem(row, column, item);
        }
    }

    QStringList summaries;
    for (const auto& currency : currencies) {
        const auto summary = summarizeOrganizationTargets(
            researchWorkspace_.targetEstimates,
            symbol,
            currency);
        if (summary) {
            summaries.push_back(
                tr("%1: mean %2 · median %3 · range %4–%5 · n=%6")
                    .arg(
                        currency,
                        formatPrice(summary->mean),
                        formatPrice(summary->median),
                        formatPrice(summary->minimum),
                        formatPrice(summary->maximum))
                    .arg(
                        static_cast<qulonglong>(
                            summary->organizationCount)));
        }
    }
    researchManualConsensusLabel_->setText(
        summaries.isEmpty() ? QStringLiteral("—")
                            : summaries.join(QStringLiteral("\n")));

    std::vector<const ResearchEvent*> visibleEvents;
    for (const auto& event : researchWorkspace_.events) {
        const auto eventSymbol = normalizeWatchlistSymbol(event.symbol);
        if ((eventSymbol.isEmpty() || eventSymbol == symbol) &&
            validateResearchEvent(event).isEmpty()) {
            visibleEvents.push_back(&event);
        }
    }
    std::ranges::sort(
        visibleEvents,
        [](const auto* left, const auto* right) {
            if (left->scheduledDate != right->scheduledDate) {
                return left->scheduledDate < right->scheduledDate;
            }
            return left->title.compare(right->title, Qt::CaseInsensitive) < 0;
        });
    researchEventTable_->setRowCount(static_cast<int>(visibleEvents.size()));
    for (int row = 0; row < static_cast<int>(visibleEvents.size()); ++row) {
        const auto& event = *visibleEvents[static_cast<std::size_t>(row)];
        const auto date = event.timeOfDay.isEmpty()
                              ? event.scheduledDate.toString(Qt::ISODate)
                              : tr("%1 · %2")
                                    .arg(
                                        event.scheduledDate.toString(
                                            Qt::ISODate),
                                        event.timeOfDay);
        const std::array values{
            date,
            researchEventTypeLabel(event.type),
            confidenceLabel(event.confidence),
            event.title,
            event.source,
        };
        for (int column = 0; column < static_cast<int>(values.size());
             ++column) {
            auto* item = new QTableWidgetItem(
                values[static_cast<std::size_t>(column)]);
            if (column == 0) {
                item->setData(Qt::UserRole, event.id);
            }
            if (!event.detail.isEmpty()) {
                item->setToolTip(event.detail);
            }
            researchEventTable_->setItem(row, column, item);
        }
    }
    const auto today = QDate::currentDate();
    const auto next = std::ranges::find_if(
        visibleEvents,
        [&](const ResearchEvent* event) {
            return event->scheduledDate >= today;
        });
    researchNextEventLabel_->setText(
        next == visibleEvents.end()
            ? QStringLiteral("—")
            : tr("%1 · %2 · %3 · %4")
                  .arg(
                      (*next)->scheduledDate.toString(Qt::ISODate),
                      researchEventTypeLabel((*next)->type),
                      (*next)->title,
                      confidenceLabel((*next)->confidence)));
}

void MainWindow::addTargetEstimate() {
    if (researchWorkspace_.targetEstimates.size() >=
        ResearchWorkspace::maximumTargetEstimates) {
        QMessageBox::warning(
            this,
            tr("Target workspace full"),
            tr("Remove target records before adding another."));
        return;
    }
    QDialog dialog(this);
    dialog.setWindowTitle(tr("Add organization target"));
    auto* layout = new QFormLayout(&dialog);
    auto* symbol = new QLabel(activeSymbol(), &dialog);
    auto* organization = new QLineEdit(&dialog);
    organization->setMaxLength(120);
    auto* target = new QDoubleSpinBox(&dialog);
    target->setDecimals(4);
    target->setRange(0.0001, 1'000'000'000.0);
    auto* currency = new QLineEdit(QStringLiteral("USD"), &dialog);
    currency->setMaxLength(12);
    auto* published = new QDateEdit(QDate::currentDate(), &dialog);
    published->setCalendarPopup(true);
    published->setDisplayFormat(QStringLiteral("yyyy-MM-dd"));
    auto* rating = new QLineEdit(&dialog);
    rating->setMaxLength(80);
    auto* sourceUrl = new QLineEdit(&dialog);
    sourceUrl->setMaxLength(1'024);
    sourceUrl->setPlaceholderText(tr("Optional http(s) source URL"));
    layout->addRow(tr("Symbol"), symbol);
    layout->addRow(tr("Organization"), organization);
    layout->addRow(tr("Target price"), target);
    layout->addRow(tr("Currency"), currency);
    layout->addRow(tr("Published date"), published);
    layout->addRow(tr("Rating"), rating);
    layout->addRow(tr("Source URL"), sourceUrl);
    auto* buttons = new QDialogButtonBox(
        QDialogButtonBox::Save | QDialogButtonBox::Cancel,
        &dialog);
    layout->addRow(buttons);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    connect(
        buttons,
        &QDialogButtonBox::accepted,
        &dialog,
        [&] {
            const AnalystTargetEstimate candidate{
                .id = QStringLiteral("manual-%1").arg(
                    QUuid::createUuid().toString(QUuid::WithoutBraces)),
                .symbol = activeSymbol(),
                .organization = organization->text().trimmed(),
                .targetPrice = target->value(),
                .currency = currency->text().trimmed().toUpper(),
                .publishedDate = published->date(),
                .rating = rating->text().trimmed(),
                .sourceUrl = sourceUrl->text().trimmed(),
            };
            if (const auto error = validateTargetEstimate(candidate);
                !error.isEmpty()) {
                QMessageBox::warning(
                    &dialog,
                    tr("Invalid target estimate"),
                    error);
                return;
            }
            const auto duplicate = std::ranges::any_of(
                researchWorkspace_.targetEstimates,
                [&](const AnalystTargetEstimate& existing) {
                    return existing.scope ==
                               TargetEstimateScope::Organization &&
                           normalizeWatchlistSymbol(existing.symbol) ==
                               candidate.symbol &&
                           existing.organization.compare(
                               candidate.organization,
                               Qt::CaseInsensitive) == 0 &&
                           existing.currency.compare(
                               candidate.currency,
                               Qt::CaseInsensitive) == 0 &&
                           existing.publishedDate ==
                               candidate.publishedDate;
                });
            if (duplicate) {
                QMessageBox::warning(
                    &dialog,
                    tr("Duplicate target estimate"),
                    tr("That organization already has a target for this "
                       "symbol, currency, and date."));
                return;
            }
            researchWorkspace_.targetEstimates.push_back(candidate);
            dialog.accept();
        });
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }
    refreshResearchDisplay();
    saveSettingsNow();
}

void MainWindow::removeTargetEstimate() {
    const auto row = targetEstimateTable_->currentRow();
    const auto* item =
        row >= 0 ? targetEstimateTable_->item(row, 0) : nullptr;
    if (!item) {
        return;
    }
    const auto id = item->data(Qt::UserRole).toString();
    std::erase_if(
        researchWorkspace_.targetEstimates,
        [&](const AnalystTargetEstimate& estimate) {
            return estimate.id == id;
        });
    refreshResearchDisplay();
    saveSettingsNow();
}

void MainWindow::importTargetEstimates() {
    const auto path = QFileDialog::getOpenFileName(
        this,
        tr("Import organization targets"),
        {},
        tr("CSV files (*.csv);;All files (*)"));
    if (path.isEmpty()) {
        return;
    }
    const QFileInfo info(path);
    if (!info.exists() || info.size() < 0 ||
        info.size() > kMaximumResearchFileBytes) {
        QMessageBox::warning(
            this,
            tr("Target import failed"),
            tr("The file must exist and be no larger than 2 MiB."));
        return;
    }
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        QMessageBox::warning(
            this,
            tr("Target import failed"),
            tr("The selected file could not be read."));
        return;
    }
    const auto imported = importTargetEstimatesCsv(
        file.read(kMaximumResearchFileBytes + 1));
    if (!imported.ok()) {
        QMessageBox::warning(
            this,
            tr("Target import failed"),
            imported.error);
        return;
    }
    auto added = std::size_t{};
    auto duplicates = std::size_t{};
    auto capacitySkipped = std::size_t{};
    for (auto estimate : imported.estimates) {
        if (researchWorkspace_.targetEstimates.size() >=
            ResearchWorkspace::maximumTargetEstimates) {
            ++capacitySkipped;
            continue;
        }
        const auto duplicate = std::ranges::any_of(
            researchWorkspace_.targetEstimates,
            [&](const AnalystTargetEstimate& existing) {
                return existing.scope == TargetEstimateScope::Organization &&
                       normalizeWatchlistSymbol(existing.symbol) ==
                           normalizeWatchlistSymbol(estimate.symbol) &&
                       existing.organization.compare(
                           estimate.organization,
                           Qt::CaseInsensitive) == 0 &&
                       existing.currency.compare(
                           estimate.currency,
                           Qt::CaseInsensitive) == 0 &&
                       existing.publishedDate == estimate.publishedDate;
            });
        if (duplicate) {
            ++duplicates;
            continue;
        }
        estimate.id = QStringLiteral("import-%1").arg(
            QUuid::createUuid().toString(QUuid::WithoutBraces));
        researchWorkspace_.targetEstimates.push_back(std::move(estimate));
        ++added;
    }
    refreshResearchDisplay();
    saveSettingsNow();
    statusBar()->showMessage(
        tr("Imported %1 targets; %2 duplicate, %3 invalid, and %4 "
           "over-capacity rows skipped.")
            .arg(static_cast<qulonglong>(added))
            .arg(static_cast<qulonglong>(duplicates))
            .arg(
                static_cast<qulonglong>(
                    imported.rejectedLines.size()))
            .arg(static_cast<qulonglong>(capacitySkipped)),
        10'000);
}

void MainWindow::exportTargetEstimates() {
    std::vector<AnalystTargetEstimate> estimates;
    const auto symbol = activeSymbol();
    std::ranges::copy_if(
        researchWorkspace_.targetEstimates,
        std::back_inserter(estimates),
        [&](const AnalystTargetEstimate& estimate) {
            return estimate.scope == TargetEstimateScope::Organization &&
                   normalizeWatchlistSymbol(estimate.symbol) == symbol;
        });
    const auto path = QFileDialog::getSaveFileName(
        this,
        tr("Export organization targets"),
        symbol + QStringLiteral("-targets.csv"),
        tr("CSV files (*.csv)"));
    if (path.isEmpty()) {
        return;
    }
    const auto output = exportTargetEstimatesCsv(estimates);
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) ||
        file.write(output) != output.size() || !file.commit()) {
        QMessageBox::critical(
            this,
            tr("Target export failed"),
            tr("The destination file could not be written."));
        return;
    }
    statusBar()->showMessage(tr("Organization targets exported."), 5'000);
}

void MainWindow::addResearchEvent() {
    if (researchWorkspace_.events.size() >=
        ResearchWorkspace::maximumEvents) {
        QMessageBox::warning(
            this,
            tr("Event workspace full"),
            tr("Remove event records before adding another."));
        return;
    }
    QDialog dialog(this);
    dialog.setWindowTitle(tr("Add research event"));
    auto* layout = new QFormLayout(&dialog);
    auto* symbol = new QLabel(activeSymbol(), &dialog);
    auto* type = new QComboBox(&dialog);
    constexpr std::array types{
        ResearchEventType::Earnings,
        ResearchEventType::ExDividend,
        ResearchEventType::DividendPayment,
        ResearchEventType::Filing,
        ResearchEventType::EconomicRelease,
        ResearchEventType::CentralBank,
        ResearchEventType::OptionsExpiration,
        ResearchEventType::MarketHoliday,
        ResearchEventType::Custom,
    };
    for (const auto value : types) {
        type->addItem(
            researchEventTypeLabel(value),
            static_cast<int>(value));
    }
    auto* date = new QDateEdit(QDate::currentDate(), &dialog);
    date->setCalendarPopup(true);
    date->setDisplayFormat(QStringLiteral("yyyy-MM-dd"));
    auto* timeOfDay = new QLineEdit(&dialog);
    timeOfDay->setMaxLength(40);
    timeOfDay->setPlaceholderText(tr("Optional, e.g. after market"));
    auto* title = new QLineEdit(&dialog);
    title->setMaxLength(160);
    auto* source = new QLineEdit(QStringLiteral("Manual"), &dialog);
    source->setMaxLength(120);
    auto* confidence = new QComboBox(&dialog);
    confidence->addItem(
        tr("Confirmed"),
        static_cast<int>(ResearchConfidence::Confirmed));
    confidence->addItem(
        tr("Estimated"),
        static_cast<int>(ResearchConfidence::Estimated));
    confidence->addItem(
        tr("Unknown"),
        static_cast<int>(ResearchConfidence::Unknown));
    auto* detail = new QLineEdit(&dialog);
    detail->setMaxLength(1'024);
    layout->addRow(tr("Symbol"), symbol);
    layout->addRow(tr("Type"), type);
    layout->addRow(tr("Date"), date);
    layout->addRow(tr("Time"), timeOfDay);
    layout->addRow(tr("Title"), title);
    layout->addRow(tr("Source"), source);
    layout->addRow(tr("Confidence"), confidence);
    layout->addRow(tr("Detail"), detail);
    auto* buttons = new QDialogButtonBox(
        QDialogButtonBox::Save | QDialogButtonBox::Cancel,
        &dialog);
    layout->addRow(buttons);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    connect(
        buttons,
        &QDialogButtonBox::accepted,
        &dialog,
        [&] {
            const ResearchEvent candidate{
                .id = QStringLiteral("manual-%1").arg(
                    QUuid::createUuid().toString(QUuid::WithoutBraces)),
                .symbol = activeSymbol(),
                .type = static_cast<ResearchEventType>(
                    type->currentData().toInt()),
                .scheduledDate = date->date(),
                .timeOfDay = timeOfDay->text().trimmed(),
                .title = title->text().trimmed(),
                .source = source->text().trimmed(),
                .asOfUtc = QDateTime::currentSecsSinceEpoch(),
                .confidence = static_cast<ResearchConfidence>(
                    confidence->currentData().toInt()),
                .detail = detail->text().trimmed(),
            };
            if (const auto error = validateResearchEvent(candidate);
                !error.isEmpty()) {
                QMessageBox::warning(
                    &dialog,
                    tr("Invalid research event"),
                    error);
                return;
            }
            researchWorkspace_.events.push_back(candidate);
            dialog.accept();
        });
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }
    refreshResearchDisplay();
    saveSettingsNow();
}

void MainWindow::removeResearchEvent() {
    const auto row = researchEventTable_->currentRow();
    const auto* item = row >= 0 ? researchEventTable_->item(row, 0) : nullptr;
    if (!item) {
        return;
    }
    const auto id = item->data(Qt::UserRole).toString();
    std::erase_if(
        researchWorkspace_.events,
        [&](const ResearchEvent& event) {
            return event.id == id;
        });
    refreshResearchDisplay();
    saveSettingsNow();
}

void MainWindow::recalculateMarginRisk() {
    if (!marginLongValueInput_ || !marginCurrentLabel_) {
        return;
    }
    marginInstrumentLabel_->setText(
        currentBars_.empty() || currentSeriesSymbol_.isEmpty()
            ? tr("No loaded chart series")
            : tr("%1 · loaded close %2 (scenario uses account values)")
                  .arg(
                      currentSeriesSymbol_,
                      formatPrice(currentBars_.back().close)));
    const MarginRiskInput input{
        .longMarketValue = marginLongValueInput_->value(),
        .marginDebit = marginDebitInput_->value(),
        .otherEquity = marginOtherEquityInput_->value(),
        .maintenanceRate = marginMaintenanceInput_->value() / 100.0,
        .stressPercent = marginStressInput_->value(),
    };
    if (const auto error = validateMarginRiskInput(input)) {
        const auto message = QString::fromStdString(*error);
        marginCurrentLabel_->setText(message);
        marginStressResultLabel_->setText(QStringLiteral("—"));
        marginThresholdLabel_->setText(QStringLiteral("—"));
        return;
    }
    const auto result = calculateMarginRisk(input);
    const auto money = [](const double value) {
        return QLocale::system().toCurrencyString(value, QStringLiteral("$"));
    };
    marginCurrentLabel_->setText(
        tr("Equity %1 (%2%) · requirement %3 · cushion %4 · %5")
            .arg(
                money(result.currentEquity),
                QLocale::system().toString(
                    result.currentEquityPercent,
                    'f',
                    2),
                money(result.currentRequirement),
                money(result.currentCushion),
                result.currentDeficiency ? tr("DEFICIENT") : tr("within assumption")));
    marginStressResultLabel_->setText(
        tr("Market value %1 · equity %2 · requirement %3 · cushion %4 · %5")
            .arg(
                money(result.stressedMarketValue),
                money(result.stressedEquity),
                money(result.stressedRequirement),
                money(result.stressedCushion),
                result.stressedDeficiency ? tr("DEFICIENT") : tr("within assumption")));
    if (!result.callMarketValue || !result.callDeclinePercent) {
        marginThresholdLabel_->setText(
            tr("Not applicable: other equity covers the financed amount."));
    } else if (*result.callDeclinePercent <= 0.0) {
        marginThresholdLabel_->setText(
            tr("Long market value %1 · %2% change from current assumption")
                .arg(
                    money(*result.callMarketValue),
                    QLocale::system().toString(
                        *result.callDeclinePercent,
                        'f',
                        2)));
    } else {
        marginThresholdLabel_->setText(
            tr("Already below the assumed threshold; long value must rise to "
               "%1 (%2%).")
                .arg(
                    money(*result.callMarketValue),
                    QLocale::system().toString(
                        *result.callDeclinePercent,
                        'f',
                        2)));
    }
}

void MainWindow::showLoadingDataStatus(
    const QString& symbol,
    const QString& timeframe) {
    dataStatus_ = {
        .lifecycle = tr("Loading"),
        .source = tr("Yahoo Finance"),
        .detail = marketDataClient_->hasTwelveDataKey()
                      ? tr("Yahoo primary; Twelve Data fallback configured.")
                      : tr("Yahoo primary; Twelve Data fallback is not configured."),
        .metadata = {
            .deliveryMode = DataDeliveryMode::Polled,
            .interval = timeframe,
        },
    };
    dataMarketLabel_->setText(symbol);
    refreshDataStatusDisplay();
    sourceLabel_->setText(tr("Loading Yahoo…"));
}

void MainWindow::updateDataStatus(
    const QString& source,
    const MarketDataMetadata& metadata,
    const Bars& bars,
    const QString& lifecycle,
    const QString& detail) {
    dataStatus_ = {
        .lifecycle = lifecycle,
        .source = source,
        .detail = detail,
        .metadata = metadata,
        .barCount = bars.size(),
        .lastBarTimestamp =
            bars.empty()
                ? std::optional<std::int64_t>{}
                : std::optional<std::int64_t>{bars.back().timestamp},
    };
    refreshDataStatusDisplay();
}

void MainWindow::refreshDataStatusDisplay() {
    if (!dataLifecycleLabel_) {
        return;
    }
    dataLifecycleLabel_->setText(dataStatus_.lifecycle.isEmpty()
                                     ? QStringLiteral("—")
                                     : dataStatus_.lifecycle);

    QString delivery;
    switch (dataStatus_.metadata.deliveryMode) {
    case DataDeliveryMode::Polled:
        delivery = dataStatus_.metadata.exchangeDelayMinutes
                       ? tr("Polled REST · provider delay %1 min")
                             .arg(*dataStatus_.metadata.exchangeDelayMinutes)
                       : tr("Polled REST · delay unknown");
        break;
    case DataDeliveryMode::LocalFile:
        delivery = tr("Local file · no live updates");
        break;
    case DataDeliveryMode::Synthetic:
        delivery = tr("Synthetic demo · not market data");
        break;
    }
    dataDeliveryLabel_->setText(delivery);

    const auto now = QDateTime::currentSecsSinceEpoch();
    dataLastBarLabel_->setText(
        dataStatus_.lastBarTimestamp
            ? tr("%1 · %2")
                  .arg(utcTimestamp(*dataStatus_.lastBarTimestamp))
                  .arg(ageText(*dataStatus_.lastBarTimestamp, now))
            : QStringLiteral("—"));
    dataRetrievedLabel_->setText(
        dataStatus_.metadata.retrievedAtUtc > 0
            ? tr("%1 · %2")
                  .arg(utcTimestamp(dataStatus_.metadata.retrievedAtUtc))
                  .arg(ageText(dataStatus_.metadata.retrievedAtUtc, now))
            : QStringLiteral("—"));
    const auto market = nonEmptyParts({
        dataStatus_.metadata.exchange,
        dataStatus_.metadata.currency,
        dataStatus_.metadata.instrumentType,
        dataStatus_.metadata.timezone,
    });
    dataMarketLabel_->setText(
        market.isEmpty() ? QStringLiteral("—") : market.join(QStringLiteral(" · ")));
    dataBarCountLabel_->setText(
        QLocale::system().toString(
            static_cast<qulonglong>(dataStatus_.barCount)));
    dataDetailLabel_->setText(
        dataStatus_.detail.isEmpty()
            ? tr("No provider detail.")
            : dataStatus_.detail);

    const auto sourceSuffix =
        dataStatus_.metadata.deliveryMode == DataDeliveryMode::Polled
            ? tr(" · polled")
            : dataStatus_.metadata.deliveryMode == DataDeliveryMode::Synthetic
                ? tr(" · synthetic")
                : tr(" · local");
    sourceLabel_->setText(
        dataStatus_.source.isEmpty()
            ? tr("No source")
            : dataStatus_.source + sourceSuffix);
    sourceLabel_->setToolTip(dataStatus_.detail);
}

void MainWindow::applyTheme(const bool dark) {
    chartView_->bridge()->setDarkTheme(dark);
    if (dark) {
        qApp->setStyleSheet(QStringLiteral(
            "QMainWindow, QDialog, QMenuBar, QMenu, QToolBar, QDockWidget, "
            "QListWidget, QComboBox, QLineEdit, QSpinBox, QDoubleSpinBox, "
            "QTabWidget, QTableWidget, "
            "QPushButton, QStatusBar {"
            " background: #131722; color: #d1d4dc; }"
            "QListWidget::item:selected, QTableWidget::item:selected {"
            " background: #2962ff; color: white; }"
            "QToolBar { border-bottom: 1px solid #2a2e39; spacing: 7px; }"
            "QDockWidget, QLineEdit, QSpinBox, QDoubleSpinBox, QTableWidget {"
            " border: 1px solid #2a2e39; }"
            "QHeaderView::section { background: #1e222d; color: #d1d4dc; }"
            "QWidget#analysisPanel, QWidget#analysisPanel QLabel, "
            "QWidget#dataStatusPanel, QWidget#dataStatusPanel QLabel, "
            "QWidget#researchPanel, QWidget#researchPanel QLabel, "
            "QWidget#targetEstimatePanel, QWidget#targetEstimatePanel QLabel, "
            "QWidget#researchEventPanel, QWidget#researchEventPanel QLabel, "
            "QWidget#marginRiskPanel, QWidget#marginRiskPanel QLabel {"
            " background: #131722; color: #d1d4dc; }"));
    } else {
        qApp->setStyleSheet({});
    }
    saveSettingsNow();
}

void MainWindow::showAbout() {
    QDialog dialog(this);
    dialog.setWindowTitle(tr("About TradingView Chart"));
    dialog.setMinimumWidth(560);
    auto* layout = new QVBoxLayout(&dialog);
    auto* label = new QLabel(
        tr("<h2>TradingView Chart 0.5.0</h2>"
           "<p>A C++/Qt market chart viewer with online and offline sources.</p>"
           "<p>Charts are rendered by "
           "<a href=\"https://www.tradingview.com/\">TradingView "
           "Lightweight Charts™</a> 5.2.0 under the Apache-2.0 license.</p>"
           "<p>Yahoo Finance is queried first through an unofficial chart endpoint. "
           "Twelve Data is used as a fallback when TWELVE_DATA_API_KEY is set. "
           "Polled data is never described as streaming real-time data.</p>"
           "<p>SMA, EMA, UTC-session VWAP, RSI, MACD, rolling high/low, "
           "Volume SMA, price change, range, and average volume are calculated "
           "locally from the displayed OHLCV bars.</p>"
           "<p>Optional Alpha Vantage research is loaded only on request when "
           "ALPHA_VANTAGE_API_KEY is set. Provider aggregate targets remain "
           "separate from dated organization-level targets recorded locally.</p>"
           "<p>Earnings and corporate-event dates retain source and confidence. "
           "The margin panel is a long-only scenario calculator, not a broker "
           "connection or prediction of a margin-call date.</p>"
           "<p>The local Strategy Lab reuses one rule definition for next-bar "
           "long-only backtests, deterministic replay, cached-watchlist scans, "
           "and foreground-only alerts. Provider history is stored locally "
           "with its provenance; synthetic demo bars are not cached.</p>"
           "<p>The crosshair values and calculations are descriptive, not "
           "forecasts or trading advice. Offline demo data is synthetic; "
           "imported CSV data and watchlist notes remain local.</p>"),
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
    if (watchlist_ && watchlist_->currentItem()) {
        return normalizeWatchlistSymbol(watchlist_->currentItem()->text());
    }
    const auto* list = activeWatchlist();
    return list && !list->entries.empty()
               ? normalizeWatchlistSymbol(list->entries.front().symbol)
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
    statusBar()->showMessage(
        tr("%1 · %2 · %3 · %4 bars")
            .arg(symbol, timeframe, source)
            .arg(static_cast<qulonglong>(barCount)));
}

void MainWindow::closeEvent(QCloseEvent* event) {
    if (settingsEnabled_) {
        saveSettings();
    }
    QMainWindow::closeEvent(event);
}

} // namespace tvchart
