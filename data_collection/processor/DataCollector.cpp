#include "DataCollector.h"

namespace DataCollection {
namespace Processor {

DataCollector::DataCollector(const Model::DeviceInfo &device,
                             std::shared_ptr<Store::DeviceList> deviceList)
    : m_device(device)
    , m_deviceList(std::move(deviceList))
{
}

bool DataCollector::initialize(QString &error)
{
    auto client = Comm::createDeviceClient(m_device.connection);

    if (!client) {
        error = QStringLiteral("Failed to create device client for device %1 (unsupported protocol)")
        .arg(m_device.id);
        return false;
    }

    m_executor = std::make_unique<Comm::RegisterExecutor>(std::move(client), m_device.connection.defaultByteOrder);
    return m_executor->connect(error);
}

bool DataCollector::collectField(const Model::RegisterField &field,
                                 QVector<quint16> &registerValues,
                                 QVector<bool> &coilValues,
                                 QString &error)
{
    if (!m_executor) {
        if (!initialize(error)) {
            return false;
        }
    }

    if (!m_executor->isConnected()) {
        if (!m_executor->connect(error)) {
            return false;
        }
    }

    return m_executor->readField(field, registerValues, coilValues, error);
}


void DataCollector::flushWrites()
{
    if (!m_deviceList) return;

    for (const Model::WriteRequest &req : m_deviceList->takeWrites(m_device.id)) {
        QString error;
        if (!m_executor || !m_executor->isConnected()) {
            if (!initialize(error)) {
                qWarning("flushWrites: connect failed [device %d]: %s",
                         m_device.id, qPrintable(error));
                continue;
            }
        }
        if (!m_executor->writeField(req.field, req.rawValues, req.coilValues, error)) {
            if (req.retryCount > 0) {
                Model::WriteRequest retry = req;
                retry.retryCount--;
                m_deviceList->enqueueWrite(m_device.id, std::move(retry));

                qWarning("flushWrites: write failed, retrying (%d left) [device %d, addr %d]: %s",
                         req.retryCount - 1, m_device.id, req.field.address, qPrintable(error));
            } else {
                qWarning("flushWrites: write failed, no retries left [device %d, addr %d]: %s",
                         m_device.id, req.field.address, qPrintable(error));
            }
        }
    }
}


} // namespace Processor
} // namespace DataCollection
