#include "DeviceList.h"

#include <QMutexLocker>
#include <stdexcept>

namespace DataCollection {
namespace Store {

void DeviceList::reset(const QList<Model::DeviceInfo> &devices)
{
    QMutexLocker locker(&m_mutex);
    m_devices.clear();
    m_writes.clear();

    for (const Model::DeviceInfo &d : devices){
        m_devices[d.id] = d;
    }
}

void DeviceList::updateStatus(int deviceId, const Model::DeviceInfo::Status &status)
{
    QMutexLocker locker(&m_mutex);
    auto it = m_devices.find(deviceId);

    if (it == m_devices.end()){
        throw std::out_of_range("DeviceList::updateStatus: device id not found (" + std::to_string(deviceId) + ")");
    }
    it->status = status;
}

void DeviceList::add(const Model::DeviceInfo &device)
{
    if (device.id < 0){
        throw std::invalid_argument("DeviceList::add: device id is invalid (" + std::to_string(device.id) + ")");
    }

    QMutexLocker locker(&m_mutex);
    if (m_devices.contains(device.id)){
        throw std::logic_error("DeviceList::add: device id already exists (" + std::to_string(device.id) + ")");
    }
    m_devices[device.id] = device;
}

void DeviceList::update(const Model::DeviceInfo &device)
{
    if (device.id < 0){
        throw std::invalid_argument("DeviceList::update: device id is invalid (" + std::to_string(device.id) + ")");
    }

    QMutexLocker locker(&m_mutex);
    if (!m_devices.contains(device.id)){
        throw std::out_of_range("DeviceList::update: device id not found (" + std::to_string(device.id) + ")");
    }
    m_devices[device.id] = device;
}

void DeviceList::remove(int deviceId)
{
    QMutexLocker locker(&m_mutex);
    if (!m_devices.contains(deviceId)){
        throw std::out_of_range("DeviceList::remove: device id not found (" + std::to_string(deviceId) + ")");
    }
    m_devices.remove(deviceId);
}

Model::DeviceInfo DeviceList::get(int deviceId) const
{
    QMutexLocker locker(&m_mutex);

    auto it = m_devices.find(deviceId);
    if (it == m_devices.end()){
        throw std::out_of_range("DeviceList::get: device id not found (" + std::to_string(deviceId) + ")");
    }
    return *it;
}

QList<Model::DeviceInfo> DeviceList::getAll() const
{
    QMutexLocker locker(&m_mutex);
    return m_devices.values();
}

//-----------------------------------------------------------------------------------//
// Find a register config by its database ID (id) or unified address (unifiedAddress).
// It's OK register ID is unique across devices,
// but unifiedAddress is unique across all devices.
//-----------------------------------------------------------------------------------//
Model::RegisterConfig DeviceList::findByRegisterId(int id, bool &found) const
{
    QMutexLocker locker(&m_mutex);

    for (const Model::DeviceInfo &device : m_devices) {
        for (const Model::RegisterConfig &f : device.registers) {
            if (f.id == id) {
                found = true;
                return f;
            }
        }
    }
    found = false;
    return {};
}

Model::RegisterConfig DeviceList::findByUnifiedAddress(int unifiedAddress, bool &found) const
{
    QMutexLocker locker(&m_mutex);

    for (const Model::DeviceInfo &device : m_devices) {
        for (const Model::RegisterConfig &f : device.registers) {
            if (f.unifiedAddress == unifiedAddress) {
                found = true;
                return f;
            }
        }
    }
    found = false;
    return {};
}

void DeviceList::enqueueWrite(int deviceId, Model::WriteRequest req)
{
    QMutexLocker locker(&m_mutex);
    m_writes[deviceId].append(std::move(req));
}

QList<Model::WriteRequest> DeviceList::takeWrites(int deviceId)
{
    QMutexLocker locker(&m_mutex);
    return m_writes.take(deviceId);
}

} // namespace Store
} // namespace DataCollection
