#pragma once

#include "domain/bar.hpp"

#include <QByteArray>
#include <QString>

namespace tvchart {

struct MarketDataParseResult {
    Bars bars;
    QString error;

    [[nodiscard]] bool ok() const noexcept {
        return error.isEmpty() && !bars.empty();
    }
};

class MarketDataParser final {
public:
    [[nodiscard]] static MarketDataParseResult parseYahoo(const QByteArray& payload);
    [[nodiscard]] static MarketDataParseResult parseTwelveData(const QByteArray& payload);
};

} // namespace tvchart
