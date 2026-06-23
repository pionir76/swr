#pragma once

#include "../store/RegisterTable.h"
#include "../store/DeviceList.h"
#include "SerialWorker.h"
#include "TcpWorker.h"
#include "PollLogQueue.h"
#include "LogWriterThread.h"

namespace DataCollection {
namespace Polling {

class PollingManager
{
public:
    PollingManager(const QString &dbPath,
                   std::shared_ptr<Store::RegisterTable> table,
                   std::shared_ptr<Store::DeviceList> deviceList);

    ~PollingManager();

    bool start(QString& error);
    void stop();
    bool isRunning() const;

private:
    QString m_dbPath;
    std::shared_ptr<Store::RegisterTable> m_table;
    std::shared_ptr<Store::DeviceList> m_deviceList;

    std::unique_ptr<PollLogQueue>    m_logQueue;
    std::unique_ptr<LogWriterThread> m_logWriter;

    std::unique_ptr<SerialWorker> m_serialWorker;
    QList<TcpWorker *> m_tcpWorkers;

    bool m_running = false;
};

} // namespace Polling
} // namespace DataCollection
