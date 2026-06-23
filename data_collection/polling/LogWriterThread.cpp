#include "LogWriterThread.h"

#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlError>
#include <QDebug>

namespace DataCollection {
namespace Polling {

LogWriterThread::LogWriterThread(const QString &dbPath, PollLogQueue *queue, QObject *parent)
    : QThread(parent)
    , m_dbPath(dbPath)
    , m_queue(queue)
{
}

void LogWriterThread::stop()
{
    m_running = false;
    wait();
}

void LogWriterThread::run()
{
    m_connName = QStringLiteral("log_writer_%1").arg(quintptr(currentThreadId()));

    {
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_connName);
        db.setDatabaseName(m_dbPath);

        if (!db.open()) {
            qWarning("LogWriterThread: failed to open DB: %s",
                     qPrintable(db.lastError().text()));
            QSqlDatabase::removeDatabase(m_connName);
            return;
        }

        m_running = true;

        while (m_running) {
            msleep(500);
            const QList<Model::PollLogEntry> entries = m_queue->popAll();
            if (!entries.isEmpty())
                writeEntries(entries);
        }

        // 종료 전 남은 항목 플러시
        const QList<Model::PollLogEntry> remaining = m_queue->popAll();
        if (!remaining.isEmpty())
            writeEntries(remaining);

        db.close();
    }

    QSqlDatabase::removeDatabase(m_connName);
}

void LogWriterThread::writeEntries(const QList<Model::PollLogEntry> &entries)
{
    QSqlDatabase db = QSqlDatabase::database(m_connName);

    db.transaction();
    for (const Model::PollLogEntry &e : entries) {
        QSqlQuery ins(db);
        ins.prepare(
            "INSERT INTO device_poll_log "
            "(device_id, device_name, timestamp, success, duration_ms, register_count, message) "
            "VALUES (?, ?, ?, ?, ?, ?, ?)"
        );
        ins.addBindValue(e.deviceId);
        ins.addBindValue(e.deviceName);
        ins.addBindValue(e.timestamp);
        ins.addBindValue(e.success ? 1 : 0);
        ins.addBindValue(e.durationMs);
        ins.addBindValue(e.registerCount);
        ins.addBindValue(e.message);
        if (!ins.exec())
            qWarning("LogWriterThread: INSERT failed: %s", qPrintable(ins.lastError().text()));

        QSqlQuery del(db);
        del.prepare(
            "DELETE FROM device_poll_log "
            "WHERE device_id = ? AND id NOT IN ("
            "  SELECT id FROM device_poll_log WHERE device_id = ? ORDER BY id DESC LIMIT 100"
            ")"
        );
        del.addBindValue(e.deviceId);
        del.addBindValue(e.deviceId);
        if (!del.exec())
            qWarning("LogWriterThread: DELETE failed: %s", qPrintable(del.lastError().text()));
    }
    db.commit();
}

} // namespace Polling
} // namespace DataCollection
