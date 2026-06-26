#pragma once

#include <QString>
#include <QDateTime>
#include <QList>
#include <QThread>
#include <QMutex>
#include <QQueue>
#include <atomic>
#include <QSqlDatabase>

namespace Util {

enum class LogLevel {
    Info,
    Warn,
    Error
};

struct LogEntry {
    qint64    id        = -1;
    QDateTime timestamp;
    LogLevel  level     = LogLevel::Info;
    QString   message;
};

//--------------------------------------------------------------------//
// Async logger backed by a single dedicated writer thread.
// Safe to call from any thread after initialize().
// Call shutdown() before application exit to flush remaining entries.
//--------------------------------------------------------------------//
class Logger : public QThread
{
    Q_OBJECT
public:
    static bool initialize(const QString &dbPath, bool printToDebug = false, int maxLines = 1000);
    static void shutdown();

    static void log(const QString &message, LogLevel level = LogLevel::Info);
    static void info(const QString &message);
    static void warning(const QString &message);
    static void error(const QString &message);

    // level/from/to empty = unfiltered. Newest first.
    static QList<LogEntry> fetch(int limit,
                                 int offset,
                                 const QString &level,
                                 const QString &from,
                                 const QString &to,
                                 QString &error);

    // total row count matching the same filters (for pagination)
    static qint64 count(const QString &level,
                        const QString &from,
                        const QString &to,
                        QString &error);

    static bool clearAll(QString &error);

    static int maxLogRows();

protected:
    void run() override;

private:
    explicit Logger(const QString &dbPath, bool printToDebug, int maxLines);

    struct QueueEntry {
        QDateTime timestamp;
        LogLevel  level;
        QString   message;
    };

    void pushEntry(const QueueEntry &entry);
    void drain(QSqlDatabase &db);
    void trimIfNeeded(QSqlDatabase &db);

    QString  m_dbPath;
    bool     m_printToDebug;
    int      m_maxLogRows;
    qint64   m_rowCount = 0;
    QString  m_connName;

    std::atomic<bool> m_running{false};
    QMutex            m_mutex;
    QQueue<QueueEntry> m_queue;

    static Logger *s_instance;
};

//--------------------------------------------------------------------//
// Helper
//--------------------------------------------------------------------//
inline QString logLevelToString(LogLevel level)
{
    switch (level) {
    case LogLevel::Warn:  return QStringLiteral("WARN");
    case LogLevel::Error: return QStringLiteral("ERROR");
    default:              return QStringLiteral("INFO");
    }
}

inline LogLevel logLevelFromString(const QString &s)
{
    if (s == QLatin1String("ERROR")) return LogLevel::Error;
    if (s == QLatin1String("WARN"))  return LogLevel::Warn;
    return LogLevel::Info;
}

} // namespace Util
