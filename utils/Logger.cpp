#include "Logger.h"

#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlError>
#include <QFileInfo>
#include <QDir>
#include <QDebug>

namespace Util {

Logger *Logger::s_instance = nullptr;

// ── Construction ─────────────────────────────────────────────────────────────

Logger::Logger(const QString &dbPath, bool printToDebug, int maxLines)
    : m_dbPath(dbPath)
    , m_printToDebug(printToDebug)
    , m_maxLogRows(qMax(1, maxLines))
{
}

bool Logger::initialize(const QString &dbPath, bool printToDebug, int maxLines)
{
    if (s_instance)
        return false;

    QFileInfo fileInfo(dbPath);
    QDir().mkpath(fileInfo.absolutePath());

    s_instance = new Logger(dbPath, printToDebug, maxLines);
    s_instance->start();
    return true;
}

void Logger::shutdown()
{
    if (!s_instance)
        return;

    s_instance->m_running = false;
    s_instance->wait();
    delete s_instance;
    s_instance = nullptr;
}

// ── Writer thread ─────────────────────────────────────────────────────────────

void Logger::run()
{
    m_connName = QStringLiteral("logger_writer_%1").arg(quintptr(currentThreadId()));

    {
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_connName);
        db.setDatabaseName(m_dbPath);

        if (!db.open()) {
            qWarning("Logger: failed to open DB: %s", qPrintable(db.lastError().text()));
            QSqlDatabase::removeDatabase(m_connName);
            return;
        }

        QSqlQuery pragma(db);
        pragma.exec(QStringLiteral("PRAGMA journal_mode = WAL"));
        pragma.exec(QStringLiteral("PRAGMA busy_timeout = 5000"));

        QSqlQuery create(db);
        create.exec(QStringLiteral(
            "CREATE TABLE IF NOT EXISTS logs ("
            "  id        INTEGER PRIMARY KEY AUTOINCREMENT,"
            "  timestamp TEXT    NOT NULL,"
            "  level     TEXT    NOT NULL,"
            "  message   TEXT    NOT NULL"
            ")"
        ));

        if (create.exec(QStringLiteral("SELECT COUNT(*) FROM logs")) && create.next())
            m_rowCount = create.value(0).toLongLong();

        m_running = true;

        while (m_running) {
            msleep(500);
            drain(db);
        }

        drain(db);  // flush remaining entries before exit
        db.close();
    }

    QSqlDatabase::removeDatabase(m_connName);
}

void Logger::drain(QSqlDatabase &db)
{
    QList<QueueEntry> batch;
    {
        QMutexLocker locker(&m_mutex);
        batch.reserve(m_queue.size());
        while (!m_queue.isEmpty())
            batch.append(m_queue.dequeue());
    }
    if (batch.isEmpty())
        return;

    db.transaction();
    QSqlQuery q(db);
    q.prepare(QStringLiteral(
        "INSERT INTO logs (timestamp, level, message) VALUES (?, ?, ?)"
    ));
    for (const QueueEntry &e : batch) {
        const QString ts = e.timestamp.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"));
        q.addBindValue(ts);
        q.addBindValue(logLevelToString(e.level));
        q.addBindValue(e.message);
        q.exec();
        ++m_rowCount;
    }
    db.commit();

    trimIfNeeded(db);
}

void Logger::trimIfNeeded(QSqlDatabase &db)
{
    if (m_rowCount <= m_maxLogRows)
        return;

    QSqlQuery q(db);
    q.prepare(QStringLiteral(
        "DELETE FROM logs WHERE id NOT IN "
        "(SELECT id FROM logs ORDER BY id DESC LIMIT ?)"
    ));
    q.addBindValue(m_maxLogRows);
    if (q.exec())
        m_rowCount = m_maxLogRows;
}

// ── Public static API ─────────────────────────────────────────────────────────

void Logger::pushEntry(const QueueEntry &entry)
{
    if (m_printToDebug) {
        const QString line = QStringLiteral("%1\t%2: %3")
            .arg(entry.timestamp.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss")),
                 logLevelToString(entry.level),
                 entry.message);
        qDebug().noquote() << line;
    }

    QMutexLocker locker(&m_mutex);
    m_queue.enqueue(entry);
}

void Logger::log(const QString &message, LogLevel level)
{
    if (!s_instance) {
        qDebug().noquote() << logLevelToString(level) << ":" << message;
        return;
    }
    s_instance->pushEntry({QDateTime::currentDateTime(), level, message});
}

