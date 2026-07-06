#pragma once

#include "../model/DeviceModels.h"
#include "../store/RegisterTable.h"
#include "../store/DeviceList.h"

#include <QThread>
#include <memory>
#include <atomic>

namespace DataCollection {
namespace Polling {

class TcpWorker : public QThread {
    Q_OBJECT
public:
    TcpWorker(Model::DeviceInfo device,
              std::shared_ptr<Store::RegisterTable> table,
              std::shared_ptr<Store::DeviceList> deviceList,
              QObject *parent = nullptr);

    void stop();

protected:
    void run() override;

private:
    Model::DeviceInfo m_device;
    std::shared_ptr<Store::RegisterTable> m_registerTable;
    std::shared_ptr<Store::DeviceList> m_deviceList;
    std::atomic<bool> m_running{false};
};

} // namespace Polling
} // namespace DataCollection
