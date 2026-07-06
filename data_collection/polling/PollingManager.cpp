#include "PollingManager.h"
#include "../../utils/Logger.h"

namespace DataCollection {
namespace Polling {

PollingManager::PollingManager(std::shared_ptr<Store::RegisterTable> table,
                               std::shared_ptr<Store::DeviceList> deviceList)
    : m_registerTable(std::move(table))
    , m_deviceList(std::move(deviceList))
{
}

PollingManager::~PollingManager()
{
    stop();
}

bool PollingManager::start(QString& error)
{
    if (m_running) {
        error = QStringLiteral("Polling is already running.");
        return false;
    }

    m_registerTable->clear();

    const QList<Model::DeviceInfo> devices = m_deviceList->getAll();

    QList<Model::DeviceInfo> serialDevices;
    QList<Model::DeviceInfo> tcpDevices;

    for (const Model::DeviceInfo &d : devices) {
        if (d.connection.type == Model::DeviceConnection::ConnectionType::Serial)
            serialDevices.append(d);
        else
            tcpDevices.append(d);
    }

    //-----------------------------------------------------------//
    // Start SerialWorker for serial devices 
    // Manage all serial devices in a single thread to avoid 
    // conflicts on the same serial port.
    //-----------------------------------------------------------//
    if (!serialDevices.isEmpty()) {
        m_serialWorker = std::make_unique<SerialWorker>(
            serialDevices,
            m_registerTable,
            m_deviceList);
            
        m_serialWorker->start();
        Util::Logger::info(QStringLiteral("SerialWorker started: %1 device(s)").arg(serialDevices.size()));
    }

    //-----------------------------------------------------------//
    // Start TcpWorker for TCP devices
    // Each TCP device is managed in its own thread to allow
    // concurrent polling of multiple devices.
    //-----------------------------------------------------------// 
    for (const Model::DeviceInfo &d : tcpDevices) {
        auto *worker = new TcpWorker(d, m_registerTable, m_deviceList);
        m_tcpWorkers.append(worker);
        
        worker->start();
    }

    if (!tcpDevices.isEmpty()){
        Util::Logger::info(QStringLiteral("TcpWorker started: %1 device(s)").arg(tcpDevices.size()));
    }
        
    m_running = true;

    Util::Logger::info(QStringLiteral("Polling started. Total devices: %1").arg(devices.size()));
    return true;
}

void PollingManager::stop()
{
    if (!m_running) return;

    if (m_serialWorker) {
        m_serialWorker->stop();
        m_serialWorker.reset();
    }

    for (TcpWorker *w : m_tcpWorkers) {
        w->stop();
        delete w;
    }
    m_tcpWorkers.clear();

    m_running = false;
    Util::Logger::info(QStringLiteral("Polling stopped."));
}

bool PollingManager::isRunning() const
{
    return m_running;
}

} // namespace Polling
} // namespace DataCollection
