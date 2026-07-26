#include "portfolio/portfolio_widget.hpp"

#include "data/historical_data_store.hpp"
#include "portfolio/portfolio_risk.hpp"
#include "watchlists/watchlist_workspace.hpp"

#include <QAbstractItemView>
#include <QComboBox>
#include <QDate>
#include <QDateTime>
#include <QDateTimeEdit>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QMessageBox>
#include <QPushButton>
#include <QSettings>
#include <QTabWidget>
#include <QTableWidget>
#include <QTimeZone>
#include <QUuid>
#include <QVBoxLayout>

#include <algorithm>
#include <array>
#include <cmath>
#include <ranges>
#include <set>
#include <utility>

namespace tvchart {
namespace {

[[nodiscard]] QString number(const double value, const int decimals = 2) {
    return QLocale::system().toString(value, 'f', decimals);
}

[[nodiscard]] QString optionalNumber(
    const std::optional<double>& value,
    const int decimals = 2) {
    return value ? number(*value, decimals) : QStringLiteral("—");
}

[[nodiscard]] QString utcDateTime(const std::int64_t timestamp) {
    return timestamp > 0
               ? QDateTime::fromSecsSinceEpoch(timestamp, QTimeZone::UTC)
                     .toString(QStringLiteral("yyyy-MM-dd HH:mm"))
               : QStringLiteral("—");
}

} // namespace

PortfolioWidget::PortfolioWidget(
    HistoricalDataStore* historyStore,
    QWidget* parent)
    : QWidget(parent),
      historyStore_(historyStore) {
    buildUi();
    refreshPortfolioSelector();
    refreshDisplay();
}

void PortfolioWidget::buildUi() {
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(6, 6, 6, 6);
    auto* selectorRow = new QHBoxLayout;
    portfolioSelector_ = new QComboBox(this);
    auto* create = new QPushButton(tr("New"), this);
    auto* rename = new QPushButton(tr("Rename"), this);
    auto* remove = new QPushButton(tr("Delete"), this);
    selectorRow->addWidget(portfolioSelector_, 1);
    selectorRow->addWidget(create);
    selectorRow->addWidget(rename);
    selectorRow->addWidget(remove);
    root->addLayout(selectorRow);
    connect(
        portfolioSelector_,
        &QComboBox::currentIndexChanged,
        this,
        [this](int) { refreshDisplay(); });
    connect(create, &QPushButton::clicked, this, &PortfolioWidget::createPortfolio);
    connect(rename, &QPushButton::clicked, this, &PortfolioWidget::renamePortfolio);
    connect(remove, &QPushButton::clicked, this, &PortfolioWidget::deletePortfolio);

    summaryLabel_ = new QLabel(this);
    summaryLabel_->setWordWrap(true);
    summaryLabel_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    root->addWidget(summaryLabel_);
    riskLabel_ = new QLabel(this);
    riskLabel_->setWordWrap(true);
    riskLabel_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    root->addWidget(riskLabel_);
    eventRiskLabel_ = new QLabel(this);
    eventRiskLabel_->setWordWrap(true);
    root->addWidget(eventRiskLabel_);

    auto* tabs = new QTabWidget(this);
    root->addWidget(tabs, 1);
    holdingsTable_ = new QTableWidget(0, 8, tabs);
    holdingsTable_->setHorizontalHeaderLabels({
        tr("Symbol"),
        tr("Quantity"),
        tr("Average cost"),
        tr("Last"),
        tr("Value"),
        tr("Unrealized"),
        tr("Realized"),
        tr("Allocation"),
    });
    holdingsTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    holdingsTable_->horizontalHeader()->setSectionResizeMode(
        QHeaderView::ResizeToContents);
    holdingsTable_->horizontalHeader()->setStretchLastSection(true);
    tabs->addTab(holdingsTable_, tr("Holdings"));

    auto* transactionPanel = new QWidget(tabs);
    auto* transactionLayout = new QVBoxLayout(transactionPanel);
    transactionsTable_ = new QTableWidget(0, 8, transactionPanel);
    transactionsTable_->setHorizontalHeaderLabels({
        tr("UTC time"),
        tr("Type"),
        tr("Symbol"),
        tr("Quantity / ratio"),
        tr("Price"),
        tr("Cash amount"),
        tr("Fees"),
        tr("Note"),
    });
    transactionsTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
    transactionsTable_->setSelectionMode(QAbstractItemView::SingleSelection);
    transactionsTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    transactionsTable_->horizontalHeader()->setSectionResizeMode(
        QHeaderView::ResizeToContents);
    transactionsTable_->horizontalHeader()->setStretchLastSection(true);
    transactionLayout->addWidget(transactionsTable_, 1);
    auto* transactionActions = new QHBoxLayout;
    auto* add = new QPushButton(tr("Add paper transaction"), transactionPanel);
    auto* removeTransaction =
        new QPushButton(tr("Remove selected"), transactionPanel);
    transactionActions->addWidget(add);
    transactionActions->addWidget(removeTransaction);
    transactionActions->addStretch();
    transactionLayout->addLayout(transactionActions);
    connect(add, &QPushButton::clicked, this, &PortfolioWidget::addTransaction);
    connect(
        removeTransaction,
        &QPushButton::clicked,
        this,
        &PortfolioWidget::removeTransaction);
    tabs->addTab(transactionPanel, tr("Transactions"));

    auto* riskPanel = new QWidget(tabs);
    auto* riskLayout = new QVBoxLayout(riskPanel);
    auto* benchmarkRow = new QHBoxLayout;
    benchmarkInput_ = new QLineEdit(QStringLiteral("SPY"), riskPanel);
    benchmarkInput_->setMaxLength(32);
    benchmarkInput_->setMaximumWidth(140);
    auto* refreshRisk =
        new QPushButton(tr("Refresh cached risk"), riskPanel);
    benchmarkRow->addWidget(new QLabel(tr("Benchmark"), riskPanel));
    benchmarkRow->addWidget(benchmarkInput_);
    benchmarkRow->addWidget(refreshRisk);
    benchmarkRow->addStretch();
    riskLayout->addLayout(benchmarkRow);
    connect(
        refreshRisk,
        &QPushButton::clicked,
        this,
        [this] { refreshDisplay(); });
    connect(
        benchmarkInput_,
        &QLineEdit::editingFinished,
        this,
        [this] {
            benchmarkInput_->setText(
                normalizeWatchlistSymbol(benchmarkInput_->text()));
            refreshDisplay();
            emit portfolioChanged();
        });
    portfolioRiskSummary_ = new QLabel(
        tr("Load raw daily histories for every holding and the benchmark."),
        riskPanel);
    portfolioRiskSummary_->setWordWrap(true);
    portfolioRiskSummary_->setTextInteractionFlags(
        Qt::TextSelectableByMouse);
    riskLayout->addWidget(portfolioRiskSummary_);
    auto* riskTables = new QTabWidget(riskPanel);
    riskContributionTable_ = new QTableWidget(0, 3, riskTables);
    riskContributionTable_->setHorizontalHeaderLabels({
        tr("Symbol"),
        tr("Current equity weight"),
        tr("Risk contribution"),
    });
    correlationTable_ = new QTableWidget(0, 4, riskTables);
    correlationTable_->setHorizontalHeaderLabels({
        tr("Left"),
        tr("Right"),
        tr("Observations"),
        tr("Correlation"),
    });
    for (auto* table : {
             riskContributionTable_,
             correlationTable_,
         }) {
        table->setEditTriggers(QAbstractItemView::NoEditTriggers);
        table->horizontalHeader()->setSectionResizeMode(
            QHeaderView::ResizeToContents);
        table->horizontalHeader()->setStretchLastSection(true);
    }
    riskTables->addTab(
        riskContributionTable_,
        tr("Risk contributions"));
    riskTables->addTab(correlationTable_, tr("Correlations"));
    riskLayout->addWidget(riskTables, 1);
    tabs->addTab(riskPanel, tr("Risk"));

    auto* targetPanel = new QWidget(tabs);
    auto* targetLayout = new QVBoxLayout(targetPanel);
    targetSummary_ = new QLabel(
        tr("Targets are local planning inputs and never create orders."),
        targetPanel);
    targetSummary_->setWordWrap(true);
    targetLayout->addWidget(targetSummary_);
    targetsTable_ = new QTableWidget(0, 2, targetPanel);
    targetsTable_->setHorizontalHeaderLabels({
        tr("Symbol"),
        tr("Target"),
    });
    targetsTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
    targetsTable_->setSelectionMode(QAbstractItemView::SingleSelection);
    targetsTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    targetsTable_->horizontalHeader()->setSectionResizeMode(
        QHeaderView::ResizeToContents);
    targetsTable_->horizontalHeader()->setStretchLastSection(true);
    targetLayout->addWidget(targetsTable_);
    auto* targetActions = new QHBoxLayout;
    auto* addTargetButton =
        new QPushButton(tr("Add / update target"), targetPanel);
    auto* removeTargetButton =
        new QPushButton(tr("Remove selected"), targetPanel);
    targetActions->addWidget(addTargetButton);
    targetActions->addWidget(removeTargetButton);
    targetActions->addStretch();
    targetLayout->addLayout(targetActions);
    connect(
        addTargetButton,
        &QPushButton::clicked,
        this,
        &PortfolioWidget::addTarget);
    connect(
        removeTargetButton,
        &QPushButton::clicked,
        this,
        &PortfolioWidget::removeTarget);
    rebalanceTable_ = new QTableWidget(0, 7, targetPanel);
    rebalanceTable_->setHorizontalHeaderLabels({
        tr("Symbol"),
        tr("Current"),
        tr("Target"),
        tr("Current value"),
        tr("Target value"),
        tr("Difference"),
        tr("Approx. shares"),
    });
    rebalanceTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    rebalanceTable_->horizontalHeader()->setSectionResizeMode(
        QHeaderView::ResizeToContents);
    rebalanceTable_->horizontalHeader()->setStretchLastSection(true);
    targetLayout->addWidget(rebalanceTable_, 1);
    tabs->addTab(targetPanel, tr("Targets"));

    auto* disclaimer = new QLabel(
        tr("Local long-only accounting in one base currency. It is not a "
           "broker statement, tax-lot report, or order-execution service."),
        this);
    disclaimer->setWordWrap(true);
    disclaimer->setStyleSheet(QStringLiteral("color: #8c8c8c;"));
    root->addWidget(disclaimer);
}

Portfolio* PortfolioWidget::activePortfolio() noexcept {
    const auto identity = portfolioSelector_->currentData().toString();
    const auto found = std::ranges::find(
        workspace_.portfolios,
        identity,
        &Portfolio::id);
    return found == workspace_.portfolios.end()
               ? nullptr
               : &*found;
}

const Portfolio* PortfolioWidget::activePortfolio() const noexcept {
    const auto identity = portfolioSelector_->currentData().toString();
    const auto found = std::ranges::find(
        workspace_.portfolios,
        identity,
        &Portfolio::id);
    return found == workspace_.portfolios.end()
               ? nullptr
               : &*found;
}

void PortfolioWidget::refreshPortfolioSelector(const QString& preferredId) {
    const auto selected =
        preferredId.isEmpty()
            ? portfolioSelector_->currentData().toString()
            : preferredId;
    portfolioSelector_->clear();
    for (const auto& portfolio : workspace_.portfolios) {
        portfolioSelector_->addItem(
            QStringLiteral("%1 · %2")
                .arg(portfolio.name, portfolio.baseCurrency),
            portfolio.id);
    }
    auto index = portfolioSelector_->findData(selected);
    if (index < 0 && portfolioSelector_->count() > 0) {
        index = 0;
    }
    portfolioSelector_->setCurrentIndex(index);
}

void PortfolioWidget::refreshDisplay() {
    holdingsTable_->setRowCount(0);
    transactionsTable_->setRowCount(0);
    riskContributionTable_->setRowCount(0);
    correlationTable_->setRowCount(0);
    targetsTable_->setRowCount(0);
    rebalanceTable_->setRowCount(0);
    const auto* portfolio = activePortfolio();
    if (!portfolio) {
        summaryLabel_->setText(tr("No portfolio is selected."));
        riskLabel_->clear();
        eventRiskLabel_->clear();
        portfolioRiskSummary_->clear();
        targetSummary_->clear();
        return;
    }
    const auto snapshot = calculatePortfolioSnapshot(*portfolio, prices_);
    if (!snapshot.ok()) {
        summaryLabel_->setText(
            tr("Portfolio calculation failed: %1").arg(snapshot.error));
        riskLabel_->clear();
        eventRiskLabel_->clear();
        portfolioRiskSummary_->setText(
            tr("Risk unavailable until the ledger reconciles."));
        targetSummary_->setText(
            tr("Targets unavailable until the ledger reconciles."));
        return;
    }
    auto valuation = snapshot.completeValuation
                         ? tr("complete")
                         : tr("incomplete · load %1")
                               .arg(snapshot.missingPrices.join(
                                   QStringLiteral(", ")));
    const auto valued = [&](const double value) {
        return snapshot.completeValuation
                   ? number(value)
                   : QStringLiteral("—");
    };
    summaryLabel_->setText(
        tr("%1 · Cash %2 · Holdings %3 · Equity %4 · Net cash flows %5 · "
           "Total gain %6 · Realized %7 · Unrealized %8 · Income %9 · "
           "Fees %10 · valuation %11")
            .arg(portfolio->baseCurrency)
            .arg(number(snapshot.cash))
            .arg(valued(snapshot.marketValue))
            .arg(valued(snapshot.equity))
            .arg(number(snapshot.netContributions))
            .arg(valued(snapshot.totalGain))
            .arg(number(snapshot.realizedProfitLoss))
            .arg(valued(snapshot.unrealizedProfitLoss))
            .arg(number(snapshot.income))
            .arg(number(snapshot.feesPaid))
            .arg(valuation));
    riskLabel_->setText(
        tr("Largest priced position %1% · concentration index %2 · "
           "effective priced holdings %3%4")
            .arg(number(snapshot.largestPositionPercent, 1))
            .arg(number(snapshot.concentrationIndex, 3))
            .arg(optionalNumber(snapshot.effectiveHoldings, 2))
            .arg(
                snapshot.cash < 0.0
                    ? tr(" · warning: negative cash / financing balance")
                    : QString{}));

    holdingsTable_->setRowCount(static_cast<int>(snapshot.holdings.size()));
    for (std::size_t index = 0; index < snapshot.holdings.size(); ++index) {
        const auto& holding = snapshot.holdings[index];
        const std::array values{
            holding.symbol,
            number(holding.quantity, 6),
            number(holding.averageCost, 4),
            optionalNumber(holding.latestPrice, 4),
            optionalNumber(holding.marketValue),
            optionalNumber(holding.unrealizedProfitLoss),
            number(holding.realizedProfitLoss),
            number(holding.allocationPercent, 1) + QStringLiteral("%"),
        };
        for (int column = 0; column < static_cast<int>(values.size());
             ++column) {
            holdingsTable_->setItem(
                static_cast<int>(index),
                column,
                new QTableWidgetItem(values[static_cast<std::size_t>(column)]));
        }
    }

    std::vector<const PortfolioTransaction*> transactions;
    transactions.reserve(portfolio->transactions.size());
    for (const auto& transaction : portfolio->transactions) {
        transactions.push_back(&transaction);
    }
    std::ranges::sort(
        transactions,
        [](const auto* left, const auto* right) {
            if (left->timestampUtc != right->timestampUtc) {
                return left->timestampUtc > right->timestampUtc;
            }
            return left->id < right->id;
        });
    transactionsTable_->setRowCount(
        static_cast<int>(transactions.size()));
    for (std::size_t index = 0; index < transactions.size(); ++index) {
        const auto& transaction = *transactions[index];
        const std::array values{
            utcDateTime(transaction.timestampUtc),
            portfolioTransactionTypeLabel(transaction.type),
            transaction.symbol,
            transaction.quantity > 0.0
                ? number(transaction.quantity, 6)
                : QStringLiteral("—"),
            transaction.price > 0.0
                ? number(transaction.price, 4)
                : QStringLiteral("—"),
            transaction.amount > 0.0
                ? number(transaction.amount)
                : QStringLiteral("—"),
            transaction.fees > 0.0
                ? number(transaction.fees)
                : QStringLiteral("—"),
            transaction.note,
        };
        for (int column = 0; column < static_cast<int>(values.size());
             ++column) {
            auto* item =
                new QTableWidgetItem(values[static_cast<std::size_t>(column)]);
            if (column == 0) {
                item->setData(Qt::UserRole, transaction.id);
            }
            transactionsTable_->setItem(
                static_cast<int>(index),
                column,
                item);
        }
    }

    QStringList holdingSymbols;
    for (const auto& holding : snapshot.holdings) {
        holdingSymbols.push_back(holding.symbol);
    }
    const auto today = QDate::currentDate();
    const auto horizon = today.addDays(30);
    std::vector<const ResearchEvent*> upcoming;
    for (const auto& event : researchEvents_) {
        if (holdingSymbols.contains(
                normalizeWatchlistSymbol(event.symbol),
                Qt::CaseInsensitive) &&
            event.scheduledDate >= today &&
            event.scheduledDate <= horizon &&
            validateResearchEvent(event).isEmpty()) {
            upcoming.push_back(&event);
        }
    }
    std::ranges::sort(
        upcoming,
        {},
        [](const ResearchEvent* event) {
            return event->scheduledDate;
        });
    QStringList eventLines;
    for (const auto* event : upcoming | std::views::take(5)) {
        eventLines.push_back(
            tr("%1 %2 · %3")
                .arg(
                    event->scheduledDate.toString(Qt::ISODate),
                    normalizeWatchlistSymbol(event->symbol),
                    event->title));
    }
    eventRiskLabel_->setText(
        eventLines.isEmpty()
            ? tr("No sourced holding events in the next 30 days.")
            : tr("Upcoming holding events:\n%1")
                  .arg(eventLines.join(QStringLiteral("\n"))));
    refreshPortfolioRisk(*portfolio, snapshot);
    refreshTargets(*portfolio, snapshot);
}

void PortfolioWidget::refreshPortfolioRisk(
    const Portfolio& portfolio,
    const PortfolioSnapshot& snapshot) {
    if (!historyStore_ || !historyStore_->isOpen()) {
        portfolioRiskSummary_->setText(
            tr("Portfolio risk unavailable: historical cache is not open."));
        return;
    }
    std::set<QString> symbols;
    for (const auto& holding : snapshot.holdings) {
        symbols.insert(normalizeWatchlistSymbol(holding.symbol));
    }
    for (const auto& transaction : portfolio.transactions) {
        const auto symbol =
            normalizeWatchlistSymbol(transaction.symbol);
        if (!symbol.isEmpty()) {
            symbols.insert(symbol);
        }
    }
    PortfolioHistory histories;
    for (const auto& symbol : symbols) {
        auto cached =
            historyStore_->loadLatestSeries(symbol, Timeframe::OneDay);
        if (cached.ok()) {
            histories.emplace(symbol, std::move(cached.bars));
        }
    }
    auto benchmark =
        normalizeWatchlistSymbol(benchmarkInput_->text());
    if (benchmark.isEmpty()) {
        benchmark = QStringLiteral("SPY");
        benchmarkInput_->setText(benchmark);
    }
    auto benchmarkHistory =
        historyStore_->loadLatestSeries(
            benchmark,
            Timeframe::OneDay);
    const auto report = calculatePortfolioRisk(
        portfolio,
        snapshot,
        histories,
        benchmark,
        benchmarkHistory.ok()
            ? benchmarkHistory.bars
            : Bars{});
    if (!report.ok()) {
        portfolioRiskSummary_->setText(
            tr("Portfolio risk unavailable: %1%2")
                .arg(
                    report.error,
                    report.missingHistory.isEmpty()
                        ? QString{}
                        : tr(" Missing: %1.")
                              .arg(report.missingHistory.join(
                                  QStringLiteral(", ")))));
        return;
    }
    const auto metric =
        [](const std::optional<double>& value,
           const int decimals,
           const QString& suffix = {}) {
        return value
                   ? number(*value, decimals) + suffix
                   : QStringLiteral("—");
    };
    portfolioRiskSummary_->setText(
        tr("Raw daily fixed-current-weight estimate · %1 common returns · "
           "%2 to %3 UTC · annual return %4 · volatility %5 · Sharpe %6 "
           "(0% risk-free) · max drawdown %7 · historical 95% VaR %8 · "
           "CVaR %9 · %10 beta %11 · annual alpha %12 · XIRR %13 · "
           "daily-close TWR estimate %14%15")
            .arg(report.observations)
            .arg(utcDateTime(report.firstTimestamp))
            .arg(utcDateTime(report.lastTimestamp))
            .arg(metric(
                report.annualizedReturnPercent,
                2,
                QStringLiteral("%")))
            .arg(metric(
                report.annualizedVolatilityPercent,
                2,
                QStringLiteral("%")))
            .arg(metric(report.sharpeRatio, 2))
            .arg(metric(
                report.maximumDrawdownPercent,
                2,
                QStringLiteral("%")))
            .arg(metric(
                report.historicalValueAtRisk95Percent,
                2,
                QStringLiteral("%")))
            .arg(metric(
                report.historicalConditionalValueAtRisk95Percent,
                2,
                QStringLiteral("%")))
            .arg(report.benchmarkSymbol)
            .arg(metric(report.beta, 3))
            .arg(metric(
                report.annualizedAlphaPercent,
                2,
                QStringLiteral("%")))
            .arg(metric(
                report.moneyWeightedReturnPercent,
                2,
                QStringLiteral("%")))
            .arg(metric(
                report.dailyCloseTimeWeightedReturnPercent,
                2,
                QStringLiteral("%")))
            .arg(
                report.dailyCloseTimeWeightedReturnIsApproximate
                    ? tr(" · cash flows assumed at daily valuation boundary")
                    : QString{}));

    riskContributionTable_->setRowCount(
        static_cast<int>(report.riskContributions.size()));
    for (std::size_t index = 0;
         index < report.riskContributions.size();
         ++index) {
        const auto& contribution =
            report.riskContributions[index];
        const QStringList values{
            contribution.symbol,
            number(contribution.weightPercent, 2) +
                QStringLiteral("%"),
            number(contribution.contributionPercent, 2) +
                QStringLiteral("%"),
        };
        for (int column = 0; column < values.size(); ++column) {
            riskContributionTable_->setItem(
                static_cast<int>(index),
                column,
                new QTableWidgetItem(values[column]));
        }
    }
    correlationTable_->setRowCount(
        static_cast<int>(report.correlations.size()));
    for (std::size_t index = 0;
         index < report.correlations.size();
         ++index) {
        const auto& correlation = report.correlations[index];
        const QStringList values{
            correlation.leftSymbol,
            correlation.rightSymbol,
            QString::number(correlation.observations),
            optionalNumber(correlation.correlation, 3),
        };
        for (int column = 0; column < values.size(); ++column) {
            correlationTable_->setItem(
                static_cast<int>(index),
                column,
                new QTableWidgetItem(values[column]));
        }
    }
}

void PortfolioWidget::refreshTargets(
    const Portfolio& portfolio,
    const PortfolioSnapshot& snapshot) {
    targetsTable_->setRowCount(
        static_cast<int>(portfolio.targets.size()));
    auto total = 0.0;
    for (std::size_t index = 0;
         index < portfolio.targets.size();
         ++index) {
        const auto& target = portfolio.targets[index];
        total += target.targetPercent;
        auto* symbol = new QTableWidgetItem(
            normalizeWatchlistSymbol(target.symbol));
        symbol->setData(
            Qt::UserRole,
            normalizeWatchlistSymbol(target.symbol));
        targetsTable_->setItem(
            static_cast<int>(index),
            0,
            symbol);
        targetsTable_->setItem(
            static_cast<int>(index),
            1,
            new QTableWidgetItem(
                number(target.targetPercent, 2) +
                QStringLiteral("%")));
    }
    const auto rebalance =
        calculateRebalance(portfolio, snapshot, prices_);
    if (!rebalance.ok()) {
        targetSummary_->setText(
            tr("Rebalancing unavailable: %1").arg(rebalance.error));
        return;
    }
    targetSummary_->setText(
        tr("Target securities %1% · target cash %2% · differences are "
           "planning suggestions only%3.")
            .arg(number(total, 2))
            .arg(number(rebalance.targetCashPercent, 2))
            .arg(
                rebalance.missingPrices.isEmpty()
                    ? QString{}
                    : tr(" · missing prices: %1")
                          .arg(rebalance.missingPrices.join(
                              QStringLiteral(", ")))));
    rebalanceTable_->setRowCount(
        static_cast<int>(rebalance.suggestions.size()));
    for (std::size_t index = 0;
         index < rebalance.suggestions.size();
         ++index) {
        const auto& suggestion =
            rebalance.suggestions[index];
        const QStringList values{
            suggestion.symbol,
            number(suggestion.currentPercent, 2) +
                QStringLiteral("%"),
            number(suggestion.targetPercent, 2) +
                QStringLiteral("%"),
            number(suggestion.currentValue, 2),
            number(suggestion.targetValue, 2),
            number(suggestion.differenceValue, 2),
            optionalNumber(suggestion.approximateShares, 6),
        };
        for (int column = 0; column < values.size(); ++column) {
            rebalanceTable_->setItem(
                static_cast<int>(index),
                column,
                new QTableWidgetItem(values[column]));
        }
    }
}

void PortfolioWidget::createPortfolio() {
    if (workspace_.portfolios.size() >=
        PortfolioWorkspace::maximumPortfolios) {
        emit statusMessage(tr("The local workspace already has 16 portfolios."));
        return;
    }
    bool ok = false;
    const auto name = QInputDialog::getText(
                          this,
                          tr("New portfolio"),
                          tr("Portfolio name"),
                          QLineEdit::Normal,
                          tr("New portfolio"),
                          &ok)
                          .trimmed();
    if (!ok || name.isEmpty()) {
        return;
    }
    const auto currency = QInputDialog::getText(
                              this,
                              tr("Base currency"),
                              tr("Currency code"),
                              QLineEdit::Normal,
                              QStringLiteral("USD"),
                              &ok)
                              .trimmed()
                              .toUpper();
    if (!ok) {
        return;
    }
    Portfolio portfolio{
        .id = QUuid::createUuid().toString(QUuid::WithoutBraces),
        .name = name,
        .baseCurrency = currency,
    };
    if (const auto error = validatePortfolio(portfolio); !error.isEmpty()) {
        QMessageBox::warning(this, tr("Invalid portfolio"), error);
        return;
    }
    const auto identity = portfolio.id;
    workspace_.portfolios.push_back(std::move(portfolio));
    refreshPortfolioSelector(identity);
    refreshDisplay();
    emit portfolioChanged();
}

void PortfolioWidget::renamePortfolio() {
    auto* portfolio = activePortfolio();
    if (!portfolio) {
        return;
    }
    bool ok = false;
    const auto name = QInputDialog::getText(
                          this,
                          tr("Rename portfolio"),
                          tr("Portfolio name"),
                          QLineEdit::Normal,
                          portfolio->name,
                          &ok)
                          .trimmed();
    if (!ok || name.isEmpty() || name.size() > 120) {
        return;
    }
    portfolio->name = name;
    refreshPortfolioSelector(portfolio->id);
    emit portfolioChanged();
}

void PortfolioWidget::deletePortfolio() {
    auto* portfolio = activePortfolio();
    if (!portfolio) {
        return;
    }
    if (workspace_.portfolios.size() <= 1) {
        emit statusMessage(tr("At least one local portfolio is required."));
        return;
    }
    const auto identity = portfolio->id;
    if (QMessageBox::question(
            this,
            tr("Delete portfolio"),
            tr("Delete “%1” and all of its local transactions?")
                .arg(portfolio->name)) != QMessageBox::Yes) {
        return;
    }
    std::erase_if(
        workspace_.portfolios,
        [&](const Portfolio& candidate) {
            return candidate.id == identity;
        });
    refreshPortfolioSelector();
    refreshDisplay();
    emit portfolioChanged();
}

void PortfolioWidget::addTarget() {
    auto* portfolio = activePortfolio();
    if (!portfolio) {
        return;
    }
    QDialog dialog(this);
    dialog.setWindowTitle(tr("Add or update allocation target"));
    auto* layout = new QFormLayout(&dialog);
    auto* symbol = new QLineEdit(currentSymbol_, &dialog);
    symbol->setMaxLength(32);
    auto* percent = new QDoubleSpinBox(&dialog);
    percent->setRange(0.0, 100.0);
    percent->setDecimals(2);
    percent->setSuffix(QStringLiteral("%"));
    const auto current = std::ranges::find(
        portfolio->targets,
        normalizeWatchlistSymbol(currentSymbol_),
        [](const PortfolioTarget& target) {
            return normalizeWatchlistSymbol(target.symbol);
        });
    if (current != portfolio->targets.end()) {
        percent->setValue(current->targetPercent);
    }
    layout->addRow(tr("Symbol"), symbol);
    layout->addRow(tr("Target allocation"), percent);
    auto* buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel,
        &dialog);
    layout->addRow(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }
    const auto normalized =
        normalizeWatchlistSymbol(symbol->text());
    auto candidate = *portfolio;
    const auto existing = std::ranges::find(
        candidate.targets,
        normalized,
        [](const PortfolioTarget& target) {
            return normalizeWatchlistSymbol(target.symbol);
        });
    if (existing == candidate.targets.end()) {
        if (candidate.targets.size() >=
            PortfolioWorkspace::maximumTargetsPerPortfolio) {
            QMessageBox::warning(
                this,
                tr("Target not saved"),
                tr("The portfolio target limit was reached."));
            return;
        }
        candidate.targets.push_back({
            .symbol = normalized,
            .targetPercent = percent->value(),
        });
    } else {
        existing->targetPercent = percent->value();
    }
    std::ranges::sort(
        candidate.targets,
        {},
        [](const PortfolioTarget& target) {
            return normalizeWatchlistSymbol(target.symbol);
        });
    if (const auto error = validatePortfolio(candidate);
        !error.isEmpty()) {
        QMessageBox::warning(this, tr("Target not saved"), error);
        return;
    }
    *portfolio = std::move(candidate);
    refreshDisplay();
    emit portfolioChanged();
}

