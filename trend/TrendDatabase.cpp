#include "TrendDatabase.h"

#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlError>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QSet>

namespace Trend {

TrendDatabase::TrendDatabase() : m_connectionName(QStringLiteral("trend_db"))
{
}

TrendDatabase::~TrendDatabase()
{
    close();
}

// ---------------------------------------------------------------------------
// open / close / isOpen
// ---------------------------------------------------------------------------
bool TrendDatabase::open(const QString &dbPath, QString &error)
{
    close();

    QFileInfo fi(dbPath);

    // Ensure directory exists
    if (!QDir().mkpath(fi.absolutePath())) {
        error = QStringLiteral("Failed to create directory: %1").arg(fi.absolutePath());
        return false;
    }

    QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_connectionName);
    db.setDatabaseName(dbPath);

    if (!db.open()) {
        error = db.lastError().text();
        return false;
    }

    return initSchema(error);
}

void TrendDatabase::close()
{
    if (!QSqlDatabase::contains(m_connectionName)){
        return;
    }
        
    QSqlDatabase::database(m_connectionName).close();
    QSqlDatabase::removeDatabase(m_connectionName);
}

bool TrendDatabase::isOpen() const
{
    if (!QSqlDatabase::contains(m_connectionName)){
        return false;
    }
        
    return QSqlDatabase::database(m_connectionName).isOpen();
}

// ---------------------------------------------------------------------------
// Schema
// ---------------------------------------------------------------------------
bool TrendDatabase::initSchema(QString &error)
{
    QSqlDatabase db = QSqlDatabase::database(m_connectionName);
    QSqlQuery q(db);

    // WAL mode: allows concurrent read while writing
    // https://www.sqlite.org/wal.html (Write-Ahead Logging mode)
    q.exec(QStringLiteral("PRAGMA journal_mode=WAL"));

    const QStringList ddl = {
        QStringLiteral(
            "CREATE TABLE IF NOT EXISTS trend_data ("
            "  id     INTEGER PRIMARY KEY AUTOINCREMENT,"
            "  reg_id INTEGER NOT NULL,"
            "  ts     INTEGER NOT NULL,"
            "  value  INTEGER NOT NULL)"),  // raw quint16 register value

        QStringLiteral(
            "CREATE INDEX IF NOT EXISTS idx_trend_reg_ts "
            "ON trend_data (reg_id, ts)"),

        QStringLiteral(
            "CREATE TABLE IF NOT EXISTS trend_data_5m ("
            "  reg_id  INTEGER NOT NULL,"
            "  ts      INTEGER NOT NULL,"
            "  avg_val INTEGER NOT NULL,"  // rounded avg of raw quint16 values
            "  min_val INTEGER NOT NULL,"
            "  max_val INTEGER NOT NULL,"
            "  PRIMARY KEY (reg_id, ts))"),

        QStringLiteral(
            "CREATE TABLE IF NOT EXISTS trend_data_10m ("
            "  reg_id  INTEGER NOT NULL,"
            "  ts      INTEGER NOT NULL,"
            "  avg_val INTEGER NOT NULL,"  // rounded avg of raw quint16 values
            "  min_val INTEGER NOT NULL,"
            "  max_val INTEGER NOT NULL,"
            "  PRIMARY KEY (reg_id, ts))")
    };

    for (const QString &sql : ddl) {
        if (!q.exec(sql)) {
            error = q.lastError().text();
            return false;
        }
    }
    return true;
}

// ---------------------------------------------------------------------------
// Write 
// Code flow: TrendSampler::flush() → TrendDatabase::insertBatch() → TrendDatabase::rollup5m() → TrendDatabase::rollup10m()
// ---------------------------------------------------------------------------
bool TrendDatabase::insertBatch(const QList<TrendRawPoint> &points, QString &error)
{
    if (points.isEmpty())
        return true;

    QSqlDatabase db = QSqlDatabase::database(m_connectionName);
    QSqlQuery q(db);

    db.transaction();
    q.prepare(QStringLiteral(
        "INSERT INTO trend_data (reg_id, ts, value) VALUES (:id, :ts, :val)"));

    // Collect affected 5m buckets: regId → set of bucket timestamps
    QMap<int, QSet<qint64>> buckets;

    for (const TrendRawPoint &p : points) {
        q.bindValue(QStringLiteral(":id"),  p.regId);
        q.bindValue(QStringLiteral(":ts"),  p.ts);
        q.bindValue(QStringLiteral(":val"), p.value);

        if (!q.exec()) {
            error = q.lastError().text();
            db.rollback();
            return false;
        }
        buckets[p.regId].insert((p.ts / 300) * 300);
    }

    db.commit();

    // Trigger rollup for each affected bucket after commit
    for (auto it = buckets.cbegin(); it != buckets.cend(); ++it)
        for (qint64 bucketTs : it.value())
            rollup5m(it.key(), bucketTs);

    return true;
}

// ---------------------------------------------------------------------------
// Rollup: Raw → 5m → 10m
// ---------------------------------------------------------------------------

