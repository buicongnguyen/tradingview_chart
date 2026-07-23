#pragma once

#include "domain/bar.hpp"

#include <QString>
#include <QStringView>

namespace tvchart {

struct CsvLoadResult {
    Bars bars;
    QString error;

    [[nodiscard]] bool ok() const noexcept {
        return error.isEmpty();
    }
};

class CsvBarLoader final {
public:
    [[nodiscard]] static CsvLoadResult loadFile(const QString& path);
    [[nodiscard]] static CsvLoadResult parse(QStringView content);
};

} // namespace tvchart
