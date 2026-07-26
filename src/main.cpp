#include "data/market_data_client.hpp"
#include "main_window.hpp"

#include <QApplication>
#include <QCommandLineParser>
#include <QComboBox>
#include <QDebug>
#include <QDoubleSpinBox>
#include <QTableWidget>
#include <QTimer>

int main(int argc, char* argv[]) {
    QApplication application(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("buicongnguyen"));
    QCoreApplication::setApplicationName(QStringLiteral("TradingViewChart"));
    QCoreApplication::setApplicationVersion(QStringLiteral("0.3.0"));

    QCommandLineParser parser;
    parser.setApplicationDescription(
        QStringLiteral("Qt market-data viewer using TradingView Lightweight Charts"));
    parser.addHelpOption();
    parser.addVersionOption();
    const QCommandLineOption smokeOption(
        QStringLiteral("smoke-test"),
        QStringLiteral("Exit successfully after the embedded chart reports ready."));
    parser.addOption(smokeOption);
    const QCommandLineOption marketDataSmokeOption(
        QStringLiteral("market-data-smoke-test"),
        QStringLiteral("Fetch AAPL from the default Yahoo Finance provider and exit."));
    parser.addOption(marketDataSmokeOption);
    parser.process(application);

    if (parser.isSet(marketDataSmokeOption)) {
        tvchart::MarketDataClient client;
        QTimer timeout;
        timeout.setSingleShot(true);
        QObject::connect(&timeout, &QTimer::timeout, &application, [&application]() {
            qCritical() << "MARKET_DATA_SMOKE_TIMEOUT";
            application.exit(2);
        });
        client.fetch(
            QStringLiteral("AAPL"),
            tvchart::Timeframe::FiveMinutes,
            [&application, &timeout](tvchart::MarketDataResult result) {
                timeout.stop();
                if (!result.ok() || result.source != QStringLiteral("Yahoo Finance")) {
                    qCritical() << "MARKET_DATA_SMOKE_FAILED" << result.error;
                    application.exit(3);
                    return;
                }
                qInfo() << "MARKET_DATA_SMOKE_OK" << result.source
                        << result.bars.size();
                application.exit(0);
            });
        timeout.start(20'000);
        return application.exec();
    }

    const auto smokeMode = parser.isSet(smokeOption);
    tvchart::MainWindow window(nullptr, !smokeMode, !smokeMode);
    if (smokeMode) {
        QTimer timeout;
        timeout.setSingleShot(true);
        QObject::connect(&timeout, &QTimer::timeout, &application, [&application]() {
            qCritical() << "SMOKE_TIMEOUT";
            application.exit(2);
        });
        auto* chartView = window.findChild<tvchart::ChartView*>();
        if (!chartView) {
            qCritical() << "SMOKE_CHART_VIEW_MISSING";
            return 4;
        }
        QObject::connect(
            chartView,
            &tvchart::ChartView::chartError,
            &application,
            [&application](const QString& message) {
                qCritical() << "SMOKE_CHART_ERROR" << message;
                application.exit(3);
            });
        QObject::connect(&window, &tvchart::MainWindow::chartReady, &application, [&]() {
            auto* indicatorTable =
                window.findChild<QTableWidget*>(QStringLiteral("indicatorTable"));
            auto* scaleSelector =
                window.findChild<QComboBox*>(QStringLiteral("scaleSelector"));
            auto* targetTable = window.findChild<QTableWidget*>(
                QStringLiteral("targetEstimateTable"));
            auto* eventTable = window.findChild<QTableWidget*>(
                QStringLiteral("researchEventTable"));
            auto* marginValue = window.findChild<QDoubleSpinBox*>(
                QStringLiteral("marginLongValueInput"));
            if (!indicatorTable || !scaleSelector || !targetTable ||
                !eventTable || !marginValue) {
                qCritical() << "SMOKE_ANALYSIS_CONTROLS_MISSING";
                application.exit(4);
                return;
            }
            indicatorTable->item(3, 0)->setCheckState(Qt::Checked);
            scaleSelector->setCurrentIndex(
                scaleSelector->findData(QStringLiteral("logarithmic")));
            QTimer::singleShot(250, &window, [indicatorTable]() {
                indicatorTable->item(4, 0)->setCheckState(Qt::Checked);
            });
            QTimer::singleShot(500, &window, [indicatorTable, scaleSelector]() {
                indicatorTable->item(3, 0)->setCheckState(Qt::Unchecked);
                indicatorTable->item(4, 0)->setCheckState(Qt::Unchecked);
                scaleSelector->setCurrentIndex(
                    scaleSelector->findData(QStringLiteral("percentage")));
            });
            QTimer::singleShot(750, &application, [&]() {
                qInfo() << "SMOKE_OK";
                timeout.stop();
                window.close();
                application.exit(0);
            });
        });
        timeout.start(20'000);
        window.show();
        return application.exec();
    }

    window.show();
    return application.exec();
}
