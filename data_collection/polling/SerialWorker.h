#pragma once

#include "../model/DeviceModels.h"
#include "../store/RegisterTable.h"
#include "../store/DeviceList.h"

#include <QThread>
#include <QList>
#include <memory>
#include <atomic>

namespace DataCollection {
namespace Polling {

class SerialWorker : public QThread
{
    Q_OBJECT

public:
    SerialWorker(QList<Model::DeviceInfo> devices,
                 std::shared_ptr<Store::RegisterTable> table,
                 std::shared_ptr<Store::DeviceList> deviceList,
                 QObject* parent = nullptr);

    void requestStop() { m_running = false; }   // returns immediately
    void stop();                                 // requestStop() + wait()

protected:
    void run() override;

private:
    QList<Model::DeviceInfo> m_devices;
    std::shared_ptr<Store::RegisterTable> m_registerTable;
    std::shared_ptr<Store::DeviceList> m_deviceList;
    std::atomic<bool> m_running{true};
};

} // namespace Polling
} // namespace DataCollection
