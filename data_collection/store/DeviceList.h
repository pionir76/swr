#pragma once

#include <QHash>
#include <QList>
#include <QMutex>

#include "../model/DeviceModels.h"

namespace DataCollection {
namespace Store {

class DeviceList
{
public:
    void reset(const QList<Model::DeviceInfo>& devices);
    void updateStatus(int deviceId, const Model::DeviceInfo::Status& status);
    void add(const Model::DeviceInfo& device);
    void update(const Model::DeviceInfo& device);
    void remove(int deviceId);

    Model::DeviceInfo get(int deviceId) const;
    QList<Model::DeviceInfo> getAll() const;
    Model::RegisterConfig findByRegisterId(int id, bool &found) const;
    Model::RegisterConfig findByUnifiedId(int unifiedId, bool &found) const;

    void enqueueWrite(int deviceId, Model::WriteRequest req);
    QList<Model::WriteRequest> takeWrites(int deviceId);

private:
    mutable QMutex m_mutex;
    QHash<int, Model::DeviceInfo> m_devices;
    QHash<int, QList<Model::WriteRequest>> m_writes;
};

} // namespace Store
} // namespace DataCollection
