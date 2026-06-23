#pragma once

#include "../model/DeviceModels.h"

#include <QMutex>
#include <QMutexLocker>
#include <QQueue>

namespace DataCollection {
namespace Polling {

class PollLogQueue {
public:
    void push(const Model::PollLogEntry &entry) {
        QMutexLocker locker(&m_mutex);
        m_queue.enqueue(entry);
    }

    QList<Model::PollLogEntry> popAll() {
        QMutexLocker locker(&m_mutex);
        QList<Model::PollLogEntry> batch;
        batch.reserve(m_queue.size());
        while (!m_queue.isEmpty())
            batch.append(m_queue.dequeue());
        return batch;
    }

private:
    QMutex                      m_mutex;
    QQueue<Model::PollLogEntry> m_queue;
};

} // namespace Polling
} // namespace DataCollection