void PortfolioWidget::removeTarget() {
    auto* portfolio = activePortfolio();
    const auto row = targetsTable_->currentRow();
    if (!portfolio || row < 0 || !targetsTable_->item(row, 0)) {
        emit statusMessage(tr("Select an allocation target to remove."));
        return;
    }
    const auto symbol =
        targetsTable_->item(row, 0)->data(Qt::UserRole).toString();
    std::erase_if(
        portfolio->targets,
        [&](const PortfolioTarget& target) {
            return normalizeWatchlistSymbol(target.symbol) == symbol;
        });
    refreshDisplay();
    emit portfolioChanged();
}

void PortfolioWidget::addTransaction() {
    auto* portfolio = activePortfolio();
    auto totalTransactions = std::size_t{};
    for (const auto& candidate : workspace_.portfolios) {
        totalTransactions += candidate.transactions.size();
    }
    if (!portfolio ||
        portfolio->transactions.size() >=
            PortfolioWorkspace::maximumTransactionsPerPortfolio ||
        totalTransactions >=
            PortfolioWorkspace::maximumTransactionsTotal) {
        emit statusMessage(tr("The active portfolio cannot accept more transactions."));
        return;
    }
    QDialog dialog(this);
    dialog.setWindowTitle(tr("Add paper transaction"));
    auto* layout = new QFormLayout(&dialog);
    auto* type = new QComboBox(&dialog);
    for (const auto value : {
             PortfolioTransactionType::Deposit,
             PortfolioTransactionType::Withdrawal,
             PortfolioTransactionType::Buy,
             PortfolioTransactionType::Sell,
             PortfolioTransactionType::Dividend,
             PortfolioTransactionType::Fee,
             PortfolioTransactionType::Split,
         }) {
        type->addItem(
            portfolioTransactionTypeLabel(value),
            static_cast<int>(value));
    }
    auto* timestamp = new QDateTimeEdit(
        QDateTime::currentDateTimeUtc(),
        &dialog);
    timestamp->setDisplayFormat(QStringLiteral("yyyy-MM-dd HH:mm"));
    timestamp->setTimeZone(QTimeZone::UTC);
    auto* symbol = new QLineEdit(currentSymbol_, &dialog);
    symbol->setMaxLength(32);
    auto* quantity = new QDoubleSpinBox(&dialog);
    quantity->setRange(0.0, 1.0e15);
    quantity->setDecimals(8);
    quantity->setValue(1.0);
    auto* price = new QDoubleSpinBox(&dialog);
    price->setRange(0.0, 1.0e12);
    price->setDecimals(8);
    price->setValue(std::max(0.0, currentPrice_));
    auto* amount = new QDoubleSpinBox(&dialog);
    amount->setRange(0.0, 1.0e15);
    amount->setDecimals(2);
    auto* fees = new QDoubleSpinBox(&dialog);
    fees->setRange(0.0, 1.0e9);
    fees->setDecimals(2);
    auto* note = new QLineEdit(&dialog);
    note->setMaxLength(512);
    auto* currency = new QLabel(portfolio->baseCurrency, &dialog);
    layout->addRow(tr("Type"), type);
    layout->addRow(tr("UTC time"), timestamp);
    layout->addRow(tr("Symbol"), symbol);
    layout->addRow(tr("Quantity / split ratio"), quantity);
    layout->addRow(tr("Price"), price);
    layout->addRow(tr("Cash amount"), amount);
    layout->addRow(tr("Trade fees"), fees);
    layout->addRow(tr("Currency"), currency);
    layout->addRow(tr("Note"), note);
    const auto refreshFields =
        [type, symbol, quantity, price, amount, fees](int) {
            const auto value = static_cast<PortfolioTransactionType>(
                type->currentData().toInt());
            const auto trade =
                value == PortfolioTransactionType::Buy ||
                value == PortfolioTransactionType::Sell;
            const auto split = value == PortfolioTransactionType::Split;
            const auto dividend = value == PortfolioTransactionType::Dividend;
            symbol->setEnabled(trade || split || dividend);
            quantity->setEnabled(trade || split);
            price->setEnabled(trade);
            amount->setEnabled(
                value == PortfolioTransactionType::Deposit ||
                value == PortfolioTransactionType::Withdrawal ||
                value == PortfolioTransactionType::Fee || dividend);
            fees->setEnabled(trade);
        };
    connect(type, &QComboBox::currentIndexChanged, &dialog, refreshFields);
    refreshFields(0);
    auto* buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel,
        &dialog);
    layout->addRow(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }
    const auto transactionType = static_cast<PortfolioTransactionType>(
        type->currentData().toInt());
    const auto trade =
        transactionType == PortfolioTransactionType::Buy ||
        transactionType == PortfolioTransactionType::Sell;
    const auto split = transactionType == PortfolioTransactionType::Split;
    const auto needsSymbol =
        trade || split ||
        transactionType == PortfolioTransactionType::Dividend;
    PortfolioTransaction transaction{
        .id = QUuid::createUuid().toString(QUuid::WithoutBraces),
        .type = transactionType,
        .symbol =
            needsSymbol
                ? normalizeWatchlistSymbol(symbol->text())
                : QString{},
        .timestampUtc = timestamp->dateTime().toUTC().toSecsSinceEpoch(),
        .quantity = trade || split ? quantity->value() : 0.0,
        .price = trade ? price->value() : 0.0,
        .amount =
            !trade && !split
                ? amount->value()
                : 0.0,
        .fees = trade ? fees->value() : 0.0,
        .currency = portfolio->baseCurrency,
        .note = note->text().trimmed(),
    };
    if (const auto error = validatePortfolioTransaction(transaction);
        !error.isEmpty()) {
        QMessageBox::warning(this, tr("Invalid transaction"), error);
        return;
    }
    portfolio->transactions.push_back(std::move(transaction));
    refreshDisplay();
    emit portfolioChanged();
}

