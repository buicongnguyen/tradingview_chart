#pragma once

#include "chart/chart_bridge.hpp"

#include <QWebEngineView>

class QWebChannel;

namespace tvchart {

class ChartView final : public QWebEngineView {
    Q_OBJECT

public:
    explicit ChartView(QWidget* parent = nullptr);

    [[nodiscard]] ChartBridge* bridge() noexcept;

signals:
    void chartReady();
    void chartError(const QString& message);

private:
    ChartBridge bridge_;
    QWebChannel* channel_{};
};

} // namespace tvchart
