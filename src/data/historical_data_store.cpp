#include "data/historical_data_store.hpp"

#include "watchlists/watchlist_workspace.hpp"

#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QVariant>
#include <QUuid>

#include <limits>
#include <utility>

namespace tvchart {
namespace {

constexpr auto kSchemaVersion = 1;

[[nodiscard]] bool validTimeframe(const Timeframe timeframe) noexcept {
    switch (timeframe) {
    case Timeframe::OneMinute:
    case Timeframe::FiveMinutes:
    case Timeframe::FifteenMinutes:
    case Timeframe::OneHour:
    case Timeframe::OneDay:
        return true;
    }
    return false;
}

[[nodiscard]] int deliveryModeId(const DataDeliveryMode mode) noexcept {
    switch (mode) {
    case DataDeliveryMode::Polled:
        return 0;
    case DataDeliveryMode::LocalFile:
        return 1;
    case DataDeliveryMode::Synthetic:
        return 2;
    }
    return 2;
}

[[nodiscard]] std::optional<DataDeliveryMode> parseDeliveryMode(
    const int value) noexcept {
    switch (value) {
    case 0:
        return DataDeliveryMode::Polled;
    case 1:
        return DataDeliveryMode::LocalFile;
    case 2:
        return DataDeliveryMode::Synthetic;
    default:
        return std::nullopt;
    }
}

[[nodiscard]] QString queryError(
    const QString& operation,
    const QSqlQuery& query) {
    return QStringLiteral("%1: %2").arg(operation, query.lastError().text());
}

[[nodiscard]] QString normalizedProvider(QString provider) {
    return provider.trimmed();
}

[[nodiscard]] QString validateIdentity(
    const QString& provider,
    const QString& symbol,
    const Timeframe timeframe) {
    if (provider.trimmed().isEmpty() || provider.size() > 120) {
        return QStringLiteral("Historical provider identity is invalid.");
    }
    const auto normalizedSymbol = normalizeWatchlistSymbol(symbol);
    const NamedWatchlist candidate{
        .id = QStringLiteral("cache-validation"),
        .name = QStringLiteral("Cache validation"),
        .entries = {{.symbol = normalizedSymbol}},
    };
    if (!validTimeframe(timeframe) || !validateWatchlist(candidate).isEmpty()) {
        return QStringLiteral("Historical symbol or timeframe is invalid.");
    }
    return {};
}

[[nodiscard]] QString validateMetadata(
    const MarketDataMetadata& metadata) {
    if (metadata.deliveryMode == DataDeliveryMode::Synthetic) {
        return QStringLiteral(
            "Synthetic demo series are not stored as provider history.");
    }
    if (metadata.retrievedAtUtc <= 0 ||
        metadata.exchange.size() > 160 ||
        metadata.currency.size() > 32 ||
        metadata.timezone.size() > 128 ||
        metadata.instrumentType.size() > 160 ||
        metadata.interval.size() > 64 ||
        (metadata.exchangeDelayMinutes &&
         (*metadata.exchangeDelayMinutes < 0 ||
          *metadata.exchangeDelayMinutes > 24 * 60))) {
        return QStringLiteral("Historical provenance metadata is invalid.");
    }
    return {};
}

} // namespace

class HistoricalDataStore::Impl final {
public:
    explicit Impl(QString path)
        : databasePath(std::move(path)),
          connectionName(
              QStringLiteral("tradingview-chart-history-%1")
                  .arg(QUuid::createUuid().toString(QUuid::WithoutBraces))) {}

