#include "Logger.h"

#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlError>
#include <QFileInfo>
#include <QDir>

#include <QMutex>
#include <QMutexLocker>
#include <QDateTime>
#include <QDebug>
#include <QThread>
#include <QThreadStorage>

namespace Util {

struct LoggerState
{
    QString dbPath;
    int maxLogRows = 1000;
    bool printToDebug = false;
    bool initialized = false;

    qint64 currentRowCount = 0;
    QMutex rowCountMutex;
};

LoggerState g_logger;

// QSqlDatabase connections may only be used from the thread that created them,
// so each calling thread (main, SerialWorker, TcpWorker, ...) gets its own connection.
QThreadStorage<QString> g_threadConnectionName;

static void createLogTable(const QSqlDatabase &db)
{
    QSqlQuery q(db);
    q.exec(QStringLiteral(
        "CREATE TABLE IF NOT EXISTS logs ("
        "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  timestamp TEXT NOT NULL,"
        "  level TEXT NOT NULL,"
        "  message TEXT NOT NULL"
        ")"
        ));
}

static QSqlDatabase threadConnection()
{
    if (g_threadConnectionName.hasLocalData()) {
        return QSqlDatabase::database(g_threadConnectionName.localData());
    }

    const QString name = QStringLiteral("smartroute_logger_%1")
        .arg(reinterpret_cast<quintptr>(QThread::currentThreadId()));

    QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), name);
    db.setDatabaseName(g_logger.dbPath);

    if (db.open()) {
        QSqlQuery pragma(db);
        pragma.exec(QStringLiteral("PRAGMA busy_timeout = 5000"));
        createLogTable(db);
    }

    g_threadConnectionName.setLocalData(name);
    return db;
}

static void trimLogTable(const QSqlDatabase &db)
{
    QSqlQuery q(db);
    q.prepare(QStringLiteral(
        "DELETE FROM logs WHERE id NOT IN ("
        "  SELECT id FROM logs ORDER BY id DESC LIMIT :max"
        ")"));
    q.bindValue(":max", g_logger.maxLogRows);

    if (q.exec()) {
        g_logger.currentRowCount = g_logger.maxLogRows;
    }
}

bool Logger::initialize(const QString& dbPath, bool printToDebug, int maxLines)
{
    if (g_logger.initialized) {
        return false;
    }

    g_logger.dbPath       = dbPath;
    g_logger.maxLogRows    = qMax(1, maxLines);
    g_logger.printToDebug = printToDebug;

    QFileInfo fileInfo(dbPath);
    QDir().mkpath(fileInfo.absolutePath());

    QSqlDatabase db = threadConnection();
    if (!db.isOpen()) {
        return false;
    }

    QSqlQuery q(db);
    if (q.exec(QStringLiteral("SELECT COUNT(*) FROM logs")) && q.next()) {
        g_logger.currentRowCount = q.value(0).toLongLong();
    }

    if (g_logger.currentRowCount > g_logger.maxLogRows) {
        trimLogTable(db);
    }

    g_logger.initialized = true;
    return true;
}

void Logger::log(const QString &message, LogLevel level)
{
    const QString timestamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"));
    const QString consoleLine = QStringLiteral("%1\t%2: %3").arg(timestamp, logLevelToString(level), message);

    if (!g_logger.initialized) {
        if (g_logger.printToDebug) {
            qDebug().noquote() << consoleLine;
        }
        return;
    }

    QSqlDatabase db = threadConnection();
    if (db.isOpen()) {
        QSqlQuery q(db);
        q.prepare(QStringLiteral(
            "INSERT INTO logs (timestamp, level, message) VALUES (:timestamp, :level, :message)"));
        q.bindValue(":timestamp", timestamp);
        q.bindValue(":level", logLevelToString(level));
        q.bindValue(":message", message);
        q.exec();

        QMutexLocker locker(&g_logger.rowCountMutex);
        ++g_logger.currentRowCount;
        if (g_logger.currentRowCount > g_logger.maxLogRows) {
            trimLogTable(db);
        }
    }

    if (g_logger.printToDebug) {
        qDebug().noquote() << consoleLine;
    }
}

QList<LogEntry> Logger::fetch(int limit, const QString &level, const QString &from, const QString &to, QString &error)
{
    QList<LogEntry> result;

    if (!g_logger.initialized) {
        error = QStringLiteral("Logger is not initialized.");
        return result;
    }

    QSqlDatabase db = threadConnection();
    if (!db.isOpen()) {
        error = QStringLiteral("Failed to open log database connection.");
        return result;
    }

    QString sql = QStringLiteral("SELECT id, timestamp, level, message FROM logs WHERE 1=1");
    if (!level.isEmpty()) sql += QStringLiteral(" AND level = :level");
    if (!from.isEmpty())  sql += QStringLiteral(" AND timestamp >= :from");
    if (!to.isEmpty())    sql += QStringLiteral(" AND timestamp <= :to");
    sql += QStringLiteral(" ORDER BY id DESC LIMIT :limit");

    QSqlQuery q(db);
    q.prepare(sql);
    if (!level.isEmpty()) q.bindValue(":level", logLevelToString(logLevelFromString(level)));
    if (!from.isEmpty())  q.bindValue(":from", from);
    if (!to.isEmpty())    q.bindValue(":to", to);
    q.bindValue(":limit", qMax(1, limit));

    if (!q.exec()) {
        error = q.lastError().text();
        return result;
    }

    while (q.next()) {
        LogEntry entry;
        entry.id        = q.value(0).toLongLong();
        entry.timestamp = QDateTime::fromString(q.value(1).toString(), QStringLiteral("yyyy-MM-dd HH:mm:ss"));
        entry.level     = logLevelFromString(q.value(2).toString());
        entry.message   = q.value(3).toString();
        result.append(entry);
    }

    return result;
}

int Logger::maxLogRows()
{
    return g_logger.maxLogRows;
}

void Logger::info(const QString &message)
{
    log(message, LogLevel::Info);
}

void Logger::warning(const QString &message)
{
    log(message, LogLevel::Warn);
}

void Logger::error(const QString &message)
{
    log(message, LogLevel::Error);
}

} // namespace Util