void TrendDatabase::rollup5m(int regId, qint64 bucketTs)
{
    QSqlDatabase db = QSqlDatabase::database(m_connectionName);
    QSqlQuery q(db);

    // Only aggregate when data past the bucket end exists (bucket is complete)
    q.prepare(QStringLiteral(
        "SELECT 1 FROM trend_data WHERE reg_id=:id AND ts>=:end LIMIT 1"));
    q.bindValue(QStringLiteral(":id"),  regId);
    q.bindValue(QStringLiteral(":end"), bucketTs + 300);
    if (!q.exec() || !q.next())
        return;

    q.prepare(QStringLiteral(
        "SELECT avg(value), min(value), max(value) "
        "FROM trend_data WHERE reg_id=:id AND ts>=:s AND ts<:e"));
    q.bindValue(QStringLiteral(":id"), regId);
    q.bindValue(QStringLiteral(":s"),  bucketTs);
    q.bindValue(QStringLiteral(":e"),  bucketTs + 300);
    if (!q.exec() || !q.next())
        return;

    const quint16 avg = static_cast<quint16>(qRound(q.value(0).toDouble()));
    const quint16 min = static_cast<quint16>(q.value(1).toUInt());
    const quint16 max = static_cast<quint16>(q.value(2).toUInt());

    q.prepare(QStringLiteral(
        "INSERT OR IGNORE INTO trend_data_5m (reg_id, ts, avg_val, min_val, max_val) "
        "VALUES (:id, :ts, :avg, :min, :max)"));
    q.bindValue(QStringLiteral(":id"),  regId);
    q.bindValue(QStringLiteral(":ts"),  bucketTs);
    q.bindValue(QStringLiteral(":avg"), avg);
    q.bindValue(QStringLiteral(":min"), min);
    q.bindValue(QStringLiteral(":max"), max);
    q.exec();

    rollup10m(regId, (bucketTs / 600) * 600);
}

void TrendDatabase::rollup10m(int regId, qint64 bucketTs)
{
    QSqlDatabase db = QSqlDatabase::database(m_connectionName);
    QSqlQuery q(db);

    // Requires both 5m buckets within this 10m window
    q.prepare(QStringLiteral(
        "SELECT COUNT(*) FROM trend_data_5m WHERE reg_id=:id AND ts>=:s AND ts<:e"));
    q.bindValue(QStringLiteral(":id"), regId);
    q.bindValue(QStringLiteral(":s"),  bucketTs);
    q.bindValue(QStringLiteral(":e"),  bucketTs + 600);
    if (!q.exec() || !q.next() || q.value(0).toInt() < 2)
        return;

    q.prepare(QStringLiteral(
        "SELECT avg(avg_val), min(min_val), max(max_val) "
        "FROM trend_data_5m WHERE reg_id=:id AND ts>=:s AND ts<:e"));
    q.bindValue(QStringLiteral(":id"), regId);
    q.bindValue(QStringLiteral(":s"),  bucketTs);
    q.bindValue(QStringLiteral(":e"),  bucketTs + 600);
    if (!q.exec() || !q.next())
        return;

    const quint16 avg = static_cast<quint16>(qRound(q.value(0).toDouble()));
    const quint16 min = static_cast<quint16>(q.value(1).toUInt());
    const quint16 max = static_cast<quint16>(q.value(2).toUInt());

    q.prepare(QStringLiteral(
        "INSERT OR IGNORE INTO trend_data_10m (reg_id, ts, avg_val, min_val, max_val) "
        "VALUES (:id, :ts, :avg, :min, :max)"));
    q.bindValue(QStringLiteral(":id"),  regId);
    q.bindValue(QStringLiteral(":ts"),  bucketTs);
    q.bindValue(QStringLiteral(":avg"), avg);
    q.bindValue(QStringLiteral(":min"), min);
    q.bindValue(QStringLiteral(":max"), max);
    q.exec();
}

