#include "util/csv_security.hpp"

namespace tvchart {
namespace {

[[nodiscard]] bool formulaLeading(const QString& value) {
    if (value.isEmpty()) {
        return false;
    }

    qsizetype index = 0;
    while (index < value.size() && value.at(index).isSpace() &&
           value.at(index) != u'\t' && value.at(index) != u'\r') {
        ++index;
    }
    if (index >= value.size()) {
        return false;
    }

    const auto first = value.at(index);
    return first == u'=' || first == u'+' || first == u'-' ||
           first == u'@' || first == u'\t' || first == u'\r';
}

} // namespace

QString protectSpreadsheetCsvText(QString value) {
    if (formulaLeading(value)) {
        value.prepend(u'\'');
    }
    return value;
}

QString restoreSpreadsheetCsvText(QString value) {
    if (value.startsWith(u'\'') && formulaLeading(value.sliced(1))) {
        value.removeFirst();
    }
    return value;
}

} // namespace tvchart
