#pragma once

#include "../store/RegisterTable.h"
#include "../store/DeviceList.h"
#include "SerialWorker.h"
#include "TcpWorker.h"

namespace DataCollection {
namespace Polling {

class PollingManager
{
public:
    PollingManager(std::shared_ptr<Store::RegisterTable> table,
                   std::shared_ptr<Store::DeviceList> deviceList);

    ~PollingManager();

    bool start(QString& error);
    void stop();
    bool isRunning() const;

private:
    std::shared_ptr<Store::RegisterTable> m_registerTable;
    std::shared_ptr<Store::DeviceList> m_deviceList;

    std::unique_ptr<SerialWorker> m_serialWorker;
    QList<TcpWorker *> m_tcpWorkers;

    bool m_running = false;
};

} // namespace Polling
} // namespace DataCollection
