#pragma once

#include "PollLogQueue.h"

#include <QThread>
#include <atomic>

namespace DataCollection {
namespace Polling {

//--------------------------------------------------------------------------------//
// LogWriterThread is a QThread that continuously pops PollLogEntry items from a
// PollLogQueue and writes them to a SQLite database. It runs in its own thread and
// ensures that log entries are written to the database in a thread-safe manner.
// Every 500ms, pop all entries from the queue and write them to the database
//--------------------------------------------------------------------------------//
class LogWriterThread : public QThread {
    Q_OBJECT
public:
    LogWriterThread(const QString &dbPath, PollLogQueue *queue, QObject *parent = nullptr);
    void stop();

protected:
    void run() override;

private:
    void writeEntries(const QList<Model::PollLogEntry> &entries);

    QString       m_dbPath;
    PollLogQueue *m_queue;
    QString       m_connName;
    std::atomic<bool> m_running{false};
};

} // namespace Polling
} // namespace DataCollection