    QString databasePath;
    QString connectionName;
    QSqlDatabase database;
    QString error;
};

HistoricalDataStore::HistoricalDataStore(QString databasePath)
    : impl_(std::make_unique<Impl>(std::move(databasePath))) {}

HistoricalDataStore::~HistoricalDataStore() {
    if (!impl_) {
        return;
    }
    if (impl_->database.isValid()) {
        impl_->database.close();
        impl_->database = QSqlDatabase{};
    }
    QSqlDatabase::removeDatabase(impl_->connectionName);
}

bool HistoricalDataStore::open() {
    impl_->error.clear();
    if (impl_->database.isOpen()) {
        return true;
    }
    if (impl_->databasePath.trimmed().isEmpty()) {
        impl_->error = QStringLiteral("Historical cache path is empty.");
        return false;
    }

    if (impl_->databasePath != QStringLiteral(":memory:")) {
        const QFileInfo info(impl_->databasePath);
        if (!QDir{}.mkpath(info.absolutePath())) {
            impl_->error =
                QStringLiteral("Could not create the historical cache directory.");
            return false;
        }
    }

    impl_->database =
        QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), impl_->connectionName);
    impl_->database.setDatabaseName(impl_->databasePath);
    if (!impl_->database.open()) {
        impl_->error =
            QStringLiteral("Could not open historical cache: %1")
                .arg(impl_->database.lastError().text());
        return false;
    }

    QSqlQuery pragma(impl_->database);
    if (!pragma.exec(QStringLiteral("PRAGMA foreign_keys = ON")) ||
        !pragma.exec(QStringLiteral("PRAGMA busy_timeout = 5000"))) {
        impl_->error = queryError(
            QStringLiteral("Could not configure historical cache"),
            pragma);
        impl_->database.close();
        return false;
    }
    if (impl_->databasePath != QStringLiteral(":memory:") &&
        !pragma.exec(QStringLiteral("PRAGMA journal_mode = WAL"))) {
        impl_->error = queryError(
            QStringLiteral("Could not enable historical cache WAL mode"),
            pragma);
        impl_->database.close();
        return false;
    }

    const QStringList schema{
        QStringLiteral(
            "CREATE TABLE IF NOT EXISTS schema_info ("
            "version INTEGER NOT NULL)"),
        QStringLiteral(
            "CREATE TABLE IF NOT EXISTS historical_series ("
            "provider TEXT NOT NULL,"
            "symbol TEXT NOT NULL,"
            "timeframe INTEGER NOT NULL,"
            "delivery_mode INTEGER NOT NULL,"
            "exchange_name TEXT NOT NULL,"
            "currency TEXT NOT NULL,"
            "timezone TEXT NOT NULL,"
            "instrument_type TEXT NOT NULL,"
            "interval_label TEXT NOT NULL,"
            "exchange_delay_minutes INTEGER,"
            "retrieved_at_utc INTEGER NOT NULL,"
            "cached_at_utc_ms INTEGER NOT NULL,"
            "PRIMARY KEY(provider, symbol, timeframe))"),
        QStringLiteral(
            "CREATE TABLE IF NOT EXISTS historical_bars ("
            "provider TEXT NOT NULL,"
            "symbol TEXT NOT NULL,"
            "timeframe INTEGER NOT NULL,"
            "timestamp INTEGER NOT NULL,"
            "open REAL NOT NULL,"
            "high REAL NOT NULL,"
            "low REAL NOT NULL,"
            "close REAL NOT NULL,"
            "volume REAL NOT NULL,"
            "PRIMARY KEY(provider, symbol, timeframe, timestamp),"
            "FOREIGN KEY(provider, symbol, timeframe) REFERENCES "
            "historical_series(provider, symbol, timeframe) ON DELETE CASCADE)"),
        QStringLiteral(
            "CREATE INDEX IF NOT EXISTS historical_bars_lookup "
            "ON historical_bars(symbol, timeframe, timestamp)"),
    };
    for (const auto& statement : schema) {
        if (!pragma.exec(statement)) {
            impl_->error =
                queryError(QStringLiteral("Could not initialize cache"), pragma);
            impl_->database.close();
            return false;
        }
    }

    if (!pragma.exec(QStringLiteral("SELECT version FROM schema_info LIMIT 1"))) {
        impl_->error =
            queryError(QStringLiteral("Could not read cache schema"), pragma);
        impl_->database.close();
        return false;
    }
    if (!pragma.next()) {
        pragma.prepare(QStringLiteral("INSERT INTO schema_info(version) VALUES(?)"));
        pragma.addBindValue(kSchemaVersion);
        if (!pragma.exec()) {
            impl_->error =
                queryError(QStringLiteral("Could not record cache schema"), pragma);
            impl_->database.close();
            return false;
        }
    } else if (pragma.value(0).toInt() != kSchemaVersion) {
        impl_->error = QStringLiteral(
            "Historical cache schema is newer or unsupported.");
        impl_->database.close();
        return false;
    }
    return true;
}

