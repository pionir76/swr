#pragma once

#include <QString>
#include <QDateTime>
#include <QList>

namespace Util{

enum class LogLevel {
    Info,
    Warn,
    Error
};

struct LogEntry {
    qint64 id = -1;
    QDateTime timestamp;
    LogLevel level = LogLevel::Info;
    QString message;
};

// Logs are persisted to a SQLite database (table "logs") instead of a flat file.
// Safe to call from any thread — each calling thread gets its own SQLite connection.
class Logger
{
    public:
        static bool initialize(const QString& dbPath, bool printToDebug = false, int maxLines = 1000);
        static void log(const QString &message, LogLevel level = LogLevel::Info);
        static void info(const QString &message);
        static void warning(const QString &message);
        static void error(const QString &message);

        // level/from/to empty = unfiltered. from/to compared against the stored
        // "yyyy-MM-dd HH:mm:ss" timestamp format. Newest first.
        static QList<LogEntry> fetch(int limit,
                                     const QString &level,
                                     const QString &from,
                                     const QString &to,
                                     QString &error);
        static int maxLogRows();
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