void Logger::info(const QString &message)    { log(message, LogLevel::Info);  }
void Logger::warning(const QString &message) { log(message, LogLevel::Warn);  }
void Logger::error(const QString &message)   { log(message, LogLevel::Error); }

int Logger::maxLogRows()
{
    return s_instance ? s_instance->m_maxLogRows : 0;
}

// ── fetch() — opens a per-calling-thread read connection ────────────────────

static QSqlDatabase openFetchConnection(const QString &dbPath, QString &error)
{
    const QString connName = QStringLiteral("logger_fetch_%1")
        .arg(quintptr(QThread::currentThreadId()));

    if (QSqlDatabase::contains(connName)) {
        QSqlDatabase db = QSqlDatabase::database(connName);
        if (!db.isOpen()) db.open();
        return db;
    }

    QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connName);
    db.setDatabaseName(dbPath);
    if (!db.open()) {
        error = db.lastError().text();
        QSqlDatabase::removeDatabase(connName);
        return {};
    }
    return db;
}

QList<LogEntry> Logger::fetch(int limit,
                               int offset,
                               const QString &level,
                               const QString &from,
                               const QString &to,
                               QString &error)
{
    if (!s_instance) {
        error = QStringLiteral("Logger is not initialized.");
        return {};
    }

    QSqlDatabase db = openFetchConnection(s_instance->m_dbPath, error);
    if (!db.isValid()) return {};

    QString sql = QStringLiteral("SELECT id, timestamp, level, message FROM logs WHERE 1=1");
    if (!level.isEmpty()) sql += QStringLiteral(" AND level = :level");
    if (!from.isEmpty())  sql += QStringLiteral(" AND timestamp >= :from");
    if (!to.isEmpty())    sql += QStringLiteral(" AND timestamp <= :to");
    sql += QStringLiteral(" ORDER BY id DESC LIMIT :limit OFFSET :offset");

    QSqlQuery q(db);
    q.prepare(sql);
    if (!level.isEmpty()) q.bindValue(QStringLiteral(":level"), logLevelToString(logLevelFromString(level)));
    if (!from.isEmpty())  q.bindValue(QStringLiteral(":from"),  from);
    if (!to.isEmpty())    q.bindValue(QStringLiteral(":to"),    to);
    q.bindValue(QStringLiteral(":limit"),  qMax(1, limit));
    q.bindValue(QStringLiteral(":offset"), qMax(0, offset));

    if (!q.exec()) {
        error = q.lastError().text();
        return {};
    }

    QList<LogEntry> result;
    while (q.next()) {
        LogEntry entry;
        entry.id        = q.value(0).toLongLong();
        entry.timestamp = QDateTime::fromString(q.value(1).toString(),
                                                QStringLiteral("yyyy-MM-dd HH:mm:ss"));
        entry.level     = logLevelFromString(q.value(2).toString());
        entry.message   = q.value(3).toString();
        result.append(entry);
    }
    return result;
}

qint64 Logger::count(const QString &level,
                     const QString &from,
                     const QString &to,
                     QString &error)
{
    if (!s_instance) {
        error = QStringLiteral("Logger is not initialized.");
        return 0;
    }

    QSqlDatabase db = openFetchConnection(s_instance->m_dbPath, error);
    if (!db.isValid()) return 0;

    QString sql = QStringLiteral("SELECT COUNT(*) FROM logs WHERE 1=1");
    if (!level.isEmpty()) sql += QStringLiteral(" AND level = :level");
    if (!from.isEmpty())  sql += QStringLiteral(" AND timestamp >= :from");
    if (!to.isEmpty())    sql += QStringLiteral(" AND timestamp <= :to");

    QSqlQuery q(db);
    q.prepare(sql);
    if (!level.isEmpty()) q.bindValue(QStringLiteral(":level"), logLevelToString(logLevelFromString(level)));
    if (!from.isEmpty())  q.bindValue(QStringLiteral(":from"),  from);
    if (!to.isEmpty())    q.bindValue(QStringLiteral(":to"),    to);

    if (!q.exec() || !q.next()) {
        error = q.lastError().text();
        return 0;
    }
    return q.value(0).toLongLong();
}

bool Logger::clearAll(QString &error)
{
    if (!s_instance) {
        error = QStringLiteral("Logger not initialized");
        return false;
    }

    QSqlDatabase db = openFetchConnection(s_instance->m_dbPath, error);
    if (!db.isValid()) return false;

    QSqlQuery q(db);
    if (!q.exec(QStringLiteral("DELETE FROM logs"))) {
        error = q.lastError().text();
        return false;
    }

    s_instance->m_rowCount = 0;
    return true;
}

} // namespace Util