bool HistoricalDataStore::isOpen() const noexcept {
    return impl_->database.isOpen();
}

QString HistoricalDataStore::lastError() const {
    return impl_->error;
}

QString HistoricalDataStore::upsertSeries(
    QString provider,
    QString symbol,
    const Timeframe timeframe,
    const Bars& bars,
    const MarketDataMetadata& metadata) {
    if (!isOpen()) {
        return QStringLiteral("Historical cache is not open.");
    }
    provider = normalizedProvider(std::move(provider));
    symbol = normalizeWatchlistSymbol(std::move(symbol));
    if (const auto identityError =
            validateIdentity(provider, symbol, timeframe);
        !identityError.isEmpty()) {
        return identityError;
    }
    if (const auto metadataError = validateMetadata(metadata);
        !metadataError.isEmpty()) {
        return metadataError;
    }
    if (const auto barsError = validateBars(bars)) {
        return QStringLiteral("Historical bars are invalid: %1")
            .arg(QString::fromStdString(*barsError));
    }
    if (!impl_->database.transaction()) {
        return QStringLiteral("Could not start cache transaction: %1")
            .arg(impl_->database.lastError().text());
    }
    const auto rollbackWith = [this](QString error) {
        impl_->database.rollback();
        return error;
    };

    QSqlQuery series(impl_->database);
    series.prepare(QStringLiteral(
        "INSERT INTO historical_series("
        "provider,symbol,timeframe,delivery_mode,exchange_name,currency,"
        "timezone,instrument_type,interval_label,exchange_delay_minutes,"
        "retrieved_at_utc,cached_at_utc_ms) "
        "VALUES(?,?,?,?,?,?,?,?,?,?,?,?) "
        "ON CONFLICT(provider,symbol,timeframe) DO UPDATE SET "
        "delivery_mode=excluded.delivery_mode,"
        "exchange_name=excluded.exchange_name,currency=excluded.currency,"
        "timezone=excluded.timezone,instrument_type=excluded.instrument_type,"
        "interval_label=excluded.interval_label,"
        "exchange_delay_minutes=excluded.exchange_delay_minutes,"
        "retrieved_at_utc=excluded.retrieved_at_utc,"
        "cached_at_utc_ms=excluded.cached_at_utc_ms"));
    series.addBindValue(provider);
    series.addBindValue(symbol);
    series.addBindValue(static_cast<int>(timeframe));
    series.addBindValue(deliveryModeId(metadata.deliveryMode));
    series.addBindValue(metadata.exchange);
    series.addBindValue(metadata.currency);
    series.addBindValue(metadata.timezone);
    series.addBindValue(metadata.instrumentType);
    series.addBindValue(metadata.interval);
    if (metadata.exchangeDelayMinutes) {
        series.addBindValue(*metadata.exchangeDelayMinutes);
    } else {
        series.addBindValue(QVariant{});
    }
    series.addBindValue(
        QVariant::fromValue<qlonglong>(metadata.retrievedAtUtc));
    series.addBindValue(QDateTime::currentMSecsSinceEpoch());
    if (!series.exec()) {
        return rollbackWith(
            queryError(QStringLiteral("Could not upsert series"), series));
    }

    QSqlQuery insert(impl_->database);
    insert.prepare(QStringLiteral(
        "INSERT INTO historical_bars("
        "provider,symbol,timeframe,timestamp,open,high,low,close,volume) "
        "VALUES(?,?,?,?,?,?,?,?,?) "
        "ON CONFLICT(provider,symbol,timeframe,timestamp) DO UPDATE SET "
        "open=excluded.open,high=excluded.high,low=excluded.low,"
        "close=excluded.close,volume=excluded.volume"));
    for (const auto& bar : bars) {
        insert.bindValue(0, provider);
        insert.bindValue(1, symbol);
        insert.bindValue(2, static_cast<int>(timeframe));
        insert.bindValue(3, QVariant::fromValue<qlonglong>(bar.timestamp));
        insert.bindValue(4, bar.open);
        insert.bindValue(5, bar.high);
        insert.bindValue(6, bar.low);
        insert.bindValue(7, bar.close);
        insert.bindValue(8, bar.volume);
        if (!insert.exec()) {
            return rollbackWith(
                queryError(QStringLiteral("Could not upsert bars"), insert));
        }
    }
    if (!impl_->database.commit()) {
        return rollbackWith(
            QStringLiteral("Could not commit cache transaction: %1")
                .arg(impl_->database.lastError().text()));
    }
    return {};
}

