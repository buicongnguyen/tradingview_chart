#pragma once

#include <QString>

namespace tvchart {

// Quoting alone does not stop spreadsheet applications from interpreting
// user-controlled text as a formula.
[[nodiscard]] QString protectSpreadsheetCsvText(QString value);
[[nodiscard]] QString restoreSpreadsheetCsvText(QString value);

} // namespace tvchart
