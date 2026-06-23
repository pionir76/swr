#pragma once

#include "PollLogQueue.h"

#include <QThread>
#include <atomic>

namespace DataCollection {
namespace Polling {

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