void PortfolioWidget::removeTransaction() {
    auto* portfolio = activePortfolio();
    const auto row = transactionsTable_->currentRow();
    if (!portfolio || row < 0 || !transactionsTable_->item(row, 0)) {
        emit statusMessage(tr("Select a transaction to remove."));
        return;
    }
    const auto identity =
        transactionsTable_->item(row, 0)->data(Qt::UserRole).toString();
    if (QMessageBox::question(
            this,
            tr("Remove transaction"),
            tr("Remove the selected local transaction?")) !=
        QMessageBox::Yes) {
        return;
    }
    std::erase_if(
        portfolio->transactions,
        [&](const PortfolioTransaction& transaction) {
            return transaction.id == identity;
        });
    refreshDisplay();
    emit portfolioChanged();
}

void PortfolioWidget::setCurrentQuote(
    QString symbol,
    const double price,
    const std::int64_t asOfUtc,
    QString currency) {
    symbol = normalizeWatchlistSymbol(std::move(symbol));
    currentSymbol_ = symbol;
    currentPrice_ = std::isfinite(price) && price > 0.0 ? price : 0.0;
    if (!symbol.isEmpty() && currentPrice_ > 0.0 && asOfUtc > 0) {
        prices_.insert(
            symbol,
            {
                .price = currentPrice_,
                .asOfUtc = asOfUtc,
                .currency = std::move(currency),
            });
    }
    refreshDisplay();
}

