#include "data/market_data_client.hpp"
#include "main_window.hpp"

#include <QApplication>
#include <QCommandLineParser>
#include <QComboBox>
#include <QDebug>
#include <QTimer>

int main(int argc, char* argv[]) {
    QApplication application(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("buicongnguyen"));
    QCoreApplication::setApplicationName(QStringLiteral("TradingViewChart"));
    QCoreApplication::setApplicationVersion(QStringLiteral("0.1.0"));

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
            auto* indicatorSelector =
                window.findChild<QComboBox*>(QStringLiteral("indicatorSelector"));
            if (!indicatorSelector) {
                qCritical() << "SMOKE_INDICATOR_SELECTOR_MISSING";
                application.exit(4);
                return;
            }
            indicatorSelector->setCurrentIndex(
                indicatorSelector->findData(
                    static_cast<int>(
                        tvchart::IndicatorKind::RelativeStrengthIndex)));
            QTimer::singleShot(250, &window, [indicatorSelector]() {
                indicatorSelector->setCurrentIndex(
                    indicatorSelector->findData(
                        static_cast<int>(
                            tvchart::IndicatorKind::
                                MovingAverageConvergenceDivergence)));
            });
            QTimer::singleShot(500, &window, [indicatorSelector]() {
                indicatorSelector->setCurrentIndex(
                    indicatorSelector->findData(
                        static_cast<int>(
                            tvchart::IndicatorKind::SimpleMovingAverage)));
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