CachedHistoricalSeries HistoricalDataStore::loadSeries(
    QString provider,
    QString symbol,
    const Timeframe timeframe,
    const std::optional<std::int64_t> fromTimestamp,
    const std::optional<std::int64_t> throughTimestamp) const {
    CachedHistoricalSeries result;
    provider = normalizedProvider(std::move(provider));
    symbol = normalizeWatchlistSymbol(std::move(symbol));
    result.key = {
        .provider = provider,
        .symbol = symbol,
        .timeframe = timeframe,
    };
    if (!isOpen()) {
        result.error = QStringLiteral("Historical cache is not open.");
        return result;
    }
    if (const auto identityError =
            validateIdentity(provider, symbol, timeframe);
        !identityError.isEmpty()) {
        result.error = identityError;
        return result;
    }
    if (fromTimestamp && throughTimestamp &&
        *fromTimestamp > *throughTimestamp) {
        result.error = QStringLiteral("Historical range is invalid.");
        return result;
    }

    QSqlQuery series(impl_->database);
    series.prepare(QStringLiteral(
        "SELECT delivery_mode,exchange_name,currency,timezone,instrument_type,"
        "interval_label,exchange_delay_minutes,retrieved_at_utc,"
        "cached_at_utc_ms FROM historical_series "
        "WHERE provider=? AND symbol=? AND timeframe=?"));
    series.addBindValue(provider);
    series.addBindValue(symbol);
    series.addBindValue(static_cast<int>(timeframe));
    if (!series.exec()) {
        result.error =
            queryError(QStringLiteral("Could not read series"), series);
        return result;
    }
    if (!series.next()) {
        result.error = QStringLiteral("No cached history is available.");
        return result;
    }
    const auto mode = parseDeliveryMode(series.value(0).toInt());
    if (!mode || *mode == DataDeliveryMode::Synthetic) {
        result.error = QStringLiteral("Cached provenance is invalid.");
        return result;
    }
    result.metadata = {
        .deliveryMode = *mode,
        .exchange = series.value(1).toString(),
        .currency = series.value(2).toString(),
        .timezone = series.value(3).toString(),
        .instrumentType = series.value(4).toString(),
        .interval = series.value(5).toString(),
        .exchangeDelayMinutes =
            series.value(6).isNull()
                ? std::nullopt
                : std::optional<int>{series.value(6).toInt()},
        .retrievedAtUtc = series.value(7).toLongLong(),
    };
    result.cachedAtUtcMilliseconds = series.value(8).toLongLong();
    if (const auto metadataError = validateMetadata(result.metadata);
        !metadataError.isEmpty() ||
        result.cachedAtUtcMilliseconds <= 0) {
        result.error =
            metadataError.isEmpty()
                ? QStringLiteral("Cached provenance timestamp is invalid.")
                : metadataError;
        return result;
    }

    QString statement = QStringLiteral(
        "SELECT timestamp,open,high,low,close,volume FROM historical_bars "
        "WHERE provider=? AND symbol=? AND timeframe=?");
    if (fromTimestamp) {
        statement += QStringLiteral(" AND timestamp>=?");
    }
    if (throughTimestamp) {
        statement += QStringLiteral(" AND timestamp<=?");
    }
    statement += QStringLiteral(" ORDER BY timestamp");

    QSqlQuery barsQuery(impl_->database);
    barsQuery.prepare(statement);
    barsQuery.addBindValue(provider);
    barsQuery.addBindValue(symbol);
    barsQuery.addBindValue(static_cast<int>(timeframe));
    if (fromTimestamp) {
        barsQuery.addBindValue(QVariant::fromValue<qlonglong>(*fromTimestamp));
    }
    if (throughTimestamp) {
        barsQuery.addBindValue(
            QVariant::fromValue<qlonglong>(*throughTimestamp));
    }
    if (!barsQuery.exec()) {
        result.error =
            queryError(QStringLiteral("Could not read cached bars"), barsQuery);
        return result;
    }
    while (barsQuery.next()) {
        result.bars.push_back({
            .timestamp = barsQuery.value(0).toLongLong(),
            .open = barsQuery.value(1).toDouble(),
            .high = barsQuery.value(2).toDouble(),
            .low = barsQuery.value(3).toDouble(),
            .close = barsQuery.value(4).toDouble(),
            .volume = barsQuery.value(5).toDouble(),
        });
    }
    if (const auto error = validateBars(result.bars)) {
        result.error =
            QStringLiteral("Cached bars failed validation: %1")
                .arg(QString::fromStdString(*error));
        result.bars.clear();
    }
    return result;
}

