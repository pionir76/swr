#pragma once

#include "../comm/DeviceClientFactory.h"
#include "../comm/RegisterExecutor.h"
#include "../model/DeviceModels.h"
#include "../store/DeviceList.h"

#include <QString>
#include <QVector>
#include <memory>

namespace DataCollection {
namespace Processor {

class DataCollector {
public:
    DataCollector(const Model::DeviceInfo &device,
                  std::shared_ptr<Store::DeviceList> deviceList = nullptr);

    bool initialize(QString &error);
    bool collectField(const Model::RegisterField &field,
                      QVector<quint16> &registerValues,
                      QVector<bool> &coilValues,
                      QString &error);

    void flushWrites();

private:
    Model::DeviceInfo m_device;
    std::shared_ptr<Store::DeviceList> m_deviceList;
    std::unique_ptr<Comm::RegisterExecutor> m_executor;
};

} // namespace Processor
} // namespace DataCollection