// ---------------------------------------------------------------------------
// Query
// from, to: inclusive timestamps
// resolution: "raw" | "5m" | "10m"
// registers: empty = all channels recorded in [from, to]
// Each channel is downsampled to ≤1,000 points server-side
//
// Code flow: ApiServer::handleGetTrendData() → TrendDatabase::query()
// ---------------------------------------------------------------------------
QMap<int, QList<TrendPoint>> TrendDatabase::query(
    qint64 from, qint64 to,
    const QString &resolution,
    const QList<int> &registers,
    QString &error) const
{
    QSqlDatabase db = QSqlDatabase::database(m_connectionName);
    QSqlQuery q(db);
    QMap<int, QList<TrendPoint>> result;

    //---------------------------------------------------------------------------//
    // validate resolution
    // Determine which table to query based on resolution
    //---------------------------------------------------------------------------//
    const QString table = (resolution == QLatin1String("5m"))  ? QStringLiteral("trend_data_5m")  :
                          (resolution == QLatin1String("10m")) ? QStringLiteral("trend_data_10m") :
                                                                 QStringLiteral("trend_data");

    //---------------------------------------------------------------------------//
    // Discover channels if not specified
    //---------------------------------------------------------------------------//
    QList<int> channels = registers;

    //---------------------------------------------------------------------------//
    // If no channels specified, query the database for all channels that 
    // have data in the given time range
    //---------------------------------------------------------------------------//
    if (channels.isEmpty()) {
        q.prepare(QStringLiteral(
            "SELECT DISTINCT reg_id FROM %1 WHERE ts BETWEEN :from AND :to "
            "ORDER BY reg_id").arg(table));

        q.bindValue(QStringLiteral(":from"), from);
        q.bindValue(QStringLiteral(":to"),   to);

        if (!q.exec()) { 
            error = q.lastError().text(); 
            return {}; 
        }

        while (q.next()){
            channels.append(q.value(0).toInt());
        }
    }

    if (channels.isEmpty())
        return {};

    //---------------------------------------------------------------------------//
    // calculate the time range and step size for downsampling
    // Downsampling: bucket raw data into ≤1,000 time slots
    //---------------------------------------------------------------------------//
    const qint64 timeRange = qMax((qint64)1, to - from);
    const qint64 step      = qMax((qint64)1, timeRange / 1000);

    for (int regId : channels) {
        QString sql;
        if (resolution == QLatin1String("raw")) {
            sql = QStringLiteral(
                "SELECT %1 + ((ts - %1) / %2) * %2, avg(value), min(value), max(value) "
                "FROM trend_data "
                "WHERE reg_id=:id AND ts BETWEEN %1 AND %3 "
                "GROUP BY (ts - %1) / %2 "
                "ORDER BY 1 ASC"
            ).arg(from).arg(step).arg(to);
        } else {
            sql = QStringLiteral(
                "SELECT %1 + ((ts - %1) / %2) * %2, avg(avg_val), min(min_val), max(max_val) "
                "FROM %4 "
                "WHERE reg_id=:id AND ts BETWEEN %1 AND %3 "
                "GROUP BY (ts - %1) / %2 "
                "ORDER BY 1 ASC"
            ).arg(from).arg(step).arg(to).arg(table);
        }

        q.prepare(sql);
        q.bindValue(QStringLiteral(":id"), regId);
        if (!q.exec()) { error = q.lastError().text(); continue; }

        QList<TrendPoint> pts;
        while (q.next()) {
            TrendPoint pt;
            pt.ts  = q.value(0).toLongLong();
            pt.avg = static_cast<quint16>(qRound(q.value(1).toDouble()));
            pt.min = static_cast<quint16>(q.value(2).toUInt());
            pt.max = static_cast<quint16>(q.value(3).toUInt());
            pts.append(pt);
        }

        if (!pts.isEmpty())
            result[regId] = pts;
    }

    return result;
}

// ---------------------------------------------------------------------------
// Data range query
// ---------------------------------------------------------------------------
QPair<qint64, qint64> TrendDatabase::dataRange() const
{
    QSqlDatabase db = QSqlDatabase::database(m_connectionName);
    QSqlQuery q(db);
    
    q.exec(QStringLiteral("SELECT MIN(ts), MAX(ts) FROM trend_data"));
    
    if (q.next() && !q.value(0).isNull()){
        return { q.value(0).toLongLong(), q.value(1).toLongLong() };
    }    
    return { 0, 0 };
}

// ---------------------------------------------------------------------------
// Delete all trend data (channel config reset)
// ---------------------------------------------------------------------------
bool TrendDatabase::deleteAll(QString &error)
{
    QSqlDatabase db = QSqlDatabase::database(m_connectionName);
    QSqlQuery q(db);

    db.transaction();
    if (!q.exec(QStringLiteral("DELETE FROM trend_data"))    ||
        !q.exec(QStringLiteral("DELETE FROM trend_data_5m")) ||
        !q.exec(QStringLiteral("DELETE FROM trend_data_10m"))) {
        error = q.lastError().text();
        db.rollback();
        return false;
    }
    db.commit();
    return true;
}

// ---------------------------------------------------------------------------
// Purge expired rows (call from a 1-hour QTimer)
//
// WAL checkpoint: SQLite auto-checkpoints when WAL exceeds 1,000 pages (~4 MB).
// Since insertBatch() runs every 10 s, the WAL is kept small automatically.
// No explicit checkpoint call is needed here.
// ---------------------------------------------------------------------------
void TrendDatabase::purgeExpired()
{
    QSqlDatabase db = QSqlDatabase::database(m_connectionName);
    QSqlQuery q(db);
    const qint64 now = QDateTime::currentSecsSinceEpoch();

    q.prepare(QStringLiteral("DELETE FROM trend_data WHERE ts < :cut"));
    q.bindValue(QStringLiteral(":cut"), now - 365LL * 86400);
    q.exec();

    q.prepare(QStringLiteral("DELETE FROM trend_data_5m WHERE ts < :cut"));
    q.bindValue(QStringLiteral(":cut"), now - 3LL * 365 * 86400);
    q.exec();

    q.prepare(QStringLiteral("DELETE FROM trend_data_10m WHERE ts < :cut"));
    q.bindValue(QStringLiteral(":cut"), now - 5LL * 365 * 86400);
    q.exec();
}

} // namespace Trend