CachedHistoricalSeries HistoricalDataStore::loadLatestSeries(
    QString symbol,
    const Timeframe timeframe) const {
    symbol = normalizeWatchlistSymbol(std::move(symbol));
    if (!isOpen()) {
        return {.error = QStringLiteral("Historical cache is not open.")};
    }
    if (const auto identityError =
            validateIdentity(QStringLiteral("placeholder"), symbol, timeframe);
        !identityError.isEmpty()) {
        return {.error = identityError};
    }

    QSqlQuery query(impl_->database);
    query.prepare(QStringLiteral(
        "SELECT provider FROM historical_series "
        "WHERE symbol=? AND timeframe=? "
        "ORDER BY cached_at_utc_ms DESC,retrieved_at_utc DESC,provider ASC "
        "LIMIT 1"));
    query.addBindValue(symbol);
    query.addBindValue(static_cast<int>(timeframe));
    if (!query.exec()) {
        return {
            .error =
                queryError(QStringLiteral("Could not locate cached series"), query),
        };
    }
    if (!query.next()) {
        return {.error = QStringLiteral("No cached history is available.")};
    }
    return loadSeries(query.value(0).toString(), symbol, timeframe);
}

std::vector<CachedSeriesSummary> HistoricalDataStore::availableSeries() const {
    std::vector<CachedSeriesSummary> result;
    if (!isOpen()) {
        return result;
    }
    QSqlQuery query(impl_->database);
    if (!query.exec(QStringLiteral(
            "SELECT s.provider,s.symbol,s.timeframe,s.delivery_mode,"
            "s.exchange_name,s.currency,s.timezone,s.instrument_type,"
            "s.interval_label,s.exchange_delay_minutes,s.retrieved_at_utc,"
            "s.cached_at_utc_ms,COUNT(b.timestamp),MIN(b.timestamp),"
            "MAX(b.timestamp) FROM historical_series s "
            "JOIN historical_bars b ON b.provider=s.provider AND "
            "b.symbol=s.symbol AND b.timeframe=s.timeframe "
            "GROUP BY s.provider,s.symbol,s.timeframe "
            "ORDER BY s.symbol,s.timeframe,s.provider"))) {
        return result;
    }
    while (query.next()) {
        const auto mode = parseDeliveryMode(query.value(3).toInt());
        const auto timeframeValue = query.value(2).toInt();
        if (!mode || *mode == DataDeliveryMode::Synthetic ||
            timeframeValue < static_cast<int>(Timeframe::OneMinute) ||
            timeframeValue > static_cast<int>(Timeframe::OneDay)) {
            continue;
        }
        result.push_back({
            .key = {
                .provider = query.value(0).toString(),
                .symbol = query.value(1).toString(),
                .timeframe = static_cast<Timeframe>(timeframeValue),
            },
            .metadata = {
                .deliveryMode = *mode,
                .exchange = query.value(4).toString(),
                .currency = query.value(5).toString(),
                .timezone = query.value(6).toString(),
                .instrumentType = query.value(7).toString(),
                .interval = query.value(8).toString(),
                .exchangeDelayMinutes =
                    query.value(9).isNull()
                        ? std::nullopt
                        : std::optional<int>{query.value(9).toInt()},
                .retrievedAtUtc = query.value(10).toLongLong(),
            },
            .barCount = query.value(12).toULongLong(),
            .firstTimestamp = query.value(13).toLongLong(),
            .lastTimestamp = query.value(14).toLongLong(),
            .cachedAtUtcMilliseconds = query.value(11).toLongLong(),
        });
    }
    return result;
}

} // namespace tvchart
