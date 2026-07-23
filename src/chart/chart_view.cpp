#include "chart/chart_view.hpp"

#include <QWebChannel>
#include <QWebEnginePage>
#include <QWebEngineProfile>
#include <QWebEngineUrlRequestInfo>
#include <QWebEngineUrlRequestInterceptor>

namespace tvchart {
namespace {

class LocalOnlyRequestInterceptor final : public QWebEngineUrlRequestInterceptor {
public:
    using QWebEngineUrlRequestInterceptor::QWebEngineUrlRequestInterceptor;

    void interceptRequest(QWebEngineUrlRequestInfo& info) override {
        const auto scheme = info.requestUrl().scheme().toLower();
        if (scheme == QStringLiteral("http") || scheme == QStringLiteral("https")) {
            info.block(true);
        }
    }
};

} // namespace

ChartView::ChartView(QWidget* parent)
    : QWebEngineView(parent),
      bridge_(this),
      channel_(new QWebChannel(this)) {
    auto* profile = new QWebEngineProfile(this);
    profile->setPersistentCookiesPolicy(QWebEngineProfile::NoPersistentCookies);
    profile->setHttpCacheType(QWebEngineProfile::MemoryHttpCache);
    profile->setUrlRequestInterceptor(new LocalOnlyRequestInterceptor(profile));

    auto* webPage = new QWebEnginePage(profile, this);
    setPage(webPage);
    setContextMenuPolicy(Qt::NoContextMenu);

    channel_->registerObject(QStringLiteral("chartBridge"), &bridge_);
    page()->setWebChannel(channel_);

    connect(&bridge_, &ChartBridge::ready, this, &ChartView::chartReady);
    connect(&bridge_, &ChartBridge::errorReported, this, &ChartView::chartError);
    connect(
        page(),
        &QWebEnginePage::renderProcessTerminated,
        this,
        [this](QWebEnginePage::RenderProcessTerminationStatus, const int exitCode) {
            emit chartError(
                QStringLiteral("The chart renderer stopped unexpectedly (exit code %1).")
                    .arg(exitCode));
        });
    connect(this, &QWebEngineView::loadFinished, this, [this](const bool ok) {
        if (!ok) {
            emit chartError(QStringLiteral("The embedded chart page could not be loaded."));
        }
    });

    load(QUrl(QStringLiteral("qrc:/web/assets/web/index.html")));
}

ChartBridge* ChartView::bridge() noexcept {
    return &bridge_;
}

} // namespace tvchart
