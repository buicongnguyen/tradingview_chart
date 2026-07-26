#include "fundamentals/fundamental_store.hpp"

#include "watchlists/watchlist_workspace.hpp"

#include <QDir>
#include <QFileInfo>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QVariant>
#include <QUuid>

#include <optional>
#include <utility>

namespace tvchart {
namespace {

constexpr auto kSchemaVersion = 1;

[[nodiscard]] QString queryError(
    const QString& operation,
    const QSqlQuery& query) {
    return QStringLiteral("%1: %2")
        .arg(operation, query.lastError().text());
}

[[nodiscard]] std::optional<FundamentalMetric> parseMetric(
    const int value) {
    if (value < static_cast<int>(FundamentalMetric::Revenue) ||
        value > static_cast<int>(FundamentalMetric::DilutedShares)) {
        return std::nullopt;
    }
    return static_cast<FundamentalMetric>(value);
}

} // namespace

class FundamentalStore::Impl final {
public:
    explicit Impl(QString path)
        : databasePath(std::move(path)),
          connectionName(
              QStringLiteral("tradingview-chart-fundamentals-%1")
                  .arg(QUuid::createUuid().toString(
                      QUuid::WithoutBraces))) {}

    QString databasePath;
    QString connectionName;
    QSqlDatabase database;
    QString error;
};

FundamentalStore::FundamentalStore(QString databasePath)
    : impl_(std::make_unique<Impl>(std::move(databasePath))) {}

FundamentalStore::~FundamentalStore() {
    if (!impl_) {
        return;
    }
    if (impl_->database.isValid()) {
        impl_->database.close();
        impl_->database = QSqlDatabase{};
    }
    QSqlDatabase::removeDatabase(impl_->connectionName);
}

bool FundamentalStore::open() {
    impl_->error.clear();
    if (impl_->database.isOpen()) {
        return true;
    }
    if (impl_->databasePath.trimmed().isEmpty()) {
        impl_->error = QStringLiteral(
            "Fundamental cache path is empty.");
        return false;
    }
    if (impl_->databasePath != QStringLiteral(":memory:")) {
        const QFileInfo info(impl_->databasePath);
        if (!QDir{}.mkpath(info.absolutePath())) {
            impl_->error = QStringLiteral(
                "Could not create the fundamental cache directory.");
            return false;
        }
    }
    impl_->database = QSqlDatabase::addDatabase(
        QStringLiteral("QSQLITE"),
        impl_->connectionName);
    impl_->database.setDatabaseName(impl_->databasePath);
    if (!impl_->database.open()) {
        impl_->error =
            QStringLiteral("Could not open fundamental cache: %1")
                .arg(impl_->database.lastError().text());
        return false;
    }
    QSqlQuery query(impl_->database);
    if (!query.exec(QStringLiteral("PRAGMA foreign_keys = ON")) ||
        !query.exec(QStringLiteral("PRAGMA busy_timeout = 5000")) ||
        (impl_->databasePath != QStringLiteral(":memory:") &&
         !query.exec(QStringLiteral("PRAGMA journal_mode = WAL")))) {
        impl_->error = queryError(
            QStringLiteral("Could not configure fundamental cache"),
            query);
        impl_->database.close();
        return false;
    }
    const QStringList schema{
        QStringLiteral(
            "CREATE TABLE IF NOT EXISTS schema_info ("
            "version INTEGER NOT NULL)"),
        QStringLiteral(
            "CREATE TABLE IF NOT EXISTS companies ("
            "symbol TEXT PRIMARY KEY,"
            "cik TEXT NOT NULL,"
            "name TEXT NOT NULL,"
            "provider TEXT NOT NULL,"
            "retrieved_at_utc INTEGER NOT NULL)"),
        QStringLiteral(
            "CREATE TABLE IF NOT EXISTS facts ("
            "symbol TEXT NOT NULL,"
            "metric INTEGER NOT NULL,"
            "taxonomy TEXT NOT NULL,"
            "tag TEXT NOT NULL,"
            "unit TEXT NOT NULL,"
            "value REAL NOT NULL,"
            "period_start TEXT NOT NULL,"
            "period_end TEXT NOT NULL,"
            "filed_date TEXT NOT NULL,"
            "form TEXT NOT NULL,"
            "fiscal_year INTEGER NOT NULL,"
            "fiscal_period TEXT NOT NULL,"
            "accession TEXT NOT NULL,"
            "frame TEXT NOT NULL,"
            "source_url TEXT NOT NULL,"
            "retrieved_at_utc INTEGER NOT NULL,"
            "PRIMARY KEY(symbol,metric,tag,unit,period_start,period_end,"
            "filed_date,accession),"
            "FOREIGN KEY(symbol) REFERENCES companies(symbol) "
            "ON DELETE CASCADE)"),
        QStringLiteral(
            "CREATE INDEX IF NOT EXISTS facts_point_in_time "
            "ON facts(symbol,metric,filed_date,period_end)"),
    };
    for (const auto& statement : schema) {
        if (!query.exec(statement)) {
            impl_->error = queryError(
                QStringLiteral(
                    "Could not initialize fundamental cache"),
                query);
            impl_->database.close();
            return false;
        }
    }
    if (!query.exec(QStringLiteral(
            "SELECT version FROM schema_info LIMIT 1"))) {
        impl_->error = queryError(
            QStringLiteral(
                "Could not read fundamental cache schema"),
            query);
        impl_->database.close();
        return false;
    }
    if (!query.next()) {
        query.prepare(QStringLiteral(
            "INSERT INTO schema_info(version) VALUES(?)"));
        query.addBindValue(kSchemaVersion);
        if (!query.exec()) {
            impl_->error = queryError(
                QStringLiteral(
                    "Could not record fundamental cache schema"),
                query);
            impl_->database.close();
            return false;
        }
    } else if (query.value(0).toInt() != kSchemaVersion) {
        impl_->error = QStringLiteral(
            "Fundamental cache schema is newer or unsupported.");
        impl_->database.close();
        return false;
    }
    return true;
}

bool FundamentalStore::isOpen() const noexcept {
    return impl_->database.isOpen();
}

QString FundamentalStore::lastError() const {
    return impl_->error;
}

QString FundamentalStore::upsertCompany(
    const FundamentalCompany& company) {
    if (!isOpen()) {
        return QStringLiteral("Fundamental cache is not open.");
    }
    if (const auto error = validateFundamentalCompany(company);
        !error.isEmpty()) {
        return error;
    }
    if (!impl_->database.transaction()) {
        return QStringLiteral(
                   "Could not start fundamental cache transaction: %1")
            .arg(impl_->database.lastError().text());
    }
    const auto rollback = [this](QString error) {
        impl_->database.rollback();
        return error;
    };
    QSqlQuery existing(impl_->database);
    existing.prepare(QStringLiteral(
        "SELECT cik FROM companies WHERE symbol=?"));
    existing.addBindValue(company.symbol);
    if (!existing.exec()) {
        return rollback(queryError(
            QStringLiteral(
                "Could not verify cached fundamental identity"),
            existing));
    }
    if (existing.next() &&
        existing.value(0).toString() != company.cik) {
        QSqlQuery remove(impl_->database);
        remove.prepare(QStringLiteral(
            "DELETE FROM companies WHERE symbol=?"));
        remove.addBindValue(company.symbol);
        if (!remove.exec()) {
            return rollback(queryError(
                QStringLiteral(
                    "Could not replace changed fundamental identity"),
                remove));
        }
    }
    QSqlQuery profile(impl_->database);
    profile.prepare(QStringLiteral(
        "INSERT INTO companies(symbol,cik,name,provider,retrieved_at_utc) "
        "VALUES(?,?,?,?,?) "
        "ON CONFLICT(symbol) DO UPDATE SET "
        "cik=excluded.cik,name=excluded.name,provider=excluded.provider,"
        "retrieved_at_utc=excluded.retrieved_at_utc"));
    profile.addBindValue(company.symbol);
    profile.addBindValue(company.cik);
    profile.addBindValue(company.name);
    profile.addBindValue(company.provider);
    profile.addBindValue(
        QVariant::fromValue<qlonglong>(company.retrievedAtUtc));
    if (!profile.exec()) {
        return rollback(queryError(
            QStringLiteral(
                "Could not upsert fundamental company"),
            profile));
    }

    QSqlQuery insert(impl_->database);
    insert.prepare(QStringLiteral(
        "INSERT INTO facts(symbol,metric,taxonomy,tag,unit,value,"
        "period_start,period_end,filed_date,form,fiscal_year,"
        "fiscal_period,accession,frame,source_url,retrieved_at_utc) "
        "VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?) "
        "ON CONFLICT(symbol,metric,tag,unit,period_start,period_end,"
        "filed_date,accession) DO UPDATE SET "
        "value=excluded.value,form=excluded.form,"
        "fiscal_year=excluded.fiscal_year,"
        "fiscal_period=excluded.fiscal_period,frame=excluded.frame,"
        "source_url=excluded.source_url,"
        "retrieved_at_utc=excluded.retrieved_at_utc"));
    for (const auto& fact : company.facts) {
        insert.bindValue(0, company.symbol);
        insert.bindValue(1, static_cast<int>(fact.metric));
        insert.bindValue(2, fact.taxonomy);
        insert.bindValue(3, fact.tag);
        insert.bindValue(4, fact.unit);
        insert.bindValue(5, fact.value);
        insert.bindValue(
            6,
            fact.periodStart.isValid()
                ? fact.periodStart.toString(Qt::ISODate)
                : QStringLiteral(""));
        insert.bindValue(7, fact.periodEnd.toString(Qt::ISODate));
        insert.bindValue(8, fact.filedDate.toString(Qt::ISODate));
        insert.bindValue(9, fact.form);
        insert.bindValue(10, fact.fiscalYear);
        insert.bindValue(11, fact.fiscalPeriod);
        insert.bindValue(12, fact.accession);
        insert.bindValue(
            13,
            fact.frame.isEmpty()
                ? QStringLiteral("")
                : fact.frame);
        insert.bindValue(14, fact.sourceUrl);
        insert.bindValue(
            15,
            QVariant::fromValue<qlonglong>(
                fact.retrievedAtUtc));
        if (!insert.exec()) {
            return rollback(queryError(
                QStringLiteral(
                    "Could not upsert fundamental facts"),
                insert));
        }
    }
    if (!impl_->database.commit()) {
        return rollback(
            QStringLiteral(
                "Could not commit fundamental cache transaction: %1")
                .arg(impl_->database.lastError().text()));
    }
    return {};
}

StoredFundamentalCompany FundamentalStore::loadCompany(
    QString symbol) const {
    StoredFundamentalCompany result;
    symbol = normalizeWatchlistSymbol(std::move(symbol));
    if (!isOpen()) {
        result.error = QStringLiteral(
            "Fundamental cache is not open.");
        return result;
    }
    QSqlQuery profile(impl_->database);
    profile.prepare(QStringLiteral(
        "SELECT cik,name,provider,retrieved_at_utc "
        "FROM companies WHERE symbol=?"));
    profile.addBindValue(symbol);
    if (!profile.exec()) {
        result.error = queryError(
            QStringLiteral(
                "Could not read fundamental company"),
            profile);
        return result;
    }
    if (!profile.next()) {
        result.error = QStringLiteral(
            "No cached SEC fundamentals are available.");
        return result;
    }
    result.company = {
        .symbol = symbol,
        .cik = profile.value(0).toString(),
        .name = profile.value(1).toString(),
        .provider = profile.value(2).toString(),
        .retrievedAtUtc = profile.value(3).toLongLong(),
    };
    QSqlQuery facts(impl_->database);
    facts.prepare(QStringLiteral(
        "SELECT metric,taxonomy,tag,unit,value,period_start,"
        "period_end,filed_date,form,fiscal_year,fiscal_period,"
        "accession,frame,source_url,retrieved_at_utc "
        "FROM facts WHERE symbol=? "
        "ORDER BY filed_date,period_end,metric,tag,unit,period_start,"
        "accession"));
    facts.addBindValue(symbol);
    if (!facts.exec()) {
        result.error = queryError(
            QStringLiteral("Could not read fundamental facts"),
            facts);
        return result;
    }
    while (facts.next()) {
        const auto metric = parseMetric(facts.value(0).toInt());
        if (!metric) {
            result.error = QStringLiteral(
                "Cached fundamental metric is invalid.");
            result.company.facts.clear();
            return result;
        }
        FundamentalFact fact{
            .symbol = symbol,
            .cik = result.company.cik,
            .metric = *metric,
            .taxonomy = facts.value(1).toString(),
            .tag = facts.value(2).toString(),
            .unit = facts.value(3).toString(),
            .value = facts.value(4).toDouble(),
            .periodStart =
                QDate::fromString(
                    facts.value(5).toString(),
                    Qt::ISODate),
            .periodEnd =
                QDate::fromString(
                    facts.value(6).toString(),
                    Qt::ISODate),
            .filedDate =
                QDate::fromString(
                    facts.value(7).toString(),
                    Qt::ISODate),
            .form = facts.value(8).toString(),
            .fiscalYear = facts.value(9).toInt(),
            .fiscalPeriod = facts.value(10).toString(),
            .accession = facts.value(11).toString(),
            .frame = facts.value(12).toString(),
            .sourceUrl = facts.value(13).toString(),
            .retrievedAtUtc = facts.value(14).toLongLong(),
        };
        if (!validateFundamentalFact(fact).isEmpty()) {
            result.error = QStringLiteral(
                "Cached fundamental fact failed validation.");
            result.company.facts.clear();
            return result;
        }
        result.company.facts.push_back(std::move(fact));
    }
    if (result.company.facts.empty()) {
        result.error = QStringLiteral(
            "Cached fundamental company contains no facts.");
    }
    return result;
}

std::vector<FundamentalCompanySummary>
FundamentalStore::availableCompanies() const {
    std::vector<FundamentalCompanySummary> result;
    if (!isOpen()) {
        return result;
    }
    QSqlQuery query(impl_->database);
    if (!query.exec(QStringLiteral(
            "SELECT c.symbol,c.cik,c.name,MAX(f.filed_date),"
            "COUNT(f.metric),c.retrieved_at_utc "
            "FROM companies c JOIN facts f ON f.symbol=c.symbol "
            "GROUP BY c.symbol,c.cik,c.name,c.retrieved_at_utc "
            "ORDER BY c.symbol"))) {
        return result;
    }
    while (query.next()) {
        result.push_back({
            .symbol = query.value(0).toString(),
            .cik = query.value(1).toString(),
            .name = query.value(2).toString(),
            .latestFiledDate =
                QDate::fromString(
                    query.value(3).toString(),
                    Qt::ISODate),
            .factCount =
                static_cast<std::size_t>(
                    query.value(4).toULongLong()),
            .retrievedAtUtc = query.value(5).toLongLong(),
        });
    }
    return result;
}

} // namespace tvchart
