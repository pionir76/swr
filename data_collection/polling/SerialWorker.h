#pragma once

#include "../model/DeviceModels.h"
#include "../store/RegisterTable.h"
#include "../store/DeviceList.h"
#include "PollLogQueue.h"

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
                 PollLogQueue *logQueue,
                 QObject* parent = nullptr);

    void stop();

protected:
    void run() override;

private:
    QList<Model::DeviceInfo> m_devices;
    std::shared_ptr<Store::RegisterTable> m_table;
    std::shared_ptr<Store::DeviceList> m_deviceList;
    PollLogQueue *m_logQueue = nullptr;
    std::atomic<bool> m_running{false};
};

} // namespace Polling
} // namespace DataCollection