void PortfolioWidget::setResearchEvents(
    std::vector<ResearchEvent> events) {
    researchEvents_ = std::move(events);
    refreshDisplay();
}

void PortfolioWidget::restoreSettings(QSettings& settings) {
    const auto loaded = deserializePortfolioWorkspace(
        settings.value(QStringLiteral("portfolio/workspace")).toByteArray());
    if (loaded.ok()) {
        workspace_ = loaded.workspace;
    } else {
        emit statusMessage(
            tr("Saved portfolio was not loaded: %1").arg(loaded.error));
    }
    benchmarkInput_->setText(
        normalizeWatchlistSymbol(
            settings
                .value(
                    QStringLiteral("portfolio/benchmark"),
                    QStringLiteral("SPY"))
                .toString()));
    refreshPortfolioSelector(
        settings.value(QStringLiteral("portfolio/activeId")).toString());
    refreshDisplay();
}

void PortfolioWidget::saveSettings(QSettings& settings) const {
    settings.setValue(
        QStringLiteral("portfolio/workspace"),
        serializePortfolioWorkspace(workspace_));
    settings.setValue(
        QStringLiteral("portfolio/activeId"),
        portfolioSelector_->currentData().toString());
    settings.setValue(
        QStringLiteral("portfolio/benchmark"),
        normalizeWatchlistSymbol(benchmarkInput_->text()));
}

} // namespace tvchart
